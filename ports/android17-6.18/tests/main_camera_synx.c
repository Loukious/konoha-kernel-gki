#include "main_camera_fence_mock.h"
typedef int32_t s32;
#define CAM_SYNX 1
#define CAM_SYNC 2
#define CAM_GENERIC_FENCE_TYPE_SYNX_OBJ 2
#define CAM_GENERIC_FENCE_TYPE_SYNX_OBJ_DUMP 3
#define CAM_SYNC_STATE_SIGNALED_SUCCESS 2
#define CAM_SYNC_STATE_SIGNALED_ERROR 3
#define CAM_SYNC_STATE_SIGNALED_CANCEL 4
#define SYNX_STATE_INVALID 0
#define SYNX_STATE_ACTIVE 1
#define SYNX_STATE_SIGNALED_SUCCESS 2
#define SYNX_STATE_SIGNALED_ERROR 3
#define SYNX_STATE_SIGNALED_CANCEL 4
#define SYNX_STATE_SIGNALED_EXTERNAL 5
#define SYNX_STATE_SIGNALED_SSR 6
#define SYNX_STATE_SIGNALED_MAX 64
#define SYNX_CREATE_GLOBAL_FENCE 0x02
#define SYNX_IMPORT_GLOBAL_FENCE 0x02
#define SYNX_IMPORT_DMA_FENCE 0x08
#define SYNX_IMPORT_INDV_PARAMS 0
#define SYNX_INIT_MAX 3
#define SYNX_CORE_APSS 0
#define SYNX_INVALID EINVAL
#define SYNX_ERR 0
#define SYNX_VERB 0
#define SYNX_DBG 0
#define dprintk(...) log_event(__VA_ARGS__)
#define msecs_to_jiffies(ms) (ms)
enum synx_client_id {
  SYNX_CLIENT_NATIVE,
  SYNX_CLIENT_ICP_CTX0,
  SYNX_CLIENT_MAX
};
enum cam_sync_synx_supported_cores { CAM_ICP_0_SYNX_CORE };
struct cam_synx_obj_signal {
  uint32_t synx_obj, status;
};
struct synx_session {
  bool live;
};
struct synx_queue_desc {
  unsigned long unused[4];
};
struct synx_initialization_params {
  const char *name;
  struct synx_queue_desc *ptr;
  int flags, id;
};
struct synx_create_params {
  void *fence;
  const char *name;
  uint32_t flags, *h_synx;
};
struct synx_import_indv_params {
  uint32_t flags, *new_h_synx;
  void *fence;
};
struct synx_import_params {
  int type;
  struct synx_import_indv_params indv;
};
struct synx_coredata {
  struct mutex obj_lock;
  struct dma_fence *fence;
  int status, global_status;
  bool live, subscribed;
};
struct synx_handle_coredata {
  struct synx_coredata *object;
};
struct synx_client {
  u64 id;
  atomic_int refs;
};
static struct synx_coredata objects[512];
static struct synx_handle_coredata handles[512];
static struct synx_client client = {.id = 17};
static unsigned int next_handle;
static int init_error, create_error, get_fence_error, wait_error,
    subscribe_count;
