#!/usr/bin/env python3
"""Exercise the actual 6.18 and SMMU fault dispatch functions in a host harness.

Only register reads, mutexes and structural types are host adapters. No phone
fault injection, no recipe/staged mutation. Hardware/complete DDK build remain
separate verification requirements.
"""
from pathlib import Path
import json
import re
import subprocess
import tempfile

ROOT = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')

def function(path, name):
    text = path.read_text()
    start = re.search(r'^(?:static )?(?:int|void) ' + name + r'\(', text, re.M).start()
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

core = ROOT / 'common/drivers/iommu/iommu.c'
smmu = ROOT / 'onyx/drivers/iommu/arm/arm-smmu/arm-smmu.c'
util = ROOT / 'onyx/drivers/iommu/qcom-iommu-util.c'
functions = '\n\n'.join([
    function(core, 'report_iommu_fault'),
    function(smmu, 'report_iommu_fault_helper'),
    function(smmu, 'arm_smmu_set_fault_handler'),
    function(util, 'qcom_iommu_set_fault_handler'),
])
adapter = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

typedef uint32_t u32;
struct device { int id; };
struct iommu_domain;
typedef int (*iommu_fault_handler_t)(struct iommu_domain *, struct device *, unsigned long, int, void *);
enum { IOMMU_COOKIE_NONE, IOMMU_COOKIE_DMA_IOVA, IOMMU_COOKIE_DMA_MSI, IOMMU_COOKIE_FAULT_HANDLER };
struct iommu_domain_ops { int id; };
struct iommu_domain {
    int cookie_type;
    const struct iommu_domain_ops *ops;
    union {
        void *iova_cookie;
        struct { iommu_fault_handler_t handler; void *handler_token; };
    };
};
struct arm_smmu_domain {
    struct iommu_domain domain;
    pthread_mutex_t fault_handler_lock;
    iommu_fault_handler_t fault_handler;
    void *fault_token;
};
struct arm_smmu_device { struct device *dev; u32 fsr, fsynr; unsigned long iova; };
struct qcom_iommu_ops { void (*set_fault_handler)(struct iommu_domain *, iommu_fault_handler_t, void *); };
static struct qcom_iommu_ops registered_ops;
static struct iommu_domain_ops domain_ops;
static struct qcom_iommu_ops *to_qcom_iommu_ops(const struct iommu_domain_ops *ops)
{ return ops == &domain_ops ? &registered_ops : NULL; }
#define to_smmu_domain(domain_ptr) ((struct arm_smmu_domain *)(domain_ptr))
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define trace_io_page_fault(dev, iova, flags) ((void)0)
#define ARM_SMMU_CB_FSR 1
#define ARM_SMMU_CB_FSYNR0 2
#define ARM_SMMU_CB_FAR 3
#define ARM_SMMU_FSYNR0_WNR (1U << 4)
#define ARM_SMMU_FSR_TF (1U << 1)
#define ARM_SMMU_FSR_PF (1U << 3)
#define ARM_SMMU_FSR_EF (1U << 4)
#define ARM_SMMU_FSR_SS (1U << 30)
#define IOMMU_FAULT_READ 0
#define IOMMU_FAULT_WRITE 1
#define IOMMU_FAULT_TRANSLATION (1U << 1)
#define IOMMU_FAULT_PERMISSION (1U << 2)
#define IOMMU_FAULT_EXTERNAL (1U << 3)
#define IOMMU_FAULT_TRANSACTION_STALLED (1U << 4)
static u32 arm_smmu_cb_read(struct arm_smmu_device *s, int idx, int reg)
{ (void)idx; assert(reg == ARM_SMMU_CB_FSR || reg == ARM_SMMU_CB_FSYNR0); return reg == ARM_SMMU_CB_FSR ? s->fsr : s->fsynr; }
static unsigned long arm_smmu_cb_readq(struct arm_smmu_device *s, int idx, int reg)
{ (void)idx; assert(reg == ARM_SMMU_CB_FAR); return s->iova; }
'''
tests = r'''
struct token { int calls, result, flags; bool wait; struct iommu_domain *domain; struct device *dev; unsigned long iova; };
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t entered_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t released_cond = PTHREAD_COND_INITIALIZER;
static bool entered, released;
static int record(struct iommu_domain *domain, struct device *dev, unsigned long iova, int flags, void *opaque)
{
    struct token *t = opaque;
    t->calls++; t->domain=domain; t->dev=dev; t->iova=iova; t->flags=flags;
    if (t->wait) {
        pthread_mutex_lock(&gate);
        entered = true; pthread_cond_broadcast(&entered_cond);
        while (!released) pthread_cond_wait(&released_cond, &gate);
        pthread_mutex_unlock(&gate);
    }
    return t->result;
}
static struct arm_smmu_domain smmu_domain;
static struct device dev = { .id=1 };
static struct arm_smmu_device smmu_device = { .dev=&dev, .fsr=ARM_SMMU_FSR_TF | ARM_SMMU_FSR_PF | ARM_SMMU_FSR_EF | ARM_SMMU_FSR_SS, .fsynr=ARM_SMMU_FSYNR0_WNR, .iova=0x123456789UL };
static atomic_bool clear_started, clear_done;
static void *fault_thread(void *unused)
{ (void)unused; assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -EBUSY); return NULL; }
static void *clear_thread(void *unused)
{ (void)unused; atomic_store(&clear_started, true); assert(qcom_iommu_set_fault_handler(&smmu_domain.domain, NULL, NULL) == 0); atomic_store(&clear_done, true); return NULL; }
int main(void)
{
    int marker;
    struct token private = { .result=-EBUSY };
    struct token core = { .result=-EINVAL };
    pthread_mutex_init(&smmu_domain.fault_handler_lock, NULL);
    smmu_domain.domain.cookie_type=IOMMU_COOKIE_DMA_IOVA;
    smmu_domain.domain.iova_cookie=&marker;
    smmu_domain.domain.ops=&domain_ops;
    registered_ops.set_fault_handler=arm_smmu_set_fault_handler;
    assert(qcom_iommu_set_fault_handler(NULL, record, &private) == -EINVAL);
    assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -ENOSYS);
    assert(qcom_iommu_set_fault_handler(&smmu_domain.domain, record, &private) == 0);
    assert(smmu_domain.domain.cookie_type == IOMMU_COOKIE_DMA_IOVA && smmu_domain.domain.iova_cookie == &marker);
    assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -EBUSY);
    assert(private.calls == 1 && private.domain == &smmu_domain.domain && private.dev == &dev && private.iova == 0x123456789UL);
    assert(private.flags == (IOMMU_FAULT_WRITE | IOMMU_FAULT_TRANSLATION | IOMMU_FAULT_PERMISSION | IOMMU_FAULT_EXTERNAL | IOMMU_FAULT_TRANSACTION_STALLED));
    private.result=0; assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == 0);
    private.result=-ENOSYS; assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -ENOSYS);
    private.result=-EINVAL; assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -EINVAL);
    assert(qcom_iommu_set_fault_handler(&smmu_domain.domain, NULL, NULL) == 0);
    assert(smmu_domain.domain.cookie_type == IOMMU_COOKIE_DMA_IOVA && smmu_domain.domain.iova_cookie == &marker);
    assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -ENOSYS);

    /* Legacy unmanaged core callbacks keep precedence and return semantics. */
    smmu_domain.domain.cookie_type=IOMMU_COOKIE_FAULT_HANDLER;
    smmu_domain.domain.handler=record; smmu_domain.domain.handler_token=&core;
    private.calls=0;
    assert(qcom_iommu_set_fault_handler(&smmu_domain.domain, record, &private) == 0);
    assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -EINVAL);
    assert(core.calls == 1 && private.calls == 0);
    core.result=-ENOSYS;
    assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -EINVAL);
    assert(core.calls == 2 && private.calls == 1);
    assert(qcom_iommu_set_fault_handler(&smmu_domain.domain, NULL, NULL) == 0);

    /* Clearing waits for an in-flight callback before its token may be freed. */
    smmu_domain.domain.cookie_type=IOMMU_COOKIE_DMA_IOVA;
    smmu_domain.domain.iova_cookie=&marker;
    private.result=-EBUSY; private.wait=true;
    assert(qcom_iommu_set_fault_handler(&smmu_domain.domain, record, &private) == 0);
    pthread_t fault, clear;
    assert(!pthread_create(&fault, NULL, fault_thread, NULL));
    pthread_mutex_lock(&gate);
    while (!entered) pthread_cond_wait(&entered_cond, &gate);
    pthread_mutex_unlock(&gate);
    assert(!pthread_create(&clear, NULL, clear_thread, NULL));
    while (!atomic_load(&clear_started)) usleep(1000);
    usleep(20000);
    assert(!atomic_load(&clear_done));
    pthread_mutex_lock(&gate); released=true; pthread_cond_broadcast(&released_cond); pthread_mutex_unlock(&gate);
    pthread_join(fault, NULL); pthread_join(clear, NULL);
    assert(atomic_load(&clear_done) && !smmu_domain.fault_handler && !smmu_domain.fault_token);
    int calls=private.calls;
    assert(report_iommu_fault_helper(&smmu_domain, &smmu_device, 2) == -ENOSYS && private.calls == calls);
    assert(smmu_domain.domain.iova_cookie == &marker);
    pthread_mutex_destroy(&smmu_domain.fault_handler_lock);
    puts("PASS: real-source cookie preservation, callback args/return forwarding, core precedence, synchronized withdrawal");
    return 0;
}
'''
source = adapter + '\n' + functions + '\n' + tests
Path('/tmp/onyx-iommu-fault-dispatch-test.c').write_text(source)
with tempfile.TemporaryDirectory(prefix='onyx-iommu-test-') as directory:
    binary = Path(directory)/'fault-dispatch-test'
    compile_cmd = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '/tmp/onyx-iommu-fault-dispatch-test.c', '-o', str(binary)]
    subprocess.run(compile_cmd, check=True)
    result = subprocess.run([str(binary)], check=True, text=True, capture_output=True)
    print(result.stdout, end='')
    Path('/tmp/onyx-iommu-dispatch-validation.json').write_text(json.dumps({'status':'PASS','source_functions':['report_iommu_fault','report_iommu_fault_helper','arm_smmu_set_fault_handler','qcom_iommu_set_fault_handler'],'compiler_flags':compile_cmd[1:-3],'output':result.stdout,'scope':'actual extracted dispatch/setter functions with host register/mutex adapters; no hardware fault injection'},indent=2)+'\n')
