#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define EPROBE_DEFER 517
#define CAM_SUBDEV_NAME_SIZE 64
#define CAM_INFO(...) ((void)0)
#define CAM_ERR(...) ((void)0)
#define CAM_GET_TIMESTAMP(x) ((x).value = 0)
#define CAM_GET_TIMESTAMP_DIFF_IN_MICRO(a, b, c) ((c) = (b).value - (a).value)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
struct timespec64 {
  long value;
};
struct mutex {
  bool initialized, locked;
};
static void mutex_init(struct mutex *m) {
  assert(!m->initialized);
  m->initialized = true;
}
static void mutex_destroy(struct mutex *m) {
  assert(m->initialized && !m->locked);
  m->initialized = false;
}
static void mutex_lock(struct mutex *m) {
  assert(m->initialized && !m->locked);
  m->locked = true;
}
static void mutex_unlock(struct mutex *m) {
  assert(m->initialized && m->locked);
  m->locked = false;
}
#define spin_lock_init(s) (*(s) = 1)
struct list_head {
  struct list_head *next, *prev;
};
static void INIT_LIST_HEAD(struct list_head *h) { h->next = h->prev = h; }
static void list_del_init(struct list_head *n) {
  n->prev->next = n->next;
  n->next->prev = n->prev;
  INIT_LIST_HEAD(n);
}
#define list_for_each_entry(p, h, m)                                           \
  for (p = container_of((h)->next, __typeof__(*p), m); &p->m != (h);           \
       p = container_of(p->m.next, __typeof__(*p), m))
struct video_device {
  const char *name;
} root_video, videos[2];
struct v4l2_device {
  int unused;
} parent_v4l2;
struct v4l2_subdev_fh {
  int unused;
};
struct v4l2_subdev;
struct v4l2_subdev_internal_ops {
  int (*open)(struct v4l2_subdev *, struct v4l2_subdev_fh *);
  int (*close)(struct v4l2_subdev *, struct v4l2_subdev_fh *);
};
struct v4l2_subdev {
  struct v4l2_device *v4l2_dev;
  struct video_device *devnode;
  const struct v4l2_subdev_internal_ops *internal_ops;
  char name[64];
  unsigned int flags;
  void *token;
  struct {
    unsigned int num_pads, function;
    void *pads;
    const char *name;
  } entity;
};
struct cam_subdev {
  struct v4l2_subdev sd;
  const struct v4l2_subdev_internal_ops *internal_ops;
  struct v4l2_subdev_internal_ops tracked_ops;
  const char *name;
  void *ops, *token;
  unsigned int sd_flags, ent_function;
  struct list_head list;
  bool registered;
} children[2];
static struct {
  struct video_device *video;
  struct v4l2_device *v4l2_dev;
  int count, open_cnt, cam_eventq_lock;
  bool shutdown_state, state;
  void *cam_eventq;
  struct mutex cam_lock, dev_lock;
} g_dev;
static struct mutex cam_node_lock = {true, false};
static struct list_head cam_req_mgr_ordered_sd_list;
static bool cam_nodes_ready, cam_subdev_nodes_live;
static struct kmem_cache {
  int unused;
} timer_cache;
static struct kmem_cache *g_cam_req_mgr_timer_cachep;
struct device {
  int kobj;
} master;
static struct {
  int attr;
} camera_debug_sysfs_attr;
static int fail_step, current_step, fail_registration, registration_calls;
static int unregistration_calls, children_bound, drains;
static bool v4l2_ready, media_ready, video_ready, util_ready, core_ready;
static bool cache_ready, vmrm_ready, sysfs_ready, nodes_published, parent_ref;
static int cam_register_subdev(struct cam_subdev *csd);
static int cam_unregister_subdev(struct cam_subdev *csd);

