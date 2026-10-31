#include "main_camera_fence_mock.h"
#define CAM_SYNC 1
#define CAM_SYNC_MAX_OBJS 8
#define CAM_SYNC_MONITOR_TABLE_SIZE 2
#define CAM_GENERIC_FENCE_TYPE_SYNC_OBJ 4
#define CAM_SYNC_STATE_INVALID 0
#define CAM_SYNC_STATE_ACTIVE 1
#define CAM_SYNC_STATE_SIGNALED_ERROR 3
#define CAM_SYNC_COMMON_RELEASE_EVENT 1
#define CAM_SYNC_NAME "cam-sync"
#define CAM_SYNC_WORKQUEUE_NAME "cam-sync-test"
#define V4L2_CAP_VIDEO_CAPTURE 1
#define VFL_TYPE_VIDEO 0
#define IS_ENABLED(x) 1
#define IS_REACHABLE(x) 0
#define CAM_GET_TIMESTAMP(ts) do { (ts).tv_sec = 0; (ts).tv_nsec = 0; } while (0)
#define CAM_GET_TIMESTAMP_DIFF_IN_MICRO(a, b, out) do { (void)(a); (void)(b); (out) = 0; } while (0)
struct timespec64 { long tv_sec, tv_nsec; };
struct completion { bool done; };
struct device { int unused; };
struct platform_device { struct device dev; const char *name; };
struct v4l2_device { void (*release)(struct v4l2_device *); int refs; bool registered; };
struct video_device {
    struct v4l2_device *v4l2_dev;
    void (*release)(struct video_device *);
    void *fops, *ioctl_ops, *drvdata;
    int minor, device_caps, vfl_type;
    char name[64];
    bool published;
};
struct sync_table_row { int state; };
struct sync_device {
    struct completion video_released;
    struct video_device *vdev;
    struct v4l2_device v4l2_dev;
    struct sync_table_row *sync_table;
    spinlock_t row_spinlocks[CAM_SYNC_MAX_OBJS], cam_sync_eventq_lock;
    struct mutex table_lock, file_lock;
    int open_cnt;
    void *dentry, *cam_sync_eventq;
    struct workqueue_struct *work_queue;
    DECLARE_BITMAP(bitmap, CAM_SYNC_MAX_OBJS);
    struct cam_generic_fence_monitor_data **mon_data;
};
static struct sync_device *sync_dev;
static int cam_sync_v4l2_fops, g_cam_sync_ioctl_ops;
static bool trigger_cb_without_switch;
static int fault, media_live, v4l2_live, videos_live, dma_live, synx_live, node_refs;
static bool nodes_ready, fail_fh, dma_quiesced, synx_quiesced, dma_opened, synx_opened;
static atomic_int callback_done;
static int callbacks_expected, rows_destroyed;
static struct work_struct close_callbacks[CAM_SYNC_MAX_OBJS];

