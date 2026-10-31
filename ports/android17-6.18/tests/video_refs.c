#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

struct kref { atomic_int value; };
typedef pthread_mutex_t spinlock_t;
struct waitqueue { pthread_mutex_t lock; pthread_cond_t cond; };
struct work_struct { void (*fn)(struct work_struct *); };
struct workqueue_struct { pthread_t thread; bool queued; };
struct msm_vidc_core { struct workqueue_struct *release_workq; atomic_int live_instances; struct waitqueue instances_wait; };
struct msm_vidc_inst {
    struct msm_vidc_core *core;
    struct kref kref;
    spinlock_t ref_lock;
    struct waitqueue ref_wait;
    struct work_struct release_work;
    void *debugfs_root, *debugfs_data;
    struct workqueue_struct *workq;
    pthread_mutex_t lock, ctx_q_lock, client_lock;
    int domain, removed, dangling;
    bool partial;
};
static atomic_int debug_active, debug_removal, freed, worker_lock, waiting, hold_put, permit_put;
static struct msm_vidc_inst *current;
static void *debug_allocation;
static _Thread_local bool worker;
static bool delay_put;
static pthread_mutex_t debug_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t debug_cond = PTHREAD_COND_INITIALIZER;
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define i_vpr_h(...) ((void)0)
#define atomic_inc(p) ((void)atomic_fetch_add(p, 1))
#define atomic_dec_and_test(p) (atomic_fetch_sub(p, 1) == 1)
#define spin_lock_init(p) assert(!pthread_mutex_init(p, NULL))
static void lock_ref(spinlock_t *lock)
{
    if (worker) atomic_store(&worker_lock, 1);
    assert(!pthread_mutex_lock(lock));
}
#define spin_lock_irqsave(p,f) do { (f)=0; lock_ref(p); } while (0)
#define spin_unlock_irqrestore(p,f) assert(!pthread_mutex_unlock(p))
#define mutex_destroy(p) assert(!pthread_mutex_destroy(p))
static void init_waitqueue_head(struct waitqueue *q) { assert(!pthread_mutex_init(&q->lock, NULL)); assert(!pthread_cond_init(&q->cond, NULL)); }
static void wake_up_all(struct waitqueue *q)
{
    if (delay_put && current && q == &current->ref_wait && atomic_load(&current->kref.value) == 0) {
        atomic_store(&hold_put, 1);
        while (!atomic_load(&permit_put)) sched_yield();
    }
    assert(!pthread_mutex_lock(&q->lock)); assert(!pthread_cond_broadcast(&q->cond)); assert(!pthread_mutex_unlock(&q->lock));
}
#define wait_event(q,c) do { \
    assert(!pthread_mutex_lock(&(q).lock)); \
    while (!(c)) { atomic_store(&waiting, 1); assert(!pthread_cond_wait(&(q).cond, &(q).lock)); } \
    assert(!pthread_mutex_unlock(&(q).lock)); \
} while (0)
static void kref_init(struct kref *ref) { atomic_init(&ref->value, 1); }
static int kref_read(struct kref *ref) { return atomic_load(&ref->value); }
static bool kref_get_unless_zero(struct kref *ref) { if (!atomic_load(&ref->value)) return false; atomic_fetch_add(&ref->value, 1); return true; }
static void kref_put(struct kref *ref, void (*release)(struct kref *)) { int before = atomic_fetch_sub(&ref->value, 1); assert(before > 0); if (before == 1) release(ref); }
#define INIT_WORK(w,f) ((w)->fn=(f))
static void *run_work(void *arg) { struct work_struct *work = arg; worker = true; work->fn(work); return NULL; }
static void queue_work(struct workqueue_struct *q, struct work_struct *work) { assert(!q->queued); q->queued=true; assert(!pthread_create(&q->thread,NULL,run_work,work)); }
static void debugfs_remove_recursive(void *root)
{
    assert(worker); atomic_store(&debug_removal, 1);
    assert(!pthread_mutex_lock(&debug_lock));
    while (atomic_load(&debug_active)) assert(!pthread_cond_wait(&debug_cond, &debug_lock));
    assert(!pthread_mutex_unlock(&debug_lock));
}
static void vfree(void *ptr)
{
    if (ptr == debug_allocation) { assert(!atomic_load(&debug_active)); debug_allocation=NULL; }
    if (ptr == current) {
        assert(current->removed && current->dangling);
        assert(!debug_allocation);
        assert(current->partial ? kref_read(&current->kref)==1 : kref_read(&current->kref)==0);
        atomic_fetch_add(&freed,1);
    }
    free(ptr);
}
static int is_decode_session(struct msm_vidc_inst *i) { return i->domain == 0; }
static int is_encode_session(struct msm_vidc_inst *i) { return i->domain == 1; }
static void msm_vidc_fence_deinit(struct msm_vidc_inst *i) { assert(!i->debugfs_root && !i->debugfs_data); }
static void msm_vdec_inst_deinit(struct msm_vidc_inst *i) { assert(is_decode_session(i)); }
static void msm_venc_inst_deinit(struct msm_vidc_inst *i) { assert(is_encode_session(i)); }
static void inst_lock(struct msm_vidc_inst *i, const char *name) { assert(!pthread_mutex_lock(&i->lock)); }
static void inst_unlock(struct msm_vidc_inst *i, const char *name) { assert(!pthread_mutex_unlock(&i->lock)); }
static void msm_vidc_vb2_queue_deinit(struct msm_vidc_inst *i) { assert(!i->debugfs_data); }
static void msm_vidc_v4l2_fh_deinit(struct msm_vidc_inst *i) { assert(!i->debugfs_data); }
static void destroy_workqueue(struct workqueue_struct *q) { assert(q && !q->queued); }
static void msm_vidc_destroy_buffers(struct msm_vidc_inst *i) { }
static void msm_vidc_remove_session(struct msm_vidc_inst *i) { i->removed = 1; }
static void msm_vidc_remove_dangling_session(struct msm_vidc_inst *i) { i->dangling = 1; }
/* SOURCE */
static struct msm_vidc_inst *create(struct msm_vidc_core *c, bool partial, int domain)
{
    struct msm_vidc_inst *i = calloc(1, sizeof(*i)); assert(i);
    current=i; i->core=c; i->partial=partial; i->domain=domain;
    pthread_mutex_init(&i->lock,NULL); pthread_mutex_init(&i->ctx_q_lock,NULL); pthread_mutex_init(&i->client_lock,NULL);
    msm_vidc_init_inst_ref(i); assert(atomic_load(&c->live_instances)==1);
    if (!partial) { i->debugfs_data=calloc(1,32); debug_allocation=i->debugfs_data; i->debugfs_root=i; i->workq=calloc(1,sizeof(*i->workq)); }
    atomic_store(&debug_removal,0); atomic_store(&worker_lock,0); atomic_store(&freed,0); atomic_store(&waiting,0);
    return i;
}
static void *drain_partial(void *arg)
{
    struct msm_vidc_inst *i=arg;
    msm_vidc_drain_inst_refs(i);
    assert(kref_read(&i->kref)==1); assert(i->removed && i->dangling);
    msm_vidc_free_inst(i);
    return NULL;
}
static void *final_put(void *arg) { put_inst(arg); return NULL; }
int main(void)
{
    struct workqueue_struct release={0}; struct msm_vidc_core core={.release_workq=&release}; init_waitqueue_head(&core.instances_wait);
    struct msm_vidc_inst *i=create(&core,true,0);
    assert(get_inst_ref_locked(i)==i && kref_read(&i->kref)==2);
    pthread_t thread; assert(!pthread_create(&thread,NULL,drain_partial,i));
    while (!atomic_load(&waiting) && !atomic_load(&freed)) sched_yield();
    assert(!atomic_load(&freed)); put_inst(i); pthread_join(thread,NULL);
    assert(atomic_load(&freed)==1 && !atomic_load(&core.live_instances)); current=NULL;
    for (int domain=0;domain<2;domain++) {
        release.queued=false; i=create(&core,false,domain); struct workqueue_struct *instance_queue=i->workq;
        atomic_store(&debug_active,1);
        put_inst(i);
        while (!atomic_load(&debug_removal) && !atomic_load(&freed)) sched_yield();
        assert(!atomic_load(&freed));
        assert(!pthread_mutex_lock(&debug_lock)); atomic_store(&debug_active,0); pthread_cond_broadcast(&debug_cond); pthread_mutex_unlock(&debug_lock);
        pthread_join(release.thread,NULL); free(instance_queue);
        assert(atomic_load(&freed)==1 && !atomic_load(&core.live_instances)); current=NULL;
    }
    release.queued=false; i=create(&core,false,0); struct workqueue_struct *instance_queue=i->workq;
    delay_put=true;
    assert(!pthread_create(&thread,NULL,final_put,i));
    while (!atomic_load(&hold_put) || (!atomic_load(&worker_lock) && !atomic_load(&freed))) sched_yield();
    assert(!atomic_load(&freed)); assert(!atomic_load(&debug_removal));
    atomic_store(&permit_put,1); pthread_join(thread,NULL); pthread_join(release.thread,NULL); free(instance_queue);
    assert(atomic_load(&freed)==1 && !atomic_load(&core.live_instances)); current=NULL;
    puts("4 instance drain, deferred debugfs release and final-put ordering cases passed");
    return 0;
}
