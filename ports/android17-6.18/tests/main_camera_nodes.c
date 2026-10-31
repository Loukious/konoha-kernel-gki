#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sched.h>

struct mutex { pthread_mutex_t lock; };
#define DEFINE_MUTEX(n) struct mutex n = { PTHREAD_MUTEX_INITIALIZER }
#define mutex_lock(m) assert(!pthread_mutex_lock(&(m)->lock))
#define mutex_unlock(m) assert(!pthread_mutex_unlock(&(m)->lock))
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_SEQ_CST)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x), (v), __ATOMIC_SEQ_CST)
#define WARN_ON(x) (assert(!(x)), 0)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
struct completion { pthread_mutex_t lock; pthread_cond_t cond; bool done; };
static struct completion cam_v4l2_released = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, false};
static pthread_mutex_t wait_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_cond = PTHREAD_COND_INITIALIZER;
static int cam_node_wait, cam_req_mgr_ordered_sd_list;
#define wait_event(q,c) do { (void)(q); pthread_mutex_lock(&wait_lock); \
	while (!(c)) pthread_cond_wait(&wait_cond, &wait_lock); \
	pthread_mutex_unlock(&wait_lock); } while (0)
static void wake_up_all(int *q)
{
	(void)q;
	pthread_mutex_lock(&wait_lock);
	pthread_cond_broadcast(&wait_cond);
	pthread_mutex_unlock(&wait_lock);
}
static void complete(struct completion *c)
{
	pthread_mutex_lock(&c->lock); c->done = true;
	pthread_cond_broadcast(&c->cond); pthread_mutex_unlock(&c->lock);
}
static void wait_for_completion(struct completion *c)
{
	pthread_mutex_lock(&c->lock);
	while (!c->done) pthread_cond_wait(&c->cond, &c->lock);
	pthread_mutex_unlock(&c->lock);
}

struct v4l2_device { atomic_int refs; } parent;
struct video_device { bool registered; bool tail; bool already_withdrawn; } root_video, sub_video;
struct v4l2_subdev { struct video_device *devnode; };
struct v4l2_subdev_fh { int unused; };
struct v4l2_subdev_internal_ops {
	int (*open)(struct v4l2_subdev *, struct v4l2_subdev_fh *);
	int (*close)(struct v4l2_subdev *, struct v4l2_subdev_fh *);
};
struct cam_subdev { struct v4l2_subdev sd; const struct v4l2_subdev_internal_ops *internal_ops; } child;
static struct { struct video_device *video; struct v4l2_device *v4l2_dev; } g_dev;
#define list_for_each_entry(p,h,m) for ((void)(h), p = &child; p; p = NULL)
static DEFINE_MUTEX(cam_node_lock);
static unsigned int cam_node_users;
static bool cam_nodes_ready, cam_v4l2_ref_held, cam_subdev_nodes_live;
static atomic_int unregisters;
static atomic_bool drain_done, tail_done, child_alive;
static int native_open_rc;
static void cam_v4l2_device_released(struct v4l2_device *dev);
static void v4l2_device_put(struct v4l2_device *dev)
{
	assert(dev == &parent);
	int old = atomic_fetch_sub(&dev->refs, 1);
	assert(old > 0);
	if (old == 1) cam_v4l2_device_released(dev);
}
static void video_unregister_device(struct video_device *dev)
{
	assert(!dev->already_withdrawn);
	if (!dev->registered) return;
	dev->registered = false;
	atomic_fetch_add(&unregisters, 1);
	if (!dev->tail) {
		if (dev == &sub_video) child.sd.devnode = NULL;
		v4l2_device_put(&parent);
	}
}
static int native_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	(void)sd; (void)fh;
	assert(READ_ONCE(cam_node_users) == 1);
	return native_open_rc;
}
static int native_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	(void)sd; (void)fh;
	assert(READ_ONCE(cam_node_users) == 1);
	assert(atomic_load(&child_alive));
	return 0;
}
static const struct v4l2_subdev_internal_ops native_ops = {native_open, native_close};
#include "nodes.inc"

