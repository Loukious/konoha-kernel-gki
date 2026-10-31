#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAM_JPEG_CTX_MAX 64
#define CAM_CTX_MAX 8
#define CAM_IFE_CTX_MAX 32
#define CAM_TFE_CTX_MAX 8
#define CAM_JPEG_DEV_NAME "jpeg"
#define CAM_ISP_DEV_NAME "isp"
#define CAM_JPEG_DEVICE_TYPE 1
#define CAM_IFE_DEVICE_TYPE 2
#define CAM_TFE_MC_DEVICE_TYPE 3
#define CAM_TFE_DEVICE_TYPE 4
#define CAM_SD_CLOSE_MEDIUM_PRIORITY 1
#define CAM_SD_CLOSE_HIGH_PRIORITY 0
#define CAM_COMMON_EVT_INJECT_HW_JPEG 2
#define CAM_COMMON_EVT_INJECT_HW_ISP 0
#define CAM_DRIVER_ID_ISP 1
#define CAM_JPEG_DEV_ENC 0
#define CAM_JPEG_DEV_DMA 1
#define CAM_JPEG_DEV_MAX 2
#define CAM_JPEG_NUM_DEV_PER_RES_MAX 1
#define CAM_JPEG_CMD_GET_NUM_PID 8
#define GFP_KERNEL 0
#define CAM_ERR(...) ((void)0)
#define CAM_DBG(...) ((void)0)
#define CAM_GET_TIMESTAMP(x) ((x).tick = 0)
#define CAM_GET_TIMESTAMP_DIFF_IN_MICRO(a, b, c) ((c) = (b).tick - (a).tick)
struct timespec64 { long tick; };
static int step, fail_at, live, node_refs, device_refs;
static bool failure(void) { return ++step == fail_at; }
static void *allocate(size_t n) {
    if (failure()) return NULL;
    void *p = calloc(1, n); assert(p); live++; return p;
}
static void drop(void *p) { if (p) { assert(live); live--; free(p); } }
#define CAM_MEM_ZALLOC(n, g) allocate(n)
#define CAM_MEM_ZALLOC_ARRAY(n, z, g) allocate((n) * (z))
#define CAM_MEM_FREE(p) drop(p)
struct mutex { bool initialized; };
static void mutex_init(struct mutex *m) { assert(!m->initialized); m->initialized = true; }
static void mutex_destroy(struct mutex *m) { assert(m->initialized); m->initialized = false; }
struct device_node { int id; const char *name; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; const char *name; };
#define to_platform_device(p) ((struct platform_device *)(p))
struct cam_context { bool ready; };
struct context { struct cam_context *base; };
#define cam_isp_context context
struct cam_hw_mgr_intf { int ready; };
struct cam_node { struct cam_hw_mgr_intf hw_mgr_intf; int crm_node_intf; int (*sd_handler)(void); };
struct cam_subdev { const void *internal_ops; int close_seq_prior; void *token; int (*msg_cb)(void); };
static struct { struct cam_subdev sd; struct context ctx_jpeg[64]; struct cam_context ctx[64]; struct mutex jpeg_mutex; } g_jpeg_dev;
static struct {
    struct cam_subdev sd; unsigned int max_context, isp_device_type;
    struct cam_context *ctx; struct context *ctx_isp; struct mutex isp_mutex;
} g_isp_dev;
static struct {
    void *dentry, *work_process_frame, *work_process_irq_cb;
    int iommu_hdl;
    void **devices[2];
    struct { struct mutex ctx_mutex; } ctx_data[64];
    struct mutex hw_mgr_mutex;
} g_jpeg_hw_mgr;
static int cam_jpeg_subdev_internal_ops, cam_isp_subdev_internal_ops;
static int cam_jpeg_subdev_close_internal(void) { return 0; }
static int cam_isp_subdev_close_internal(void) { return 0; }
static int cam_isp_subdev_handle_message(void) { return 0; }
static int cam_isp_vmrm_callback_handler;
static int cam_jpeg_dev_mini_dump_cb, cam_jpeg_hw_mgr_mini_dump_cb;
static int cam_jpeg_dev_evt_inject_cb, cam_isp_dev_evt_inject_cb;
static int cam_jpeg_dev_iommu_fault_handler, cam_isp_dev_iommu_fault_handler;
struct cam_driver_node { int driver_id; char driver_name[32]; int driver_msg_callback; void *driver_msg_callback_data; };
static bool manager, dump_registered, fault_registered;
static int contexts, contexts_destroyed, managers_destroyed, workqueues_destroyed;
static int contexts_expected;
static void cam_common_register_evt_inject_cb(int fn, int id) { (void)fn; (void)id; }
static void cam_common_unregister_mini_dump_cb(int fn, void *p) { (void)fn; (void)p; assert(dump_registered); dump_registered = false; }
static void debugfs_remove_recursive(void *p) { assert(p == (void *)1); }
static void cam_req_mgr_workq_destroy(void **p) {
    assert(*p && !dump_registered);
    assert(contexts == contexts_expected);
    drop(*p); *p = NULL; workqueues_destroyed++;
}
static int cam_smmu_destroy_handle(int h) {
    assert(h == 42 && !fault_registered);
    assert(!g_jpeg_hw_mgr.work_process_frame && !g_jpeg_hw_mgr.work_process_irq_cb);
    manager = false;
    return 0;
}
static void cam_record_bind_latency(const char *name, long us) { (void)name; (void)us; }
static int cam_subdev_probe(struct cam_subdev *s, struct platform_device *p, const char *name, int type) {
    (void)p; (void)name; (void)type;
    s->token = allocate(sizeof(struct cam_node)); return s->token ? 0 : -ENOMEM;
}
static int cam_subdev_remove(struct cam_subdev *s) {
    assert(!manager && !contexts && !fault_registered);
    drop(s->token); memset(s, 0, sizeof(*s)); return 0;
}
static int cam_jpeg_hw_mgr_init(struct device_node *n, uint64_t *intf, int *handle, int cb) {
    (void)n; (void)intf; (void)cb;
    if (failure()) return -EIO;
    int saved = fail_at; fail_at = 0;
    g_jpeg_hw_mgr.work_process_frame = allocate(32);
    g_jpeg_hw_mgr.work_process_irq_cb = allocate(32);
    g_jpeg_hw_mgr.devices[0] = allocate(8); g_jpeg_hw_mgr.devices[1] = allocate(8);
    fail_at = saved;
    g_jpeg_hw_mgr.iommu_hdl = *handle = 42; g_jpeg_hw_mgr.dentry = (void *)1;
    mutex_init(&g_jpeg_hw_mgr.hw_mgr_mutex);
    for (int i = 0; i < 64; i++) mutex_init(&g_jpeg_hw_mgr.ctx_data[i].ctx_mutex);
    manager = dump_registered = true; return 0;
}
static int cam_jpeg_context_init(struct context *c, struct cam_context *base, struct cam_hw_mgr_intf *m, int i, int h) {
    (void)m; (void)i; assert(manager && h == 42);
    if (failure()) return -EIO;
    c->base = base; base->ready = true; contexts++; contexts_expected = contexts; return 0;
}
static int cam_jpeg_context_deinit(struct context *c) {
    assert(c->base && c->base->ready && contexts);
    c->base->ready = false; c->base = NULL; contexts--; contexts_destroyed++; return 0;
}
static int cam_node_init(struct cam_node *n, struct cam_hw_mgr_intf *m, struct cam_context *base, int count, const char *name) {
    (void)n; (void)m; (void)base; (void)name;
    assert(contexts == count); return failure() ? -EIO : 0;
}
static void cam_smmu_set_client_page_fault_handler(int h, int fn, void *token) {
    assert(manager && h == 42 && token); (void)fn; fault_registered = true;
}
static void cam_smmu_remove_client(void *p) { assert(p); fault_registered = false; }
static int of_property_read_string_index(struct device_node *n, const char *prop, int i, const char **out) {
    (void)n; (void)i; assert(!strcmp(prop, "arch-compat"));
    if (failure()) return -EIO;
    *out = "ife";
    return 0;
}
static char *strnstr(const char *s, const char *needle, size_t len) { (void)len; return strstr(s, needle); }
#define scnprintf snprintf
static int cam_isp_hw_mgr_init(const char *s, struct cam_hw_mgr_intf *m, int *h, int type) {
    (void)s; (void)m; (void)type;
    if (failure()) return -EIO;
    manager = true;
    *h = 42;
    return 0;
}
static void cam_isp_hw_mgr_deinit(const char *s) {
    (void)s; assert(manager && !fault_registered);
    assert(contexts == contexts_expected); manager = false; managers_destroyed++;
}
static int cam_isp_context_init(struct context *c, struct cam_context *base, int *crm, struct cam_hw_mgr_intf *m, int i, int type, int h) {
    (void)crm; (void)type; return cam_jpeg_context_init(c, base, m, i, h);
}
static int cam_isp_context_deinit(struct context *c) { return cam_jpeg_context_deinit(c); }
static int cam_vmrm_populate_driver_node_info(struct cam_driver_node *node) {
    assert(node->driver_msg_callback_data == &g_isp_dev); return failure() ? -EIO : 0;
}
#include "clients_camera.inc"
static void reset(void) {
    assert(!live && !contexts && !fault_registered);
    memset(&g_jpeg_dev, 0, sizeof(g_jpeg_dev)); memset(&g_jpeg_hw_mgr, 0, sizeof(g_jpeg_hw_mgr));
    memset(&g_isp_dev, 0, sizeof(g_isp_dev));
    manager = dump_registered = false; step = fail_at = contexts_expected = 0;
    contexts_destroyed = managers_destroyed = workqueues_destroyed = 0;
}
static void run_jpeg(struct device *d, int failed) {
    reset(); fail_at = failed;
    int rc = cam_jpeg_dev_component_bind(d, NULL, NULL);
    if (failed) assert(rc < 0);
    else {
        assert(!rc && contexts == 64 && fault_registered);
        cam_jpeg_dev_component_unbind(d, NULL, NULL);
    }
    assert(!live && !contexts && !manager && !dump_registered && !fault_registered);
}
int main(void) {
    struct device_node n = {0}; struct platform_device p = {.dev.of_node = &n, .name = "camera-test"};
    /* Mock allocations inside successful manager init aren't injection sites. */
    run_jpeg(&p.dev, 0);
    assert(contexts_destroyed == 64 && workqueues_destroyed == 2);
    int total = step, tested = 0;
    for (int fail = 1; fail <= total; fail++) {
        if (fail >= 3 && fail <= 6) continue;
        run_jpeg(&p.dev, fail); tested++;
    }
    for (int cycle = 0; cycle < 3; cycle++) run_jpeg(&p.dev, 0);
    reset();
    assert(!cam_isp_dev_component_bind(&p.dev, NULL, NULL));
    int isp_steps = step;
    cam_isp_dev_component_unbind(&p.dev, NULL, NULL);
    assert(!live && !contexts && !manager && contexts_destroyed == 32);
    for (int fail = 1; fail <= isp_steps; fail++) {
        reset(); fail_at = fail;
        assert(cam_isp_dev_component_bind(&p.dev, NULL, NULL) < 0);
        assert(!live && !contexts && !manager && !fault_registered);
    }
    assert(!node_refs && !device_refs);
    printf("{\"jpeg_setup_failures\":%d,\"jpeg_contexts_drained\":64,\"jpeg_rebinds\":3,\"isp_setup_failures\":%d,\"isp_contexts_drained\":32}\n", tested, isp_steps);
}