static bool init_null, get_fence_null;
static struct synx_session *session;
static struct synx_client *synx_get_client(struct synx_session *s) {
  assert(!provider_irq && s && s->live);
  atomic_fetch_add(&client.refs, 1);
  return &client;
}
static void synx_put_client(struct synx_client *c) {
  assert(atomic_fetch_sub(&c->refs, 1) > 0);
}
static struct synx_handle_coredata *
synx_util_acquire_handle(struct synx_client *c, u32 h) {
  (void)c;
  return h > 0 && h < 512 && objects[h].live ? &handles[h] : NULL;
}
static struct synx_coredata *
synx_util_obtain_object(struct synx_handle_coredata *h) {
  return h ? h->object : NULL;
}
static void synx_util_release_handle(struct synx_handle_coredata *h) {
  (void)h;
}
static bool synx_util_is_global_handle(u32 h) {
  assert(h && h < 512);
  return true;
}
static bool synx_util_is_merged_object(struct synx_coredata *o) {
  (void)o;
  return false;
}
static u32 synx_util_global_idx(u32 h) { return h; }
static int synx_global_test_status_set_wait(u32 idx, int core) {
  assert(objects[idx].live && core == SYNX_CORE_APSS);
  objects[idx].subscribed = true;
  subscribe_count++;
  return objects[idx].global_status;
}
#include "synx_status.inc"
static u32 synx_util_get_object_status(struct synx_coredata *o) {
  return __fence_state(o->fence, false);
}
static int synx_native_signal_fence(struct synx_coredata *o, u32 status) {
  pthread_mutex_lock(o->fence->lock);
  int rc = -EALREADY;
  if (!o->fence->signaled) {
    o->status = status;
    o->fence->error = status == SYNX_STATE_SIGNALED_SUCCESS ? 0 : -(int)status;
    rc = dma_fence_signal_locked(o->fence);
  }
  pthread_mutex_unlock(o->fence->lock);
  return rc;
}
static int synx_native_signal_merged_fence(struct synx_coredata *o,
                                           u32 status) {
  return synx_native_signal_fence(o, status);
}
static unsigned long dma_fence_wait_timeout(struct dma_fence *f, bool intr,
                                            u64 timeout) {
  (void)intr;
  assert(timeout == 0);
  return dma_fence_get_status(f) != 0;
}
#include "synx_provider.inc"
static void *synx_get_fence(struct synx_session *s, u32 h) {
  if (get_fence_error)
    return ERR_PTR(get_fence_error);
  if (get_fence_null)
    return NULL;
  return synx_internal_get_fence(s, h);
}
static int synx_wait(struct synx_session *s, u32 h, u64 timeout) {
  if (wait_error)
    return wait_error;
  return synx_internal_wait(s, h, timeout);
}
static int synx_get_status(struct synx_session *s, u32 h) {
  return synx_internal_get_status(s, h);
}
static const char *backing_name(struct dma_fence *f) {
  (void)f;
  return "Synx test backing";
}
static const struct dma_fence_ops backing_ops = {
    .get_driver_name = backing_name, .release = dma_fence_free};