static void setup(bool tail)
{
	assert(!READ_ONCE(cam_node_users));
	cam_nodes_ready = false;
	cam_v4l2_ref_held = true;
	cam_subdev_nodes_live = true;
	cam_v4l2_released.done = false;
	atomic_store(&parent.refs, 3);
	root_video = (struct video_device){true, false, false};
	sub_video = (struct video_device){true, tail, false};
	child.sd.devnode = &sub_video;
	child.internal_ops = &native_ops;
	g_dev.video = &root_video; g_dev.v4l2_dev = &parent;
	atomic_store(&unregisters, 0); atomic_store(&drain_done, false);
	atomic_store(&tail_done, !tail); atomic_store(&child_alive, true);
}
static void *drain(void *arg)
{
	(void)arg;
	cam_req_mgr_drain_nodes();
	assert(atomic_load(&parent.refs) == 0);
	assert(atomic_load(&tail_done));
	atomic_store(&child_alive, false);
	atomic_store(&drain_done, true);
	return NULL;
}
static void settle_gate(void)
{
	for (unsigned int i = 0; i < 10000000; i++) {
		mutex_lock(&cam_node_lock);
		bool ready = cam_nodes_ready;
		mutex_unlock(&cam_node_lock);
		if (!ready) return;
		sched_yield();
	}
	assert(0);
}
static void settle_unregisters(void)
{
	for (unsigned int i = 0; i < 10000000; i++) {
		if (atomic_load(&unregisters) == 2) return;
		sched_yield();
	}
	assert(0);
}
static void finish_tail(void)
{
	/* Native V4L2 writes sd->devnode after its internal release hook. */
	assert(atomic_load(&child_alive));
	child.sd.devnode = NULL;
	atomic_store(&tail_done, true);
	v4l2_device_put(&parent);
}
int main(void)
{
	setup(false);
	assert(cam_subdev_open_tracked(&child.sd, NULL) == -ENODEV);
	assert(!READ_ONCE(cam_node_users));
	cam_nodes_ready = true;
	native_open_rc = -ENOMEM;
	assert(cam_subdev_open_tracked(&child.sd, NULL) == -ENOMEM);
	assert(!READ_ONCE(cam_node_users));
	native_open_rc = 2;
	assert(cam_subdev_open_tracked(&child.sd, NULL) == 2);
	assert(cam_subdev_close_tracked(&child.sd, NULL) == 0);
	cam_req_mgr_drain_nodes();
	for (unsigned int race = 0; race < 100; race++) {
		setup(true); cam_nodes_ready = true; native_open_rc = 0;
		assert(!cam_subdev_open_tracked(&child.sd, NULL));
		pthread_t thread; assert(!pthread_create(&thread, NULL, drain, NULL));
		settle_gate();
		assert(cam_req_mgr_get_node() == -ENODEV);
		assert(!atomic_load(&unregisters));
		assert(!atomic_load(&drain_done));
		assert(!cam_subdev_close_tracked(&child.sd, NULL));
		settle_unregisters();
		assert(!atomic_load(&drain_done));
		finish_tail();
		assert(!pthread_join(thread, NULL));
		assert(atomic_load(&drain_done));
	}
	/* Failed helper already withdrew subdevice nodes; don't touch them again. */
	setup(true); cam_subdev_nodes_live = false;
	sub_video.registered = false; sub_video.already_withdrawn = true;
	atomic_store(&unregisters, 1);
	pthread_t thread; assert(!pthread_create(&thread, NULL, drain, NULL));
	settle_unregisters();
	assert(!atomic_load(&drain_done));
	finish_tail(); assert(!pthread_join(thread, NULL));
	puts("{\"admission_cases\":3,\"file_and_vfs_tail_races\":100,\"helper_failure_tail\":1}");
	return 0;
}
