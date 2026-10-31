#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t u32;
typedef uint64_t u64;
struct device_dma_parameters { unsigned int max_segment_size; unsigned long segment_boundary_mask; };
struct iommu_domain { void *ops; };
struct device { struct device_dma_parameters *dma_parms; struct iommu_domain *domain; };
typedef int (*iommu_fault_handler_t)(struct iommu_domain *, struct device *, unsigned long, int, void *);
struct arm_smmu_domain {
    struct iommu_domain domain;
    pthread_mutex_t fault_handler_lock;
    iommu_fault_handler_t fault_handler;
    void *fault_token;
};
struct arm_smmu_device { struct device *dev; };
struct qcom_iommu_ops { void (*set_fault_handler)(struct iommu_domain *, iommu_fault_handler_t, void *); };
struct msm_vidc_core;
/* LAYOUT */
struct msm_vidc_core { struct context_bank_info *cb; bool alive; };
static atomic_int active, release_started, finished, permit, attempted;
static bool blocking;
static int fail, allocations, calls;
static void mutex_lock(pthread_mutex_t *lock)
{
    if (atomic_load(&release_started)) atomic_store(&attempted, 1);
    assert(!pthread_mutex_lock(lock));
}
static void mutex_unlock(pthread_mutex_t *lock) { assert(!pthread_mutex_unlock(lock)); }
#define to_smmu_domain(d) ((struct arm_smmu_domain *)(d))
#define d_vpr_h(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define unlikely(x) (x)
#define WARN_ON_ONCE(x) (x)
#define GFP_KERNEL 0
#define LINUX_VERSION_CODE 618
#define KERNEL_VERSION(a,b,c) 516
#define DMA_BIT_MASK(n) (UINT64_MAX >> (64 - (n)))
#define ARM_SMMU_CB_FSR 0
#define ARM_SMMU_CB_FSYNR0 1
#define ARM_SMMU_CB_FAR 2
#define ARM_SMMU_FSYNR0_WNR 1
#define ARM_SMMU_FSR_TF 2
#define ARM_SMMU_FSR_PF 4
#define ARM_SMMU_FSR_EF 8
#define ARM_SMMU_FSR_SS 16
#define IOMMU_FAULT_WRITE 1
#define IOMMU_FAULT_READ 2
#define IOMMU_FAULT_TRANSLATION 4
#define IOMMU_FAULT_PERMISSION 8
#define IOMMU_FAULT_EXTERNAL 16
#define IOMMU_FAULT_TRANSACTION_STALLED 32
static u32 arm_smmu_cb_read(struct arm_smmu_device *s, int i, int reg) { return 0; }
static u64 arm_smmu_cb_readq(struct arm_smmu_device *s, int i, int reg) { return 4096; }
static int report_iommu_fault(struct iommu_domain *d, struct device *v, unsigned long a, int f) { return -ENOSYS; }
/* PROVIDER */
static struct qcom_iommu_ops ops = { .set_fault_handler = arm_smmu_set_fault_handler };
static struct qcom_iommu_ops *to_qcom_iommu_ops(void *arg) { return arg; }
/* UTIL */
static struct context_bank_info *msm_vidc_get_context_bank_for_device(struct msm_vidc_core *c, struct device *d) { return c->cb; }
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d) { return d->domain; }
static int dma_set_mask_and_coherent(struct device *d, u64 mask) { return fail == 1 ? -EIO : 0; }
static void *kzalloc(size_t n, int flags) { if (fail == 2) return NULL; allocations++; return calloc(1, n); }
static void kfree(void *p) { assert(p); allocations--; free(p); }
/* DMA */
static int msm_vidc_smmu_fault_handler(struct iommu_domain *d, struct device *v, unsigned long a, int f, void *token)
{
    struct msm_vidc_core *core = token;
    assert(core && core->alive && core->cb->dev);
    calls++;
    atomic_store(&active, 1);
    while (blocking && !atomic_load(&permit)) sched_yield();
    assert(core->alive && core->cb->dev);
    atomic_store(&active, 0);
    return -ENOSYS;
}
/* VIDEO */
struct test { struct msm_vidc_core core; struct device dev; struct arm_smmu_domain domain; struct arm_smmu_device smmu; };
static void *dispatch(void *arg)
{
    struct test *t = arg;
    assert(report_iommu_fault_helper(&t->domain, &t->smmu, 0) == -ENOSYS);
    return NULL;
}
static void *remove_context(void *arg)
{
    struct test *t = arg;
    atomic_store(&release_started, 1);
    msm_vidc_component_unbind(&t->dev, NULL, &t->core);
    atomic_store(&finished, 1);
    return NULL;
}
int main(void)
{
    struct context_bank_info cb = { .dma_mask = UINT64_MAX };
    struct test t = { .core = { .cb = &cb, .alive = true }, .domain = { .domain.ops = &ops, .fault_handler_lock = PTHREAD_MUTEX_INITIALIZER } };
    struct device_dma_parameters borrowed = { .max_segment_size = 123, .segment_boundary_mask = 456 };
    t.dev.domain = &t.domain.domain; t.smmu.dev = &t.dev;
    t.core.cb = NULL; assert(msm_vidc_setup_context_bank(&t.core, &t.dev) == -EIO); t.core.cb = &cb;
    t.dev.domain = NULL; assert(msm_vidc_setup_context_bank(&t.core, &t.dev) == -EIO); assert(!cb.dev && !cb.domain); t.dev.domain = &t.domain.domain;
    for (int own = 0; own < 2; own++) {
        for (fail = 1; fail <= 5; fail++) {
            if (fail == 3 || fail == 4) continue;
            if (!own && fail == 2) continue;
            t.dev.dma_parms = own ? NULL : &borrowed;
            t.domain.domain.ops = fail == 5 ? NULL : &ops;
            assert(msm_vidc_setup_context_bank(&t.core, &t.dev) < 0);
            assert(!cb.dev && !cb.domain && !cb.fault_registered && !cb.dma_parms_owned && !allocations);
            assert(t.dev.dma_parms == (own ? NULL : &borrowed));
            assert(borrowed.max_segment_size == 123 && borrowed.segment_boundary_mask == 456);
        }
        fail = 0; t.domain.domain.ops = &ops;
        for (int cycle = 0; cycle < 2; cycle++) {
            t.dev.dma_parms = own ? NULL : &borrowed;
            assert(!msm_vidc_setup_context_bank(&t.core, &t.dev));
            assert(cb.dev == &t.dev && cb.domain == &t.domain.domain && cb.fault_registered);
            assert(t.dev.dma_parms->max_segment_size == UINT32_MAX && t.dev.dma_parms->segment_boundary_mask == UINT64_MAX);
            assert(msm_vidc_setup_context_bank(&t.core, &t.dev) == -EBUSY);
            dispatch(&t); assert(calls == 1); calls = 0;
            msm_vidc_component_unbind(&t.dev, NULL, &t.core);
            assert(!cb.dev && !cb.domain && !t.domain.fault_handler && !t.domain.fault_token && !allocations);
            assert(t.dev.dma_parms == (own ? NULL : &borrowed));
            assert(borrowed.max_segment_size == 123 && borrowed.segment_boundary_mask == 456);
            msm_vidc_component_unbind(&t.dev, NULL, &t.core);
            dispatch(&t); assert(!calls);
        }
    }
    t.dev.dma_parms = NULL;
    assert(!msm_vidc_setup_context_bank(&t.core, &t.dev));
    blocking = true;
    pthread_t reader, remover;
    assert(!pthread_create(&reader, NULL, dispatch, &t));
    while (!atomic_load(&active)) sched_yield();
    assert(!pthread_create(&remover, NULL, remove_context, &t));
    while (!atomic_load(&attempted) && !atomic_load(&finished)) sched_yield();
    assert(!atomic_load(&finished));
    assert(cb.dev && cb.domain && allocations == 1);
    atomic_store(&permit, 1);
    pthread_join(reader, NULL); pthread_join(remover, NULL);
    assert(atomic_load(&finished) && !allocations && !cb.dev && !cb.domain);
    t.core.alive = false; calls = 0; dispatch(&t); assert(!calls);
    puts("12 context-bank failure, rebind and callback-drain cases passed");
    return 0;
}
