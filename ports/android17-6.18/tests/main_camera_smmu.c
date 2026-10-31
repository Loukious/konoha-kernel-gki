#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPROBE_DEFER 517
#define EBADHANDLE 521
#define GFP_KERNEL 0
#define BIT(n) (1UL << (n))
#define DMA_BIT_MASK(n) (~0ULL >> (64 - (n)))
#define READ_ONCE(v) (v)
#define HANDLE_INIT (-1)
#define CAM_SMMU_CB_MAX 6
#define CAM_SMMU_SHARED_HDL_MAX 6
#define CAM_SMMU_MULTI_REGION_MAX 2
#define CAM_SMMU_HDL_MASK 0xfffffff
#define GET_SMMU_TABLE_IDX(x) (((x) & CAM_SMMU_HDL_MASK) >> 16)
#define CAM_SMMU_HDL_VALIDATE(a, b) ((a) != ((b) & CAM_SMMU_HDL_MASK))
#define SHARED_MEM_POOL_GRANULARITY 16
#define CAM_ERR(...) ((void)0)
#define CAM_DBG(...) ((void)0)
#define CAM_WARN(...) ((void)0)
#define EXPORT_SYMBOL_GPL(...)
#define EXPORT_SYMBOL(...)
#define unlikely(x) (x)
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
#define WARN_ON(x) (!!(x))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
static int panic_count;
#define CAM_TRIGGER_PANIC(...) (panic_count++)
struct timespec64 { long tick; };
#define CAM_GET_TIMESTAMP(ts) ((ts).tick = 0)
#define CAM_GET_TIMESTAMP_DIFF_IN_MICRO(a, b, c) ((c) = (b).tick - (a).tick)
#define trace_io_page_fault(...) ((void)0)

struct mutex { pthread_mutex_t lock; atomic_int waiters; bool initialized; };
static void mutex_init(struct mutex *m) {
    assert(!m->initialized);
    assert(!pthread_mutex_init(&m->lock, NULL));
    atomic_init(&m->waiters, 0);
    m->initialized = true;
}
static void mutex_lock(struct mutex *m) {
    assert(m->initialized);
    atomic_fetch_add(&m->waiters, 1);
    assert(!pthread_mutex_lock(&m->lock));
    atomic_fetch_sub(&m->waiters, 1);
}
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->lock)); }
static void mutex_destroy(struct mutex *m) {
    assert(m->initialized && !atomic_load(&m->waiters));
    assert(!pthread_mutex_destroy(&m->lock));
    m->initialized = false;
}
struct rw_semaphore { pthread_rwlock_t lock; atomic_int writers; };
static void init_rwsem(struct rw_semaphore *s) {
    assert(!pthread_rwlock_init(&s->lock, NULL)); atomic_init(&s->writers, 0);
}
static void down_read(struct rw_semaphore *s) { assert(!pthread_rwlock_rdlock(&s->lock)); }
static void up_read(struct rw_semaphore *s) { assert(!pthread_rwlock_unlock(&s->lock)); }
static void down_write(struct rw_semaphore *s) {
    atomic_fetch_add(&s->writers, 1);
    assert(!pthread_rwlock_wrlock(&s->lock));
    atomic_fetch_sub(&s->writers, 1);
}
static void up_write(struct rw_semaphore *s) { assert(!pthread_rwlock_unlock(&s->lock)); }
#define spin_lock_init(p) (*(p) = 1)
struct list_head { struct list_head *next, *prev; };
static void INIT_LIST_HEAD(struct list_head *l) { l->next = l->prev = l; }
static int steps, fail_step, live;
static bool fail(void) { return ++steps == fail_step; }
static void *allocate(size_t n) {
    if (fail()) return NULL;
    void *p = calloc(1, n); assert(p); live++; return p;
}
static void release(void *p) { if (p) { assert(live > 0); live--; free(p); } }
#define CAM_MEM_ZALLOC_ARRAY(n, z, g) allocate((n) * (z))
#define CAM_MEM_FREE(p) release(p)