static bool fail(void) {
  assert(!cam_nodes_ready);
  return ++current_step == fail_step;
}
static void list_add(struct list_head *n, struct list_head *h) {
  struct cam_subdev *csd = container_of(n, struct cam_subdev, list);
  assert(csd->sd.v4l2_dev && csd->registered);
  n->next = h->next;
  n->prev = h;
  h->next->prev = n;
  h->next = n;
}
static int cam_req_mgr_ordered_list_cmp(void *p, const struct list_head *a,
                                        const struct list_head *b) {
  (void)p;
  (void)a;
  (void)b;
  return 0;
}
static void list_sort(void *p, struct list_head *h,
                      int (*cmp)(void *, const struct list_head *,
                                 const struct list_head *)) {
  (void)p;
  (void)h;
  (void)cmp;
}
static int cam_subdev_open_tracked(struct v4l2_subdev *s,
                                   struct v4l2_subdev_fh *f) {
  (void)s;
  (void)f;
  return cam_nodes_ready ? 0 : -ENODEV;
}
static int cam_subdev_close_tracked(struct v4l2_subdev *s,
                                    struct v4l2_subdev_fh *f) {
  (void)s;
  (void)f;
  return 0;
}
static void v4l2_subdev_init(struct v4l2_subdev *s, void *ops) {
  (void)ops;
  memset(s, 0, sizeof(*s));
}
static void v4l2_set_subdevdata(struct v4l2_subdev *s, void *token) {
  s->token = token;
}
static int v4l2_device_register_subdev(struct v4l2_device *p,
                                       struct v4l2_subdev *s) {
  assert(p == &parent_v4l2 && v4l2_ready);
  assert(s->internal_ops->open == cam_subdev_open_tracked);
  assert(s->internal_ops->close == cam_subdev_close_tracked);
  if (++registration_calls == fail_registration)
    return -EIO;
  s->v4l2_dev = p;
  return 0;
}
static void v4l2_device_unregister_subdev(struct v4l2_subdev *s) {
  assert(s->v4l2_dev == &parent_v4l2 && !s->devnode);
  s->v4l2_dev = NULL;
  unregistration_calls++;
}
static int cam_v4l2_device_setup(struct device *d) {
  (void)d;
  if (fail())
    return -EIO;
  v4l2_ready = parent_ref = true;
  g_dev.v4l2_dev = &parent_v4l2;
  return 0;
}
static int cam_media_device_setup(struct device *d) {
  (void)d;
  assert(v4l2_ready);
  if (fail())
    return -EIO;
  media_ready = true;
  return 0;
}
static int cam_video_device_setup(void) {
  assert(media_ready);
  if (fail())
    return -EIO;
  video_ready = true;
  g_dev.video = &root_video;
  return 0;
}
static int cam_req_mgr_util_init(void) {
  assert(video_ready);
  if (fail())
    return -EIO;
  util_ready = true;
  return 0;
}
static int cam_req_mgr_core_device_init(void) {
  assert(util_ready);
  if (fail())
    return -EIO;
  core_ready = true;
  return 0;
}
static struct kmem_cache *create_cache(void) {
  assert(core_ready);
  if (fail())
    return NULL;
  cache_ready = true;
  return &timer_cache;
}
#define KMEM_CACHE(t, f) create_cache()
static void kmem_cache_destroy(struct kmem_cache *c) {
  assert(c == &timer_cache && cache_ready && !children_bound);
  cache_ready = false;
}
static int component_bind_all(struct device *d, void *data) {
  (void)d;
  (void)data;
  assert(cache_ready && g_dev.state);
  bool failed = fail();
  for (unsigned int i = 0; i < 2; i++) {
    children[i].name = i ? "camera1" : "camera0";
    int rc = cam_register_subdev(&children[i]);
    assert(!rc);
    children_bound++;
    assert(!children[i].sd.devnode);
    if (failed) {
      /* Actual component_bind_all unwinds its own partial successes. */
      assert(!cam_unregister_subdev(&children[i]));
      children_bound--;
      return -EIO;
    }
  }
  return 0;
}
static void component_unbind_all(struct device *d, void *data) {
  (void)d;
  (void)data;
  assert(children_bound == 2 && util_ready && core_ready && cache_ready);
  assert(!cam_nodes_ready && !nodes_published);
  for (unsigned int i = 0; i < 2; i++) {
    assert(!cam_unregister_subdev(&children[i]));
    children_bound--;
  }
}
static int cam_vmrm_populate_io_resource_info(void) {
  assert(children_bound == 2);
  return fail() ? -EIO : 0;
}
static int cam_vmrm_register_gh_callback(void) {
  if (fail())
    return -EIO;
  vmrm_ready = true;
  return 0;
}
static int cam_vmrm_unregister_gh_callback(void) {
  assert(vmrm_ready);
  vmrm_ready = false;
  return 0;
}
static int sysfs_create_file(int *k, int *a) {
  (void)k;
  (void)a;
  assert(vmrm_ready);
  if (fail())
    return -EIO;
  sysfs_ready = true;
  return 0;
}
static void sysfs_remove_file(int *k, int *a) {
  (void)k;
  (void)a;
  assert(sysfs_ready);
  sysfs_ready = false;
}
static int v4l2_device_register_subdev_nodes(struct v4l2_device *v) {
  assert(v == &parent_v4l2 && sysfs_ready && children_bound == 2);
  if (fail())
    return -EIO;
  nodes_published = true;
  for (unsigned int i = 0; i < 2; i++) {
    videos[i].name = children[i].name;
    children[i].sd.devnode = &videos[i];
  }
  return 0;
}
static const char *video_device_node_name(struct video_device *v) {
  return v->name;
}
static void cam_req_mgr_debug_record_bind_time(long us) { (void)us; }
static void cam_req_mgr_debug_bind_latency_cleanup(void) {}
static void cam_req_mgr_drain_nodes(void) {
  cam_nodes_ready = false;
  if (nodes_published) {
    assert(children_bound == 2 && core_ready && util_ready);
    for (unsigned int i = 0; i < 2; i++)
      children[i].sd.devnode = NULL;
  }
  nodes_published = cam_subdev_nodes_live = false;
  parent_ref = false;
  drains++;
}
static void cam_req_mgr_core_device_deinit(void) {
  assert(core_ready && !children_bound);
  core_ready = false;
}
static void cam_req_mgr_util_deinit(void) {
  assert(util_ready && !core_ready && !children_bound);
  util_ready = false;
}
static void cam_video_device_cleanup(void) {
  assert(video_ready && !children_bound && drains && !parent_ref);
  video_ready = false;
  g_dev.video = NULL;
}
static void cam_media_device_cleanup(void) {
  assert(media_ready && !video_ready);
  media_ready = false;
}
static void cam_v4l2_device_cleanup(void) {
  assert(v4l2_ready && !media_ready && !parent_ref && !g_dev.count);
  v4l2_ready = false;
  g_dev.v4l2_dev = NULL;
}

