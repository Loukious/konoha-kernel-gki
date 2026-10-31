#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef atomic_llong atomic64_t;
#define atomic64_inc_return(p) (atomic_fetch_add((p), 1) + 1)
#define atomic64_set(p, v) atomic_store((p), (v))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define O_CLOEXEC 1
#define WQ_HIGHPRI 1
#define WQ_UNBOUND 2
#define WQ_MEM_RECLAIM 4
#define THIS_MODULE ((void *)1)
#define CAM_GENERIC_MONITOR_TABLE_ENTRY_SZ 16
#define CAM_GENERIC_FENCE_TYPE_DMA_FENCE 0
#define CAM_GENERIC_FENCE_TYPE_DMA_FENCE_DUMP 1
#define CAM_DMA_FENCE 0
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define DECLARE_BITMAP(n, bits) unsigned long n[((bits) + 63) / 64]
#define CAM_MEM_ZALLOC(n, f) kzalloc(n, f)
#define CAM_MEM_FREE(p) kfree(p)

static void log_event(int unused, const char *fmt, ...) {
  (void)unused;
  (void)fmt;
}
#define CAM_DBG(...) log_event(__VA_ARGS__)
#define CAM_WARN(...) log_event(__VA_ARGS__)
#define CAM_ERR(...) log_event(__VA_ARGS__)
#define CAM_INFO(...) log_event(__VA_ARGS__)

static atomic_int allocations, fail_allocation, module_refs, live_queues;
static bool fail_module, fail_queue, fail_fd, fail_sync_file;
static _Thread_local int provider_irq;
static void *kzalloc(size_t n, int flags) {
  (void)flags;
  int fail = atomic_load(&fail_allocation);
  if (fail && atomic_fetch_sub(&fail_allocation, 1) == 1)
    return NULL;
  void *p = calloc(1, n);
  assert(p);
  atomic_fetch_add(&allocations, 1);
  return p;
}
static void kfree(void *p) {
  if (p) {
    assert(atomic_fetch_sub(&allocations, 1) > 0);
    free(p);
  }
}
static bool try_module_get(void *p) {
  (void)p;
  if (fail_module)
    return false;
  atomic_fetch_add(&module_refs, 1);
  return true;
}
static void module_put(void *p) {
  (void)p;
  assert(atomic_fetch_sub(&module_refs, 1) > 0);
}
static size_t strscpy(char *out, const char *in, size_t size) {
  size_t n = strlen(in);
  if (n >= size)
    n = size - 1;
  memcpy(out, in, n);
  out[n] = 0;
  return n;
}
typedef pthread_mutex_t spinlock_t;
struct mutex {
  pthread_mutex_t lock;
};
static void spin_lock_init(spinlock_t *p) {
  assert(!pthread_mutex_init(p, NULL));
}
static void spin_lock_bh(spinlock_t *p) {
  /* The production IRQ callback must not acquire a camera row lock. */
  assert(!provider_irq);
  assert(!pthread_mutex_lock(p));
}
static void spin_unlock_bh(spinlock_t *p) { assert(!pthread_mutex_unlock(p)); }
static void mutex_init(struct mutex *p) {
  assert(!pthread_mutex_init(&p->lock, NULL));
}
static void mutex_lock(struct mutex *p) {
  assert(!provider_irq);
  assert(!pthread_mutex_lock(&p->lock));
}
static void mutex_unlock(struct mutex *p) {
  assert(!pthread_mutex_unlock(&p->lock));
}
static void mutex_destroy(struct mutex *p) {
  assert(!pthread_mutex_destroy(&p->lock));
}
static bool test_bit(unsigned int bit, const unsigned long *bits) {
  return (__atomic_load_n(&bits[bit / 64], __ATOMIC_SEQ_CST) >> (bit % 64)) & 1;
}
static bool test_and_set_bit(unsigned int bit, unsigned long *bits) {
  return (__atomic_fetch_or(&bits[bit / 64], 1UL << (bit % 64),
                            __ATOMIC_SEQ_CST) >>
          (bit % 64)) &
         1;
}
static void set_bit(unsigned int bit, unsigned long *bits) {
  test_and_set_bit(bit, bits);
}
static void clear_bit(unsigned int bit, unsigned long *bits) {
  __atomic_fetch_and(&bits[bit / 64], ~(1UL << (bit % 64)), __ATOMIC_SEQ_CST);
}
static unsigned int find_first_zero_bit(unsigned long *bits,
                                        unsigned int limit) {
  for (unsigned int n = 0; n < limit; n++)
    if (!test_bit(n, bits))
      return n;
  return limit;
}
static void bitmap_zero(unsigned long *bits, unsigned int limit) {
  memset(bits, 0, (limit + 63) / 64 * sizeof(*bits));
}