struct device;
struct iommu_domain;
typedef int (*iommu_fault_handler_t)(struct iommu_domain *, struct device *, unsigned long, int, void *);
struct qcom_iommu_ops { void (*set_fault_handler)(struct iommu_domain *, iommu_fault_handler_t, void *); };
enum { IOMMU_COOKIE_NONE, IOMMU_COOKIE_FAULT_HANDLER, IOMMU_COOKIE_DMA };
struct iommu_domain {
    struct qcom_iommu_ops *ops;
    int cookie_type;
    iommu_fault_handler_t handler;
    void *handler_token;
};
struct arm_smmu_domain {
    struct iommu_domain domain;
    struct mutex fault_handler_lock;
    iommu_fault_handler_t fault_handler;
    void *fault_token;
};
static struct arm_smmu_domain *to_smmu_domain(struct iommu_domain *d) {
    return container_of(d, struct arm_smmu_domain, domain);
}
static struct qcom_iommu_ops *to_qcom_iommu_ops(struct qcom_iommu_ops *ops) {
    return fail() ? NULL : ops;
}
#include "smmu_provider.inc"
static struct qcom_iommu_ops native_ops = { arm_smmu_set_fault_handler };
static int legacy_registration(struct iommu_domain *d, iommu_fault_handler_t fn, void *token) {
    iommu_set_fault_handler(d, fn, token); return 0;
}

struct device_node {
    struct device_node *children;
    int child_count, index, kind, labels, multi_count;
    bool secure, coherent, hint;
    const char *multi_label;
};
struct device { struct device_node *of_node; void *dma_parms; struct arm_smmu_domain *smmu; };
struct platform_device { struct device dev; const char *name; };
#define to_platform_device(d) container_of(d, struct platform_device, dev)
#define for_each_available_child_of_node(root, child) \
    for (child = (root)->children; child != (root)->children + (root)->child_count; child++)