struct mock_backing {
  struct dma_fence fence;
  spinlock_t lock;
};
static struct dma_fence *new_backing(void) {
  struct mock_backing *b = kzalloc(sizeof(*b), GFP_KERNEL);
  spin_lock_init(&b->lock);
  dma_fence_init(&b->fence, &backing_ops, &b->lock, 7000, 1);
  return &b->fence;
}
static struct synx_session *
synx_initialize(struct synx_initialization_params *p) {
  assert(p->id == SYNX_CLIENT_NATIVE && p->flags == SYNX_INIT_MAX);
  assert(!strcmp(p->name, "Camera_Generic_Synx_Session"));
  for (int i = 0; i < 4; i++)
    assert(!p->ptr->unused[i]);
  if (init_error)
    return ERR_PTR(init_error);
  if (init_null)
    return NULL;
  assert(!session);
  session = kzalloc(sizeof(*session), GFP_KERNEL);
  session->live = true;
  return session;
}
static int synx_uninitialize(struct synx_session *s) {
  assert(s == session && s->live && !atomic_load(&client.refs));
  for (int n = 1; n < 512; n++)
    assert(!objects[n].live);
  s->live = false;
  kfree(s);
  session = NULL;
  return 0;
}
static unsigned int add_handle(struct dma_fence *f) {
  unsigned int h = ++next_handle;
  assert(h < 512);
  objects[h] = (struct synx_coredata){.fence = f,
                                      .status = SYNX_STATE_ACTIVE,
                                      .global_status = SYNX_STATE_ACTIVE,
                                      .live = true};
  mutex_init(&objects[h].obj_lock);
  handles[h].object = &objects[h];
  return h;
}
static int synx_create(struct synx_session *s, struct synx_create_params *p) {
  assert(s == session && s->live && p->flags == SYNX_CREATE_GLOBAL_FENCE);
  assert(!p->fence);
  if (create_error)
    return create_error;
  *p->h_synx = add_handle(new_backing());
  return 0;
}
static int synx_import(struct synx_session *s, struct synx_import_params *p) {
  assert(s == session && s->live && p->type == SYNX_IMPORT_INDV_PARAMS);
  assert(p->indv.flags == (SYNX_IMPORT_GLOBAL_FENCE | SYNX_IMPORT_DMA_FENCE));
  if (create_error)
    return create_error;
  *p->indv.new_h_synx = add_handle(dma_fence_get(p->indv.fence));
  return 0;
}
static int synx_release(struct synx_session *s, u32 h) {
  assert(s == session && s->live && objects[h].live);
  assert(!objects[h].fence->callbacks);
  objects[h].live = false;
  dma_fence_put(objects[h].fence);
  objects[h].fence = NULL;
  mutex_destroy(&objects[h].obj_lock);
  return 0;
}
static int synx_signal(struct synx_session *s, u32 h, u32 status) {
  assert(s == session && s->live && objects[h].live);
  objects[h].global_status = status;
  mutex_lock(&objects[h].obj_lock);
  int rc = synx_native_signal_fence(&objects[h], status);
  mutex_unlock(&objects[h].obj_lock);
  return rc;
}
static int synx_recover(enum synx_client_id id) {
  assert(id == SYNX_CLIENT_ICP_CTX0);
  return 0;
}
static void __cam_synx_obj_dump_monitor_array(int idx) { (void)idx; }
static void __cam_synx_obj_save_previous_monitor_data(int idx) { (void)idx; }
// clang-format off: extracted source fragments require this include order.
#include "synx_header.inc"
#include "synx_camera.inc"
// clang-format on