static void *vzalloc(size_t n) { return kzalloc(n, GFP_KERNEL); }
static void vfree(void *p) { kfree(p); }
static struct platform_device *to_platform_device(struct device *d) { return container_of(d, struct platform_device, dev); }
static void init_completion(struct completion *c) { c->done = false; }
static void complete(struct completion *c) { assert(!c->done); c->done = true; }
static void wait_for_completion(struct completion *c) { assert(c->done); }
static struct video_device *video_device_alloc(void)
{
    struct video_device *v = kzalloc(sizeof(*v), GFP_KERNEL);
    if (v) videos_live++;
    return v;
}
static void video_device_release_empty(struct video_device *v) { (void)v; }
static void video_device_release(struct video_device *v)
{
    assert(v && !v->published && videos_live == 1);
    videos_live--; kfree(v);
}
static int cam_sync_media_controller_init(struct sync_device *s, struct platform_device *p)
{
    (void)s; (void)p;
    if (fault == 1) return -EIO;
    assert(!media_live); media_live = 1; return 0;
}
static void cam_sync_media_controller_cleanup(struct sync_device *s) { (void)s; assert(media_live == 1); media_live = 0; }
static int v4l2_device_register(struct device *d, struct v4l2_device *v)
{
    (void)d;
    if (fault == 2) return -EIO;
    assert(media_live && !v4l2_live); v4l2_live = 1;
    v->refs = 1; v->registered = true; return 0;
}
static void v4l2_device_put(struct v4l2_device *v)
{
    assert(v->refs > 0);
    if (!--v->refs) v->release(v);
}
static void v4l2_device_unregister(struct v4l2_device *v)
{
    assert(v->registered && !v->refs && v4l2_live == 1);
    v->registered = false; v4l2_live = 0;
}
static void video_set_drvdata(struct video_device *v, void *s) { v->drvdata = s; }
static int video_register_device(struct video_device *v, int type, int nr)
{
    (void)type; (void)nr;
    assert(v->drvdata == sync_dev && sync_dev->work_queue && dma_live && synx_live);
    assert(test_bit(0, sync_dev->bitmap) && !v->published);
    assert(!trigger_cb_without_switch);
    if (fault == 5) return -EIO;
    v->published = true; v->v4l2_dev->refs++; return 0;
}
static void video_unregister_device(struct video_device *v)
{
    assert(v->published && !sync_dev->open_cnt && !node_refs);
    v->published = false;
    v->release(v);
    v4l2_device_put(v->v4l2_dev);
}
static void cam_sync_init_entity(struct sync_device *s) { assert(s->vdev->published); }
static void cam_record_bind_latency(const char *name, long us) { (void)name; (void)us; }
static void cam_sync_create_debugfs(void) { sync_dev->dentry = kzalloc(16, GFP_KERNEL); }
static void debugfs_remove_recursive(void *p) { kfree(p); }
static int cam_dma_fence_driver_init(void)
{
    if (fault == 3) return -EIO;
    assert(!dma_live); dma_live = 1; return 0;
}
static int cam_synx_obj_driver_init(void)
{
    if (fault == 4) return -EIO;
    assert(dma_live && !synx_live); synx_live = 1; return 0;
}
static void cam_dma_fence_driver_deinit(void) { assert(dma_live); dma_live = 0; }
static void cam_synx_obj_driver_deinit(void) { assert(synx_live); synx_live = 0; }
static void cam_dma_fence_open(void) { assert(dma_live && !dma_opened); dma_opened = true; dma_quiesced = false; }
static void cam_synx_obj_open(void) { assert(synx_live && !synx_opened); synx_opened = true; synx_quiesced = false; }
static void cam_dma_fence_quiesce(void) { assert(dma_opened); dma_quiesced = true; }
static void cam_synx_obj_quiesce(void) { assert(synx_opened); synx_quiesced = true; }
static void cam_dma_fence_close(void) { assert(dma_quiesced && !sync_dev->mon_data); dma_opened = false; }
static void cam_synx_obj_close(void) { assert(synx_quiesced && !sync_dev->mon_data); synx_opened = false; }
static int cam_req_mgr_get_node(void) { if (!nodes_ready) return -ENODEV; node_refs++; return 0; }
static void cam_req_mgr_put_node(void) { assert(node_refs > 0); node_refs--; }
static struct sync_device *video_drvdata(struct file *f) { return f->vdev ? f->vdev->drvdata : NULL; }
static int v4l2_fh_open(struct file *f)
{
    if (fail_fh) return -EIO;
    f->private_data = kzalloc(16, GFP_KERNEL); return 0;
}
static void v4l2_fh_release(struct file *f)
{
    assert(!sync_dev->cam_sync_eventq && f->private_data && !dma_opened && !synx_opened);
    kfree(f->private_data); f->private_data = NULL;
}
static void close_callback(struct work_struct *w)
{
    (void)w;
    /* A real callback may use sync table APIs while close waits for it. */
    mutex_lock(&sync_dev->table_lock);
    atomic_fetch_add(&callback_done, 1);
    mutex_unlock(&sync_dev->table_lock);
}
static int cam_sync_signal(int i, int status, int cause)
{
    (void)status; (void)cause;
    assert(dma_quiesced && synx_quiesced && sync_dev->sync_table[i].state == CAM_SYNC_STATE_ACTIVE);
    sync_dev->sync_table[i].state = CAM_SYNC_STATE_SIGNALED_ERROR;
    INIT_WORK(&close_callbacks[i], close_callback);
    callbacks_expected++;
    assert(queue_work(sync_dev->work_queue, &close_callbacks[i]));
    return 0;
}
static int cam_sync_destroy(int i)
{
    assert(dma_quiesced && synx_quiesced && atomic_load(&callback_done) == callbacks_expected);
    assert(sync_dev->sync_table[i].state != CAM_SYNC_STATE_INVALID);
    sync_dev->sync_table[i].state = CAM_SYNC_STATE_INVALID;
    rows_destroyed++;
    return 0;
}
#include "sync_lifecycle.inc"