struct workqueue_struct;
struct work_struct {
  void (*fn)(struct work_struct *);
  struct work_struct *next;
  struct workqueue_struct *queue;
  int state;
};
struct workqueue_struct {
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t changed;
  struct work_struct *head, *tail;
  int active;
  bool stop, pause;
};
static void *worker_loop(void *opaque) {
  struct workqueue_struct *q = opaque;
  assert(!pthread_mutex_lock(&q->lock));
  for (;;) {
    while ((!q->head || q->pause) && !q->stop)
      pthread_cond_wait(&q->changed, &q->lock);
    if (q->stop)
      break;
    struct work_struct *w = q->head;
    q->head = w->next;
    if (!q->head)
      q->tail = NULL;
    w->next = NULL;
    w->state = 2;
    q->active++;
    pthread_mutex_unlock(&q->lock);
    w->fn(w);
    pthread_mutex_lock(&q->lock);
    w->state = 0;
    q->active--;
    pthread_cond_broadcast(&q->changed);
  }
  pthread_mutex_unlock(&q->lock);
  return NULL;
}
static struct workqueue_struct *alloc_workqueue(const char *name, int flags,
                                                int max_active) {
  (void)name;
  (void)flags;
  (void)max_active;
  if (fail_queue)
    return NULL;
  struct workqueue_struct *q = calloc(1, sizeof(*q));
  assert(q);
  atomic_fetch_add(&live_queues, 1);
  pthread_mutex_init(&q->lock, NULL);
  pthread_cond_init(&q->changed, NULL);
  assert(!pthread_create(&q->thread, NULL, worker_loop, q));
  return q;
}
static void INIT_WORK(struct work_struct *w, void (*fn)(struct work_struct *)) {
  w->fn = fn;
}
static bool queue_work(struct workqueue_struct *q, struct work_struct *w) {
  pthread_mutex_lock(&q->lock);
  if (w->state) {
    pthread_mutex_unlock(&q->lock);
    return false;
  }
  w->queue = q;
  w->state = 1;
  if (q->tail)
    q->tail->next = w;
  else
    q->head = w;
  q->tail = w;
  pthread_cond_broadcast(&q->changed);
  pthread_mutex_unlock(&q->lock);
  return true;
}
static bool cancel_work_sync(struct work_struct *w) {
  struct workqueue_struct *q = w->queue;
  if (!q)
    return false;
  pthread_mutex_lock(&q->lock);
  bool pending = w->state == 1;
  if (pending) {
    struct work_struct **link = &q->head, *prev = NULL;
    while (*link != w) {
      prev = *link;
      link = &prev->next;
    }
    *link = w->next;
    if (q->tail == w)
      q->tail = prev;
    w->next = NULL;
    w->state = 0;
  }
  while (w->state == 2)
    pthread_cond_wait(&q->changed, &q->lock);
  pthread_mutex_unlock(&q->lock);
  return pending;
}
static void flush_workqueue(struct workqueue_struct *q) {
  pthread_mutex_lock(&q->lock);
  while (q->head || q->active)
    pthread_cond_wait(&q->changed, &q->lock);
  pthread_mutex_unlock(&q->lock);
}
static void destroy_workqueue(struct workqueue_struct *q) {
  flush_workqueue(q);
  pthread_mutex_lock(&q->lock);
  q->stop = true;
  pthread_cond_broadcast(&q->changed);
  pthread_mutex_unlock(&q->lock);
  pthread_join(q->thread, NULL);
  pthread_mutex_destroy(&q->lock);
  pthread_cond_destroy(&q->changed);
  assert(atomic_fetch_sub(&live_queues, 1) > 0);
  free(q);
}