static pthread_mutex_t sink_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sink_changed = PTHREAD_COND_INITIALIZER;
static bool sink_block, sink_entered, sink_leave;
static atomic_int notifications, observed_status, stale;
static int sync_sink(int32_t id, struct cam_synx_obj_signal_sync_obj *signal) {
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
static void fixture(void) {
  assert(!g_cam_synx_obj_dev && !atomic_load(&allocations));
  assert(!cam_synx_obj_driver_init());
  cam_sync_monitor_mask = 1UL << CAM_GENERIC_FENCE_TYPE_SYNX_OBJ;
  cam_synx_obj_open();
  atomic_store(&generations[1], 42);
  atomic_store(&notifications, 0);
  atomic_store(&observed_status, 0);
  atomic_store(&stale, 0);
  sink_block = sink_entered = sink_leave = false;
  subscribe_count = 0;
}
static void cleanup(void) {
  cam_synx_obj_driver_deinit();
  assert(!session && !atomic_load(&allocations) && !atomic_load(&client.refs));
  next_handle = 0;
}
static u32 create_handle(int32_t *idx) {
  u32 h = 0;
  assert(!cam_synx_obj_create("camera", 0, &h, idx));
  return h;
}
static void register_row(int32_t idx) {
  int32_t id = 1;
  assert(!cam_synx_obj_register_cb(&id, idx, sync_sink));
}
static void remote_signal(u32 h, int status) {
  assert(objects[h].subscribed);
  assert(!synx_signal(session, h, status));
}
static void external_irq(u32 h, int status) {
  struct dma_fence *f = objects[h].fence;
  pthread_mutex_lock(f->lock);
  f->error = status == SYNX_STATE_SIGNALED_SUCCESS ? 0 : -status;
  assert(!dma_fence_signal_locked(f));
  pthread_mutex_unlock(f->lock);
  /* Simulate the provider's core-status update still queued behind this IRQ. */
  assert(objects[h].status == SYNX_STATE_ACTIVE);
}
static void init_failures(void) {
  atomic_store(&fail_allocation, 1);
  assert(cam_synx_obj_driver_init() == -ENOMEM);
  assert(!g_cam_synx_obj_dev && !atomic_load(&allocations));
  fail_queue = true;
  assert(cam_synx_obj_driver_init() == -ENOMEM);
  fail_queue = false;
  assert(!g_cam_synx_obj_dev && !atomic_load(&allocations));
  init_error = -EAGAIN;
  assert(cam_synx_obj_driver_init() == -EAGAIN);
  init_error = 0;
  assert(!g_cam_synx_obj_dev && !atomic_load(&allocations));
  init_null = true;
  assert(cam_synx_obj_driver_init() == -ENODEV);
  init_null = false;
  assert(!g_cam_synx_obj_dev && !atomic_load(&allocations));
}
static void registration_failures(void) {
  for (int mode = 0; mode < 5; mode++) {
    fixture();
    int32_t idx, id = 1;
    u32 h = create_handle(&idx);
    int before = atomic_load(&allocations);
    if (mode == 0)
      atomic_store(&fail_allocation, 1);
    if (mode == 1)
      get_fence_error = -EIO;
    if (mode == 2)
      get_fence_null = true;
    if (mode == 3)
      wait_error = -EIO;
    if (mode == 4)
      add_error = -EINVAL;
    assert(cam_synx_obj_register_cb(&id, idx, sync_sink) < 0);
    atomic_store(&fail_allocation, 0);
    get_fence_error = wait_error = add_error = 0;
    get_fence_null = false;
    assert(atomic_load(&allocations) == before);
    assert(kref_read(&objects[h].fence->refcount) == 1 &&
           !objects[h].fence->callbacks);
    assert(!g_cam_synx_obj_dev->rows[idx].cb_registered_for_sync);
    register_row(idx);
    assert(objects[h].subscribed &&
           kref_read(&objects[h].fence->refcount) == 2);
    remote_signal(h, SYNX_STATE_SIGNALED_SUCCESS);
    flush_workqueue(g_cam_synx_obj_dev->work_queue);
    assert(atomic_load(&notifications) == 1 &&
           atomic_load(&observed_status) == CAM_SYNC_STATE_SIGNALED_SUCCESS);
    cleanup();
  }
}
static void signal_cases(void) {
  int statuses[] = {SYNX_STATE_SIGNALED_SUCCESS, SYNX_STATE_SIGNALED_CANCEL,
                    SYNX_STATE_SIGNALED_ERROR, 88};
  for (int mode = 0; mode < 4; mode++) {
    for (int state = 0; state < 4; state++) {
      fixture();
      int32_t idx;
      u32 h = create_handle(&idx);
      if (mode == 0)
        objects[h].global_status = statuses[state];
      if (mode == 1)
        signal_during_add = true;
      register_row(idx);
      signal_during_add = false;
      assert(objects[h].subscribed);
      if (mode == 2)
        remote_signal(h, statuses[state]);
      if (mode == 3)
        external_irq(h, statuses[state]);
      flush_workqueue(g_cam_synx_obj_dev->work_queue);
      int actual = mode == 1 ? SYNX_STATE_SIGNALED_SUCCESS : statuses[state];
      int expected = actual == SYNX_STATE_SIGNALED_SUCCESS
                         ? CAM_SYNC_STATE_SIGNALED_SUCCESS
                     : actual == SYNX_STATE_SIGNALED_CANCEL
                         ? CAM_SYNC_STATE_SIGNALED_CANCEL
                         : CAM_SYNC_STATE_SIGNALED_ERROR;
      assert(atomic_load(&notifications) == 1 &&
             atomic_load(&observed_status) == expected);
      cleanup();
    }
  }
}
static void external_release(void) {
  fixture();
  struct dma_fence *f = new_backing();
  u32 h;
  int32_t idx;
  assert(!cam_synx_obj_import_dma_fence("external", 0, f, &h, &idx));
  register_row(idx);
  assert(kref_read(&f->refcount) == 3);
  assert(!__cam_synx_obj_release(idx));
  assert(!f->callbacks && !f->signaled && kref_read(&f->refcount) == 1);
  pthread_mutex_lock(f->lock);
  dma_fence_signal_locked(f);
  pthread_mutex_unlock(f->lock);
  flush_workqueue(g_cam_synx_obj_dev->work_queue);
  assert(!atomic_load(&notifications));
  dma_fence_put(f);
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
    cam_synx_obj_quiesce();
  else
    assert(!__cam_synx_obj_release(j->idx));
  atomic_store(&j->done, true);
  return NULL;
}
static void running_callback_drain(void) {
  for (int repeat = 0; repeat < 100; repeat++) {
    fixture();
    int32_t idx;
    u32 h = create_handle(&idx);
    sink_block = true;
    register_row(idx);
    remote_signal(h, SYNX_STATE_SIGNALED_SUCCESS);
    pthread_mutex_lock(&sink_lock);
    while (!sink_entered)
      pthread_cond_wait(&sink_changed, &sink_lock);
    pthread_mutex_unlock(&sink_lock);
    struct release_job j = {.idx = idx, .quiesce = repeat >= 50};
    pthread_t thread;
    pthread_create(&thread, NULL, release_thread, &j);
    for (int n = 0; n < 2000 && !atomic_load(&j.entered); n++)
      usleep(1000);
    assert(atomic_load(&j.entered));
    usleep(1000);
    assert(!atomic_load(&j.done) && objects[h].live);
    assert(test_bit(idx, g_cam_synx_obj_dev->bitmap));
    pthread_mutex_lock(&sink_lock);
    sink_leave = true;
    pthread_cond_broadcast(&sink_changed);
    pthread_mutex_unlock(&sink_lock);
    pthread_join(thread, NULL);
    assert(atomic_load(&j.done));
    if (j.quiesce) {
      assert(objects[h].live && !g_cam_synx_obj_dev->rows[idx].callback);
      assert(!__cam_synx_obj_release(idx));
    }
    assert(!objects[h].live);
    int32_t new_idx;
    create_handle(&new_idx);
    assert(new_idx == idx);
    cleanup();
  }
}
static void queued_and_internal(void) {
  for (int mode = 0; mode < 2; mode++) {
    fixture();
    int32_t idx;
    u32 h = create_handle(&idx);
    struct workqueue_struct *q = g_cam_synx_obj_dev->work_queue;
    pthread_mutex_lock(&q->lock);
    q->pause = true;
    pthread_mutex_unlock(&q->lock);
    register_row(idx);
    struct cam_synx_obj_signal signal = {
        .synx_obj = h + 1, .status = CAM_SYNC_STATE_SIGNALED_SUCCESS};
    assert(!cam_synx_obj_internal_signal(idx, &signal));
    assert(!g_cam_synx_obj_dev->rows[idx].sync_signal_synx);
    signal.synx_obj = h;
    if (mode == 0) {
      assert(!cam_synx_obj_internal_signal(idx, &signal));
      assert(!__cam_synx_obj_release(idx));
    } else {
      remote_signal(h, SYNX_STATE_SIGNALED_SUCCESS);
      atomic_store(&generations[1], 43);
    }
    pthread_mutex_lock(&q->lock);
    q->pause = false;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->lock);
    flush_workqueue(q);
    assert(!atomic_load(&notifications));
    assert(atomic_load(&stale) == mode);
    cleanup();
  }
}
int main(void) {
  init_failures();
  registration_failures();
  signal_cases();
  external_release();
  running_callback_drain();
  queued_and_internal();
  puts("{\"init_failures\":4,\"registration_failures\":5,\"signal_cases\":16,"
       "\"external_release\":1,\"running_callback_release_races\":50,\"running_"
       "callback_quiesce_races\":50,\"queued_internal_generation\":2}");
  return 0;
}