static struct platform_device pdev = {.name = "sync"};
static void empty_state(void)
{
    assert(!sync_dev && !media_live && !v4l2_live && !videos_live && !dma_live && !synx_live);
    assert(!node_refs && !atomic_load(&allocations) && !atomic_load(&live_queues));
}
static void bind_failures(void)
{
    for (int n = 1; n <= 3; n++) {
        atomic_store(&fail_allocation, n);
        assert(cam_sync_component_bind(&pdev.dev, NULL, NULL) == -ENOMEM);
        atomic_store(&fail_allocation, 0);
        empty_state();
    }
    for (fault = 1; fault <= 5; fault++) {
        assert(cam_sync_component_bind(&pdev.dev, NULL, NULL) == -EIO);
        empty_state();
    }
    fault = 0;
    fail_queue = true;
    assert(cam_sync_component_bind(&pdev.dev, NULL, NULL) == -ENOMEM);
    fail_queue = false;
    empty_state();
}
static void file_cases(void)
{
    assert(!cam_sync_component_bind(&pdev.dev, NULL, NULL));
    struct file f = {.vdev = sync_dev->vdev}, second = {.vdev = sync_dev->vdev};
    assert(cam_sync_open(&f) == -ENODEV && !node_refs);
    nodes_ready = true;
    fail_fh = true;
    assert(cam_sync_open(&f) == -EIO && !node_refs && !sync_dev->open_cnt);
    fail_fh = false;
    cam_sync_monitor_mask = 1UL << CAM_GENERIC_FENCE_TYPE_SYNC_OBJ;
    assert(!cam_sync_open(&f));
    assert(node_refs == 1 && sync_dev->open_cnt == 1 && sync_dev->cam_sync_eventq == f.private_data);
    assert(cam_sync_open(&second) == -EALREADY && node_refs == 1);
    assert(sync_dev->mon_data);
    for (int i = 0; i < CAM_SYNC_MONITOR_TABLE_SIZE; i++)
        sync_dev->mon_data[i] = kzalloc(sizeof(**sync_dev->mon_data), GFP_KERNEL);
    for (int i = 1; i < CAM_SYNC_MAX_OBJS; i++) sync_dev->sync_table[i].state = CAM_SYNC_STATE_ACTIVE;
    callbacks_expected = rows_destroyed = 0; atomic_store(&callback_done, 0);
    assert(!cam_sync_close(&f));
    assert(!node_refs && !sync_dev->open_cnt && !sync_dev->mon_data);
    assert(rows_destroyed == CAM_SYNC_MAX_OBJS - 1 && callbacks_expected == rows_destroyed);
    assert(!cam_sync_open(&f));
    assert(!cam_sync_close(&f));
    nodes_ready = false;
    cam_sync_component_unbind(&pdev.dev, NULL, NULL);
    empty_state();
}
int main(void)
{
    bind_failures(); file_cases();
    assert(!cam_sync_component_bind(&pdev.dev, NULL, NULL));
    cam_sync_component_unbind(&pdev.dev, NULL, NULL); empty_state();
    puts("{\"bind_failures\":9,\"file_admission_cases\":4,\"close_with_live_callbacks\":1,\"reopen\":1,\"never_opened_unbind\":1}");
    return 0;
}