struct dma_fence;
struct dma_fence_cb {
  void (*fn)(struct dma_fence *, struct dma_fence_cb *);
  struct dma_fence_cb *next;
  bool initialized, linked;
};
struct dma_fence_ops {
  bool (*enable_signaling)(struct dma_fence *);
  const char *(*get_driver_name)(struct dma_fence *);
  const char *(*get_timeline_name)(struct dma_fence *);
  void (*release)(struct dma_fence *);
};
struct dma_fence {
  const struct dma_fence_ops *ops;
  spinlock_t *lock;
  atomic_int refcount;
  u64 context, seqno;
  int error;
  bool signaled;
  struct dma_fence_cb *callbacks;
};
static int add_error;
static bool signal_during_add;
static u64 next_context;
static u64 dma_fence_context_alloc(int n) {
  next_context += n;
  return next_context;
}
static void dma_fence_init(struct dma_fence *f, const struct dma_fence_ops *ops,
                           spinlock_t *lock, u64 context, u64 seqno) {
  f->ops = ops;
  f->lock = lock;
  f->context = context;
  f->seqno = seqno;
  atomic_init(&f->refcount, 1);
}
static struct dma_fence *dma_fence_get(struct dma_fence *f) {
  if (f)
    assert(atomic_fetch_add(&f->refcount, 1) > 0);
  return f;
}
static void dma_fence_put(struct dma_fence *f) {
  if (!f)
    return;
  int refs = atomic_fetch_sub(&f->refcount, 1);
  assert(refs > 0);
  if (refs == 1) {
    assert(!f->callbacks);
    f->ops->release(f);
  }
}
static int kref_read(atomic_int *p) { return atomic_load(p); }
static void dma_fence_free(struct dma_fence *f) {
  pthread_mutex_destroy(f->lock);
  kfree(f);
}
static int dma_fence_get_status_locked(struct dma_fence *f) {
  return f->signaled ? (f->error ?: 1) : 0;
}
static int dma_fence_get_status(struct dma_fence *f) {
  pthread_mutex_lock(f->lock);
  int status = dma_fence_get_status_locked(f);
  pthread_mutex_unlock(f->lock);
  return status;
}
static bool dma_fence_is_signaled_locked(struct dma_fence *f) {
  return f->signaled;
}
static void dma_fence_set_error(struct dma_fence *f, int status) {
  f->error = status;
}
static int dma_fence_signal_locked(struct dma_fence *f) {
  if (f->signaled)
    return -EINVAL;
  f->signaled = true;
  while (f->callbacks) {
    struct dma_fence_cb *cb = f->callbacks;
    f->callbacks = cb->next;
    cb->next = NULL;
    cb->linked = false;
    provider_irq++;
    cb->fn(f, cb);
    provider_irq--;
  }
  return 0;
}
static int dma_fence_add_callback(struct dma_fence *f, struct dma_fence_cb *cb,
                                  void (*fn)(struct dma_fence *,
                                             struct dma_fence_cb *)) {
  cb->initialized = true;
  if (add_error)
    return add_error;
  pthread_mutex_lock(f->lock);
  int rc = 0;
  if (f->signaled) {
    rc = -ENOENT;
  } else {
    cb->fn = fn;
    cb->linked = true;
    cb->next = f->callbacks;
    f->callbacks = cb;
    if (signal_during_add)
      dma_fence_signal_locked(f);
  }
  pthread_mutex_unlock(f->lock);
  return rc;
}
static bool dma_fence_remove_callback(struct dma_fence *f,
                                      struct dma_fence_cb *cb) {
  assert(cb->initialized);
  pthread_mutex_lock(f->lock);
  bool linked = cb->linked;
  if (linked) {
    struct dma_fence_cb **link = &f->callbacks;
    while (*link != cb)
      link = &(*link)->next;
    *link = cb->next;
    cb->next = NULL;
    cb->linked = false;
  }
  pthread_mutex_unlock(f->lock);
  return linked;
}

struct file {
  int unused;
  void *private_data;
  struct video_device *vdev;
};
struct sync_file {
  struct file *file;
  struct file storage;
  struct dma_fence *fence;
};
static struct sync_file *fds[64];
static bool reserved[64];
static int get_unused_fd_flags(int flags) {
  (void)flags;
  if (fail_fd)
    return -EMFILE;
  for (int n = 10; n < 64; n++)
    if (!reserved[n]) {
      reserved[n] = true;
      return n;
    }
  return -EMFILE;
}
static void put_unused_fd(int fd) {
  assert(reserved[fd]);
  reserved[fd] = false;
}
static struct sync_file *sync_file_create(struct dma_fence *f) {
  if (fail_sync_file)
    return NULL;
  struct sync_file *s = kzalloc(sizeof(*s), GFP_KERNEL);
  s->file = &s->storage;
  s->fence = dma_fence_get(f);
  return s;
}
static void fd_install(int fd, struct file *file) {
  assert(reserved[fd] && !fds[fd]);
  fds[fd] = container_of(file, struct sync_file, storage);
}
static struct dma_fence *sync_file_get_fence(int fd) {
  return fd >= 0 && fd < 64 && fds[fd] ? dma_fence_get(fds[fd]->fence) : NULL;
}
static void close_fd(int fd) {
  assert(fds[fd]);
  dma_fence_put(fds[fd]->fence);
  kfree(fds[fd]);
  fds[fd] = NULL;
  reserved[fd] = false;
}

enum cam_fence_op {
  CAM_FENCE_OP_CREATE,
  CAM_FENCE_OP_REGISTER_CB,
  CAM_FENCE_OP_UNREGISTER_ON_SIGNAL,
  CAM_FENCE_OP_SIGNAL,
  CAM_FENCE_OP_DESTROY
};
struct cam_generic_fence_monitor_data {
  int unused;
};
static unsigned long cam_sync_monitor_mask;
static atomic_int monitor_events;
static void cam_generic_fence_update_monitor_array(
    int idx, struct mutex *lock, struct cam_generic_fence_monitor_data **data,
    enum cam_fence_op op) {
  (void)idx;
  (void)lock;
  (void)data;
  (void)op;
  assert(!provider_irq);
  atomic_fetch_add(&monitor_events, 1);
}
static void __cam_dma_fence_dump_monitor_array(int idx) { (void)idx; }
static void __cam_dma_fence_save_previous_monitor_data(int idx) { (void)idx; }
struct cam_dma_fence_signal {
  int32_t dma_fence_fd, status;
};
static atomic_ullong generations[32];
static u64 cam_sync_get_generation(int32_t id) {
  return id > 0 && id < 32 ? atomic_load(&generations[id]) : 0;
}