static bool of_device_is_compatible(struct device_node *n, const char *s) {
    return n->kind == 1 && !strcmp(s, "qcom,msm-cam-smmu-cb");
}
static bool of_property_read_bool(struct device_node *n, const char *s) {
    if (!strcmp(s, "dma-coherent")) return n->coherent;
    if (!strcmp(s, "dma-coherent-hint-cached")) return n->hint;
    return !strcmp(s, "expanded_memory");
}
static int of_property_count_strings(struct device_node *n, const char *s) {
    if (!strcmp(s, "cam-smmu-label")) return n->labels;
    if (!strcmp(s, "multiple-same-region-clients")) return n->multi_count;
    return -EINVAL;
}
static int of_property_read_string_index(struct device_node *n, const char *s, int i, const char **out) {
    if (fail()) return -EIO;
    if (!strcmp(s, "cam-smmu-label")) *out = i ? "aux" : "camera";
    else if (!strcmp(s, "multiple-same-region-clients")) *out = n->multi_label;
    else *out = "non-fatal";
    return 0;
}
struct cam_smmu_pf_info {
    struct iommu_domain *domain;
    struct device *dev;
    unsigned long iova;
    int flags;
    void *token;
    uint32_t buf_info;
    bool in_map_region, is_secure;
};
enum cam_iommu_type { CAM_QSMMU, CAM_ARM_SMMU };
enum cam_smmu_init_dir { CAM_SMMU_TABLE_INIT, CAM_SMMU_TABLE_DEINIT };
enum { CAM_SMMU_NO_COHERENCY, CAM_SMMU_DMA_COHERENT, CAM_SMMU_DMA_COHERENT_HINT_CACHED };
enum { CAM_SMMU_ATTACH, CAM_SMMU_DETACH };
struct gen_pool { int marker; };
struct scratch_mapping { void *bitmap; };
struct region { unsigned long iova_start, iova_len; };
struct nested_region { struct region region_info; };
struct multi_region { int num_regions; struct nested_region nested_regions[2]; };
struct cam_context_bank_info {
    struct device *dev;
    struct iommu_domain *domain;
    const char *name[6], *multi_region_clients[2];
    int num_shared_hdl, num_multi_regions, coherency_mode, state, handle, cb_count, pf_count;
    bool is_mul_client, is_secure, is_secheap_allocated, shared_support, scratch_buf_support;
    bool io_support, fault_handler_registered, dma_parms_owned, non_fatal_faults_en;
    unsigned long discard_iova_start, discard_iova_len;
    struct gen_pool *shared_mem_pool[2];
    struct multi_region shared_info;
    struct scratch_mapping scratch_map;
    struct region scratch_info;
    struct mutex lock;
    struct rw_semaphore handler_lock;
    long monitor_head;
    struct list_head smmu_buf_list, smmu_buf_kernel_list;
    void (*handler[6])(struct cam_smmu_pf_info *);
    void *token[6];
};
struct cam_iommu_cb_set {
    struct cam_context_bank_info *cb_info;
    unsigned int cb_num, cb_init_count;
    int s_lock, csf_version;
    bool force_cache_allocs, need_shared_buffer_padding, is_expanded_memory;
    struct { void *dentry; unsigned int fatal_pf_mask; } debug_cfg;
} iommu_cb_set;
static int g_num_pf_handled = 1, dump_count;
#define atomic64_set(p, v) (*(p) = (v))
static struct gen_pool *gen_pool_create(int a, int b) { (void)a; (void)b; return allocate(sizeof(struct gen_pool)); }
static int gen_pool_add(struct gen_pool *p, unsigned long a, unsigned long l, int n) {
    assert(p); (void)a; (void)l; (void)n; return fail() ? -EIO : 0;
}
static void gen_pool_destroy(struct gen_pool *p) { release(p); }
static int cam_smmu_init_scratch_map(struct scratch_mapping *m, unsigned long a, unsigned long l, int n) {
    (void)a; (void)l; (void)n; m->bitmap = allocate(16); return m->bitmap ? 0 : -ENOMEM;
}
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d) {
    return fail() ? NULL : &d->smmu->domain;
}
static void cam_smmu_util_iommu_custom(struct device *d, unsigned long a, unsigned long l) { (void)d; (void)a; (void)l; }
static void *devm_kzalloc(struct device *d, size_t n, int flags) { (void)d; (void)flags; return allocate(n); }
static void devm_kfree(struct device *d, void *p) { assert(p == d->dma_parms); release(p); }
static int dma_set_max_seg_size(struct device *d, unsigned long v) { assert(d->dma_parms); (void)v; return 0; }
static int dma_set_seg_boundary(struct device *d, unsigned long v) { assert(d->dma_parms); (void)v; return 0; }
static int dma_set_mask_and_coherent(struct device *d, unsigned long long m) { (void)d; (void)m; return fail() ? -EIO : 0; }
static int cam_smmu_get_memory_regions_info(struct device_node *n, struct cam_context_bank_info *cb) {
    if (fail()) return -EIO;
    cb->is_secure = n->secure;
    cb->shared_support = cb->scratch_buf_support = cb->io_support = true;
    cb->shared_info.num_regions = 2;
    cb->shared_info.nested_regions[0].region_info = (struct region){0x10000, 0x10000};
    cb->shared_info.nested_regions[1].region_info = (struct region){0x20000, 0x10000};
    return 0;
}
static void cam_smmu_check_for_fault_properties(const char *s, struct cam_context_bank_info *cb) { (void)s; (void)cb; }
static void cam_check_iommu_faults(struct iommu_domain *d, struct cam_smmu_pf_info *info) { (void)d; (void)info; }
static uint32_t cam_smmu_find_closest_mapping(int idx, void *addr, bool *in_map) {
    assert(pthread_mutex_trylock(&iommu_cb_set.cb_info[idx].lock.lock) == EBUSY);
    *in_map = addr == (void *)0x1000; return 42;
}
static void cam_smmu_dump_cb_info(int idx) {
    assert(pthread_mutex_trylock(&iommu_cb_set.cb_info[idx].lock.lock) == EBUSY);
    dump_count++;
}
static int cam_smmu_fetch_csf_version(int *v) { *v = 33; return fail() ? -EIO : 0; }
static int cam_smmu_create_debug_fs(void) { iommu_cb_set.debug_cfg.dentry = (void *)1; return 0; }
static void debugfs_remove_recursive(void *p) { (void)p; }
static void cam_record_bind_latency(const char *n, long t) { (void)n; (void)t; }
static unsigned long cam_smmu_mini_dump_cb(void *d, unsigned long n, void *p) { (void)d; (void)n; (void)p; return 0; }
static int cam_common_register_mini_dump_cb(unsigned long (*fn)(void *, unsigned long, void *), const char *n, void *p) { (void)fn; (void)n; (void)p; return 0; }
static void cam_common_unregister_mini_dump_cb(unsigned long (*fn)(void *, unsigned long, void *), void *p) { (void)fn; (void)p; }
#include "smmu_camera.inc"

