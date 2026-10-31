// clang-format off: extracted source fragments require this include order.
#include "main_camera_fence_mock.h"
#include "dma_header.inc"
#include "dma.inc"
// clang-format on

static pthread_mutex_t sink_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sink_changed = PTHREAD_COND_INITIALIZER;
static bool sink_block, sink_entered, sink_leave;
static atomic_int notifications, observed_status, stale;
static int sync_sink(int32_t id, struct cam_dma_fence_signal_sync_obj *signal) {
  assert(!provider_irq);
  pthread_mutex_lock(&sink_lock);
  sink_entered = true;
  pthread_cond_broadcast(&sink_changed);
  while (sink_block && !sink_leave)
    pthread_cond_wait(&sink_changed, &sink_lock);
  if (signal->sync_generation != cam_sync_get_generation(id))
    atomic_fetch_add(&stale, 1);
  else {
    atomic_fetch_add(&notifications, 1);
    atomic_store(&observed_status, signal->status);
  }
  pthread_mutex_unlock(&sink_lock);
  return 0;
}
static const char *external_name(struct dma_fence *f) {
  (void)f;
  return "external";
}
static const struct dma_fence_ops external_ops = {
    .get_driver_name = external_name,
    .release = dma_fence_free,
};
static struct dma_fence *new_external(void) {
  struct cam_dma_fence *f = kzalloc(sizeof(*f), GFP_KERNEL);
  spin_lock_init(&f->lock);
  dma_fence_init(&f->base, &external_ops, &f->lock, 9000, 1);
  return &f->base;
}
static int install_external(struct dma_fence *f) {
  int fd = get_unused_fd_flags(0);
  struct sync_file *s = sync_file_create(f);
  fd_install(fd, s->file);
  return fd;
}
static void fixture(void) {
  assert(!g_cam_dma_fence_dev && !atomic_load(&allocations));
  assert(!cam_dma_fence_driver_init());
  cam_sync_monitor_mask = 1;
  cam_dma_fence_open();
  atomic_store(&generations[1], 42);
  atomic_store(&notifications, 0);
  atomic_store(&observed_status, 0);
  atomic_store(&stale, 0);
  sink_block = sink_entered = sink_leave = false;
}
static void cleanup(void) {
  cam_dma_fence_driver_deinit();
  for (int i = 0; i < 64; i++)
    if (fds[i])
      close_fd(i);
  assert(!atomic_load(&allocations));
  assert(!atomic_load(&module_refs));
}
static int create_native(int *idx) {
  int fd;
  assert(!cam_dma_fence_create_fd(&fd, idx, "camera"));
  return fd;
}
static void register_row(int idx) {
  int id = 1;
  assert(!cam_dma_fence_register_cb(&id, &idx, sync_sink));
}
static void provider_signal(struct dma_fence *f, int error) {
  pthread_mutex_lock(f->lock);
  f->error = error;
  assert(!dma_fence_signal_locked(f));
  pthread_mutex_unlock(f->lock);
}
static void setup_failures(void) {
  atomic_store(&fail_allocation, 1);
  assert(cam_dma_fence_driver_init() == -ENOMEM);
  assert(!g_cam_dma_fence_dev && !atomic_load(&allocations));
  fail_queue = true;
  assert(cam_dma_fence_driver_init() == -ENOMEM);
  fail_queue = false;
  assert(!g_cam_dma_fence_dev && !atomic_load(&allocations));
  for (int failure = 0; failure < 4; failure++) {
    fixture();
    int before = atomic_load(&allocations), fd, idx;
    atomic_store(&fail_allocation, failure == 0 ? 1 : 0);
    fail_module = failure == 1;
    fail_fd = failure == 2;
    fail_sync_file = failure == 3;
    assert(cam_dma_fence_create_fd(&fd, &idx, "fail") < 0);
    atomic_store(&fail_allocation, 0);
    fail_module = fail_fd = fail_sync_file = false;
    assert(find_first_zero_bit(g_cam_dma_fence_dev->bitmap, 128) == 1);
    assert(atomic_load(&allocations) == before && !atomic_load(&module_refs));
    cleanup();
  }
}
static void registration_cases(void) {
  for (int mode = 0; mode < 5; mode++) {
    fixture();
    int idx, id = 1;
    int fd = create_native(&idx);
    struct dma_fence *f = fds[fd]->fence;
    if (mode == 0)
      atomic_store(&fail_allocation, 1);
    if (mode == 1)
      add_error = -EINVAL;
    if (mode == 2)
      provider_signal(f, -ECANCELED);
    if (mode == 3)
      signal_during_add = true;
    int rc = cam_dma_fence_register_cb(&id, &idx, sync_sink);
    atomic_store(&fail_allocation, 0);
    add_error = 0;
    signal_during_add = false;
    if (mode < 2) {
      assert(rc < 0);
      assert(!g_cam_dma_fence_dev->rows[idx].cb_registered_for_sync &&
             !f->callbacks);
      register_row(idx);
      provider_signal(f, 0);
    } else {
      assert(!rc);
      if (mode == 4) {
        int before = atomic_load(&allocations);
        register_row(idx);
        assert(before == atomic_load(&allocations));
        provider_signal(f, -EIO);
      }
    }
    flush_workqueue(g_cam_dma_fence_dev->work_queue);
    assert(atomic_load(&notifications) == 1);
    assert(atomic_load(&observed_status) == (mode == 2   ? -ECANCELED
                                             : mode == 4 ? -EIO
                                                         : 1));
    cleanup();
  }
}
static void imported_refs(void) {
  fixture();
  struct dma_fence *f = new_external();
  int fd = install_external(f), idx;
  struct dma_fence *lookup = cam_dma_fence_get_fence_from_fd(fd, &idx);
  assert(lookup == f && kref_read(&f->refcount) == 4);
  dma_fence_put(lookup);
  struct dma_fence *owned = cam_dma_fence_get_row_ref(idx, fd);
  assert(owned == f);
  register_row(idx);
  assert(!__cam_dma_fence_release(idx));
  assert(!f->callbacks && !f->signaled);
  assert(kref_read(&f->refcount) == 3);
  dma_fence_put(owned);
  provider_signal(f, 0);
  flush_workqueue(g_cam_dma_fence_dev->work_queue);
  assert(!atomic_load(&notifications));
  dma_fence_put(f);
  cleanup();
}
static void retained_fence(void) {
  fixture();
  int a, b;
  int fd = create_native(&a);
  struct dma_fence *f = fds[fd]->fence;
  u64 seq = f->seqno, context = f->context;
  assert(atomic_load(&module_refs) == 1);
  cam_dma_fence_close();
  assert(f->signaled && f->error == -EADV);
  cam_dma_fence_open();
  int second = create_native(&b);
  assert(fds[second]->fence->context == context &&
         fds[second]->fence->seqno > seq);
  cam_dma_fence_driver_deinit();
  assert(atomic_load(&module_refs) == 2);
  assert(!strcmp(f->ops->get_driver_name(f), "Camera DMA fence driver"));
  close_fd(fd);
  close_fd(second);
  assert(!atomic_load(&allocations) && !atomic_load(&module_refs));
}
static void recycled_fd(void) {
  fixture();
  int old_idx, new_idx, lookup_idx;
  int old_fd = create_native(&old_idx);
  struct dma_fence *old_fence = fds[old_fd]->fence;
  close_fd(old_fd);
  int new_fd = create_native(&new_idx);
  assert(new_fd == old_fd && new_idx != old_idx);
  struct dma_fence *lookup =
      cam_dma_fence_get_fence_from_fd(new_fd, &lookup_idx);
  assert(lookup != old_fence && lookup == fds[new_fd]->fence &&
         lookup_idx == new_idx);
  dma_fence_put(lookup);
  cleanup();
}
struct release_job {
  int idx;
  bool quiesce;
  atomic_bool entered, done;
};
static void *release_thread(void *opaque) {
  struct release_job *j = opaque;
  atomic_store(&j->entered, true);
  if (j->quiesce)
    cam_dma_fence_quiesce();
  else
    assert(!__cam_dma_fence_release(j->idx));
  atomic_store(&j->done, true);
  return NULL;
}
static void wait_flag(atomic_bool *flag) {
  for (int n = 0; n < 2000 && !atomic_load(flag); n++)
    usleep(1000);
  assert(atomic_load(flag));
}
static void running_callback_drain(void) {
  for (int repeat = 0; repeat < 100; repeat++) {
    fixture();
    int idx;
    int fd = create_native(&idx);
    sink_block = true;
    register_row(idx);
    provider_signal(fds[fd]->fence, 0);
    pthread_mutex_lock(&sink_lock);
    while (!sink_entered)
      pthread_cond_wait(&sink_changed, &sink_lock);
    pthread_mutex_unlock(&sink_lock);
    struct release_job j = {.idx = idx, .quiesce = repeat >= 50};
    pthread_t thread;
    pthread_create(&thread, NULL, release_thread, &j);
    wait_flag(&j.entered);
    usleep(1000);
    assert(!atomic_load(&j.done));
    assert(test_bit(idx, g_cam_dma_fence_dev->bitmap));
    pthread_mutex_lock(&sink_lock);
    sink_leave = true;
    pthread_cond_broadcast(&sink_changed);
    pthread_mutex_unlock(&sink_lock);
    pthread_join(thread, NULL);
    assert(atomic_load(&j.done));
    if (j.quiesce) {
      assert(g_cam_dma_fence_dev->rows[idx].fence == fds[fd]->fence);
      assert(!g_cam_dma_fence_dev->rows[idx].callback);
      assert(!__cam_dma_fence_release(idx));
    }
    int next_idx;
    create_native(&next_idx);
    assert(next_idx == idx);
    cleanup();
  }
}
static void queued_and_internal_signal(void) {
  fixture();
  int idx;
  int fd = create_native(&idx);
  struct workqueue_struct *q = g_cam_dma_fence_dev->work_queue;
  pthread_mutex_lock(&q->lock);
  q->pause = true;
  pthread_mutex_unlock(&q->lock);
  register_row(idx);
  struct cam_dma_fence_signal signal = {.dma_fence_fd = fd, .status = 0};
  struct dma_fence *other = new_external();
  assert(cam_dma_fence_internal_signal(idx, &signal, other) == -EINVAL);
  assert(!fds[fd]->fence->signaled);
  pthread_mutex_lock(&sink_lock);
  assert(!cam_dma_fence_internal_signal(idx, &signal, fds[fd]->fence));
  pthread_mutex_unlock(&sink_lock);
  assert(!__cam_dma_fence_release(idx));
  pthread_mutex_lock(&q->lock);
  q->pause = false;
  pthread_cond_broadcast(&q->changed);
  pthread_mutex_unlock(&q->lock);
  flush_workqueue(q);
  assert(!atomic_load(&notifications));
  dma_fence_put(other);
  cleanup();
}
static void generation_race(void) {
  fixture();
  int idx;
  int fd = create_native(&idx);
  struct workqueue_struct *q = g_cam_dma_fence_dev->work_queue;
  pthread_mutex_lock(&q->lock);
  q->pause = true;
  pthread_mutex_unlock(&q->lock);
  register_row(idx);
  provider_signal(fds[fd]->fence, 0);
  atomic_store(&generations[1], 43);
  pthread_mutex_lock(&q->lock);
  q->pause = false;
  pthread_cond_broadcast(&q->changed);
  pthread_mutex_unlock(&q->lock);
  flush_workqueue(q);
  assert(!atomic_load(&notifications) && atomic_load(&stale) == 1);
  cleanup();
}
int main(void) {
  setup_failures();
  registration_cases();
  imported_refs();
  retained_fence();
  recycled_fd();
  running_callback_drain();
  queued_and_internal_signal();
  generation_race();
  puts("{\"setup_failures\":6,\"registration_cases\":5,\"import_reference_"
       "cases\":1,\"retained_fence_reopen\":1,\"recycled_fd\":1,\"running_"
       "callback_release_races\":50,\"running_callback_quiesce_races\":50,"
       "\"queued_internal_signal\":1,\"generation_race\":1}");
  return 0;
}
