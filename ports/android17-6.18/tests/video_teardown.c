#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define SUPPORTS_SYNX_FENCE 0
#define SW_PC 1
#define SW_PC_DELAY 2
#define FW_UNLOAD 3
#define FW_UNLOAD_DELAY 4
#define d_vpr_h(...) ((void)0)
#define d_vpr_l(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define READ_ONCE(p) (p)
#define WRITE_ONCE(p,v) ((p)=(v))
#define smp_store_release(p,v) (*(p)=(v))
#define msecs_to_jiffies(x) (x)
struct work { bool pending; };
struct msm_vidc_core { bool ready, stopping, synx_registered, irq_requested; int irq; struct { int value; } capabilities[5]; void *fence_ops, *debugfs_root, *pm_workq; struct work ssr_work, fw_unload_work, pm_work; pthread_mutex_t lock; };
struct device { struct msm_vidc_core *core; };
static struct msm_vidc_core *current;
static bool components, media, queues, hardware, debug, removing, fence;
static int fail, bind_result, msm_vidc_synx_fence_enable=1;
static atomic_int irq_active, permit_irq, syncing, finished;
static struct msm_vidc_core *dev_get_drvdata(struct device *d) { return d->core; }
static int component_bind_all(struct device *d,struct msm_vidc_core *c) { if(fail==1)return -EIO; components=true; return 0; }
static void component_unbind_all(struct device *d,struct msm_vidc_core *c) { assert(components && !media && !queues && !fence); components=false; }
static int fence_register(struct msm_vidc_core *c) { assert(components); if(fail==2)return -EIO; fence=true; return 0; }
static void fence_deregister(struct msm_vidc_core *c) { assert(fence && !queues && !media); fence=false; }
#define call_fence_op(c,op,...) op(__VA_ARGS__)
static void *get_dma_fence_ops(void) { return &fence; }
static int msm_vidc_initialize_media(struct msm_vidc_core *c) { if(fail==3)return -EIO; media=true; return 0; }
static void msm_vidc_deinitialize_media(struct msm_vidc_core *c) { assert(!c->ready && c->stopping && media); media=false; }
static int venus_hfi_queue_init(struct msm_vidc_core *c) { if(fail==4)return -EIO; queues=true; return 0; }
static int msm_vidc_core_init(struct msm_vidc_core *c) { assert(queues); if(fail==5) { atomic_store(&irq_active,1); c->pm_work.pending=true; return -EIO; } hardware=true; return 0; }
static void *msm_vidc_debugfs_init_core(struct msm_vidc_core *c) { debug=true; return &debug; }
static void debugfs_remove_recursive(void *p) { assert(debug && !media); debug=false; }
static void cancel_work_sync(struct work *w) { assert(!debug); w->pending=false; }
static void cancel_delayed_work_sync(struct work *w) { if(removing) assert(!debug && !current->ssr_work.pending); w->pending=false; }
static void msm_vidc_core_deinit(struct msm_vidc_core *c,bool force) { assert(force && !media && !debug && !c->ssr_work.pending && !c->fw_unload_work.pending); hardware=false; }
static void synchronize_irq(int irq) { assert(!hardware); atomic_store(&syncing,1); while(atomic_load(&irq_active)) sched_yield(); }
static void venus_hfi_queue_deinit(struct msm_vidc_core *c) { assert(!hardware && !atomic_load(&irq_active) && !c->pm_work.pending); queues=false; }
static void core_lock(struct msm_vidc_core *c,const char *name) { assert(!pthread_mutex_lock(&c->lock)); }
static void core_unlock(struct msm_vidc_core *c,const char *name) { assert(!pthread_mutex_unlock(&c->lock)); }
static int mod_delayed_work(void *q,struct work *w,int delay) { w->pending=true; return 1; }
static void cancel_delayed_work(struct work *w) { w->pending=false; }
static void schedule_delayed_work(struct work *w,int delay) { w->pending=true; }
/* SOURCE */
static void *bind_thread(void *arg) { bind_result=msm_vidc_component_master_bind(arg); atomic_store(&finished,1); return NULL; }
static void *remove_thread(void *arg) { msm_vidc_component_master_unbind(arg); atomic_store(&finished,1); return NULL; }
static void reset(struct msm_vidc_core *c)
{
    assert(!components && !media && !queues && !hardware && !debug && !fence);
    c->ready=false; c->stopping=true; c->synx_registered=false; c->irq_requested=true;
    c->ssr_work.pending=c->fw_unload_work.pending=c->pm_work.pending=false;
    atomic_store(&irq_active,0); atomic_store(&syncing,0); atomic_store(&finished,0); removing=false;
}
static void drain(struct device *d)
{
    struct msm_vidc_core *c=d->core;
    c->ssr_work.pending=c->fw_unload_work.pending=c->pm_work.pending=true;
    atomic_store(&irq_active,1); atomic_store(&syncing,0); atomic_store(&finished,0); removing=true;
    pthread_t thread; assert(!pthread_create(&thread,NULL,remove_thread,d));
    while(!atomic_load(&syncing) && !atomic_load(&finished))sched_yield();
    assert(!atomic_load(&finished) && queues);
    core_lock(c,"test"); __schedule_power_collapse_work(c); core_unlock(c,"test");
    atomic_store(&irq_active,0); pthread_join(thread,NULL);
    assert(!components && !media && !queues && !hardware && !debug && !fence && !c->ready && c->stopping);
    __schedule_power_collapse_work(c); msm_vidc_schedule_core_deinit(c);
    assert(!c->pm_work.pending && !c->fw_unload_work.pending);
}
int main(void)
{
    struct msm_vidc_core c={.lock=PTHREAD_MUTEX_INITIALIZER}; struct device d={.core=&c}; current=&c;
    for(int i=0;i<5;i++)c.capabilities[i].value=1;
    for(fail=1;fail<=3;fail++){reset(&c);assert(msm_vidc_component_master_bind(&d)==-EIO);assert(!components && !media && !queues && !fence && !c.ready && c.stopping);}
    for(int error=4;error<=5;error++){
        fail=error;
        reset(&c);
        if(fail==4) { assert(!msm_vidc_component_master_bind(&d)); }
        else {
            pthread_t thread; assert(!pthread_create(&thread,NULL,bind_thread,&d));
            while(!atomic_load(&syncing) && !atomic_load(&finished))sched_yield();
            assert(!atomic_load(&finished) && queues);
            __schedule_power_collapse_work(&c);
            atomic_store(&irq_active,0); pthread_join(thread,NULL); assert(!bind_result);
        }
        assert(c.ready && !c.stopping && !queues && !hardware);
        /* Native retry on session-open remains possible after initial failure. */
        fail=0; assert(!venus_hfi_queue_init(&c) && !msm_vidc_core_init(&c)); drain(&d);
    }
    for(int cycle=0;cycle<2;cycle++){reset(&c);fail=0;assert(!msm_vidc_component_master_bind(&d));assert(c.ready && queues && hardware);__schedule_power_collapse_work(&c);msm_vidc_schedule_core_deinit(&c);assert(c.pm_work.pending && c.fw_unload_work.pending);drain(&d);}
    puts("7 bind-failure, retry, IRQ-drain, work-drain and rebind cases passed");
    return 0;
}