static struct platform_device parent, devices[2];
static struct device_node root_node, nodes[2];
static struct arm_smmu_domain domains[2];
static void fixture(void) {
    assert(!live && !iommu_cb_set.cb_info);
    memset(&iommu_cb_set, 0, sizeof(iommu_cb_set));
    memset(&parent, 0, sizeof(parent));
    memset(devices, 0, sizeof(devices));
    root_node = (struct device_node){.children = nodes, .child_count = 2};
    parent.dev.of_node = &root_node; parent.name = "smmu";
    for (int i = 0; i < 2; i++) {
        nodes[i] = (struct device_node){.kind = 1, .labels = 2, .multi_count = 1, .multi_label = "aux"};
        devices[i].dev.of_node = &nodes[i]; devices[i].dev.smmu = &domains[i]; devices[i].name = "cb";
        memset(&domains[i], 0, sizeof(domains[i]));
        mutex_init(&domains[i].fault_handler_lock);
        domains[i].domain = (struct iommu_domain){.ops = &native_ops, .cookie_type = IOMMU_COOKIE_DMA};
    }
    fail_step = 0; steps = 0; dump_count = 0;
}
static void cleanup(void) {
    fail_step = 0;
    if (iommu_cb_set.cb_info) cam_smmu_component_unbind(&parent.dev, NULL, NULL);
    assert(!live && !iommu_cb_set.cb_info && !iommu_cb_set.cb_num && !iommu_cb_set.cb_init_count);
    for (int i = 0; i < 2; i++) {
        assert(!domains[i].fault_handler && !devices[i].dev.dma_parms);
        mutex_destroy(&domains[i].fault_handler_lock);
    }
}
struct client { atomic_int entered, resume, calls; bool gated; struct cam_smmu_pf_info last; };
static void handler(struct cam_smmu_pf_info *pf) {
    struct client *c = pf->token; c->last = *pf;
    atomic_fetch_add(&c->calls, 1); atomic_store(&c->entered, 1);
    while (c->gated && !atomic_load(&c->resume)) sched_yield();
}
static int fault(int idx) {
    struct arm_smmu_domain *d = &domains[idx];
    int ret = report_iommu_fault(&d->domain, &devices[idx].dev, 0x1000, 9);
    mutex_lock(&d->fault_handler_lock);
    if (ret == -ENOSYS && d->fault_handler)
        ret = d->fault_handler(&d->domain, &devices[idx].dev, 0x1000, 9, d->fault_token);
    mutex_unlock(&d->fault_handler_lock); return ret;
}
static void *fault_thread(void *p) { (void)p; assert(fault(0) == -ENOSYS); return NULL; }
static atomic_int removal_done;
static void *remove_thread(void *p) { cam_smmu_remove_client(p); atomic_store(&removal_done, 1); return NULL; }
static void *bank_thread(void *p) { (void)p; cam_smmu_cb_component_unbind(&devices[0].dev, NULL, NULL); atomic_store(&removal_done, 1); return NULL; }
static void register_client(int idx, struct client *c) {
    int handle = (idx << 16) | 0x1234;
    iommu_cb_set.cb_info[idx].handle = handle;
    cam_smmu_set_client_page_fault_handler(handle, handler, c);
}
int main(void) {
    fixture(); assert(!cam_smmu_component_bind(&parent.dev, NULL, NULL));
    steps = 0; assert(!cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL));
    int total_steps = steps;
    assert(domains[0].fault_handler == cam_smmu_iommu_fault_handler);
    assert(domains[0].domain.cookie_type == IOMMU_COOKIE_DMA);
    assert(iommu_cb_set.cb_init_count == 1);
    cleanup();
    for (int n = 1; n <= total_steps; n++) {
        fixture(); assert(!cam_smmu_component_bind(&parent.dev, NULL, NULL));
        assert(!cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL));
        int kept = live;
        steps = 0; fail_step = n;
        assert(cam_smmu_cb_component_bind(&devices[1].dev, NULL, NULL) < 0);
        assert(live == kept && iommu_cb_set.cb_num == 2 && iommu_cb_set.cb_init_count == 1);
        assert(domains[0].fault_handler == cam_smmu_iommu_fault_handler && !domains[1].fault_handler);
        cleanup();
    }
    for (int n = 1; n <= 2; n++) {
        fixture(); fail_step = n; assert(cam_smmu_component_bind(&parent.dev, NULL, NULL) < 0);
        assert(!live && !iommu_cb_set.cb_info); cleanup();
    }
    fixture(); assert(!cam_smmu_component_bind(&parent.dev, NULL, NULL));
    assert(cam_smmu_component_bind(&parent.dev, NULL, NULL) == -EBUSY);
    nodes[0].labels = -EINVAL; assert(cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL) == -EINVAL);
    nodes[0].labels = 7; assert(cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL) == -EINVAL);
    nodes[0].labels = 2; nodes[0].multi_label = "not-a-client";
    assert(cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL) == -EINVAL);
    nodes[0].multi_label = "aux";
    assert(cam_smmu_cb_qsmmu_component_bind(&devices[0].dev, NULL, NULL) == -ENODEV);
    nodes[0].coherent = nodes[0].hint = true;
    assert(cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL) == -EBADR);
    nodes[0].coherent = nodes[0].hint = false;
    assert(!cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL));
    assert(!cam_smmu_cb_component_bind(&devices[1].dev, NULL, NULL));
    assert(cam_smmu_cb_component_bind(&devices[1].dev, NULL, NULL) == -EBADHANDLE);
    struct client clients[7] = {0};
    for (int i = 0; i < 6; i++) register_client(0, &clients[i]);
    register_client(0, &clients[0]); assert(iommu_cb_set.cb_info[0].cb_count == 6);
    register_client(0, &clients[6]); assert(iommu_cb_set.cb_info[0].cb_count == 6);
    cam_smmu_unset_client_page_fault_handler(0x1234, &clients[3]);
    register_client(0, &clients[6]); assert(iommu_cb_set.cb_info[0].cb_count == 6);
    register_client(1, &clients[0]);
    assert(fault(0) == -ENOSYS && dump_count == 1);
    assert(atomic_load(&clients[0].calls) == 1 && atomic_load(&clients[3].calls) == 0);
    assert(clients[6].last.buf_info == 42 && clients[6].last.in_map_region);
    assert(fault(0) == 0 && dump_count == 1);
    cam_smmu_reset_cb_page_fault_cnt();
    cam_smmu_remove_client(&clients[0]);
    assert(iommu_cb_set.cb_info[0].cb_count == 5 && iommu_cb_set.cb_info[1].cb_count == 0);
    assert(fault(0) == -ENOSYS && atomic_load(&clients[0].calls) == 1);
    assert(cam_smmu_iommu_fault_handler(&domains[0].domain, &devices[0].dev, 0, 0, NULL) == -EINVAL);
    cleanup();
    fixture(); assert(!cam_smmu_component_bind(&parent.dev, NULL, NULL));
    char inherited_params;
    devices[0].dev.dma_parms = &inherited_params;
    assert(!cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL));
    cam_smmu_cb_component_unbind(&devices[0].dev, NULL, NULL);
    assert(devices[0].dev.dma_parms == &inherited_params);
    devices[0].dev.dma_parms = NULL; cleanup();
    fixture(); assert(!cam_smmu_component_bind(&parent.dev, NULL, NULL));
    nodes[0].secure = true; assert(!cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL));
    assert(!domains[0].fault_handler && live == 1); cleanup();
    for (int mode = 0; mode < 2; mode++) for (int round = 0; round < 50; round++) {
        fixture(); assert(!cam_smmu_component_bind(&parent.dev, NULL, NULL));
        assert(!cam_smmu_cb_component_bind(&devices[0].dev, NULL, NULL));
        struct client c = {.gated = true}; register_client(0, &c);
        atomic_store(&removal_done, 0);
        pthread_t producer, remover;
        assert(!pthread_create(&producer, NULL, fault_thread, NULL));
        while (!atomic_load(&c.entered)) sched_yield();
        assert(!pthread_create(&remover, NULL, mode ? bank_thread : remove_thread, &c));
        while (!atomic_load(&removal_done) && !(mode ? atomic_load(&domains[0].fault_handler_lock.waiters) : atomic_load(&iommu_cb_set.cb_info[0].handler_lock.writers))) sched_yield();
        assert(!atomic_load(&removal_done));
        assert(iommu_cb_set.cb_info[0].dev == &devices[0].dev);
        atomic_store(&c.resume, 1);
        assert(!pthread_join(producer, NULL)); assert(!pthread_join(remover, NULL));
        assert(atomic_load(&removal_done)); cleanup();
    }
    printf("{\"context_setup_failures\":%d,\"parent_setup_failures\":2,\"invalid_metadata_cases\":6,\"registry_and_fault_cases\":12,\"inherited_dma_parameters\":1,\"secure_context\":1,\"client_withdrawal_races\":50,\"provider_withdrawal_races\":50}\n", total_steps);
}