#include "master.inc"

static void clean(void) {
  assert(!v4l2_ready && !media_ready && !video_ready && !util_ready &&
         !core_ready);
  assert(!cache_ready && !vmrm_ready && !sysfs_ready && !children_bound &&
         !nodes_published);
  assert(!cam_nodes_ready && !parent_ref && !g_dev.count && !g_dev.state);
  assert(!g_dev.dev_lock.initialized && !g_dev.cam_lock.initialized);
  assert(cam_req_mgr_ordered_sd_list.next == &cam_req_mgr_ordered_sd_list);
}
int main(void) {
  for (int fail_at = 1; fail_at <= 11; fail_at++) {
    fail_step = fail_at;
    current_step = drains = 0;
    memset(children, 0, sizeof(children));
    registration_calls = 0;
    int rc = cam_req_mgr_component_master_bind(&master);
    assert(rc == (fail_at == 6 ? -ENOMEM : -EIO));
    clean();
  }
  fail_step = current_step = drains = 0;
  memset(children, 0, sizeof(children));
  registration_calls = 0;
  assert(!cam_req_mgr_component_master_bind(&master));
  assert(cam_nodes_ready && cam_subdev_nodes_live && g_dev.state &&
         g_dev.count == 2);
  assert(current_step == 11);
  cam_req_mgr_component_master_unbind(&master);
  clean();
  assert(cam_register_subdev(NULL) == -EPROBE_DEFER);
  g_dev.state = true;
  v4l2_ready = true;
  g_dev.v4l2_dev = &parent_v4l2;
  mutex_init(&g_dev.dev_lock);
  INIT_LIST_HEAD(&cam_req_mgr_ordered_sd_list);
  assert(cam_register_subdev(NULL) == -EINVAL);
  memset(children, 0, sizeof(children));
  children[0].name = "camera0";
  fail_registration = 1;
  registration_calls = unregistration_calls = 0;
  assert(cam_register_subdev(&children[0]) == -EIO);
  assert(g_dev.count == 0 && !children[0].registered);
  assert(cam_req_mgr_ordered_sd_list.next == &cam_req_mgr_ordered_sd_list);
  fail_registration = 0;
  assert(!cam_register_subdev(&children[0]));
  assert(cam_register_subdev(&children[0]) == -EALREADY);
  assert(g_dev.count == 1 && children[0].registered);
  assert(!cam_unregister_subdev(&children[0]));
  assert(!cam_unregister_subdev(&children[0]));
  assert(!g_dev.count && unregistration_calls == 1);
  assert(cam_req_mgr_ordered_sd_list.next == &cam_req_mgr_ordered_sd_list);
  puts("{\"master_setup_failures\":11,\"master_success_teardown\":1,"
       "\"subdevice_registration_cases\":7}");
  return 0;
}
