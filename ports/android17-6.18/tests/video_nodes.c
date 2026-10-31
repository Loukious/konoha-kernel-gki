#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_MEDIA_CONTROLLER 1
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define d_vpr_h(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define PTR_ERR(p) ((intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define BASE_DEVICE_NUMBER 32
#define MEDIA_ENT_F_PROC_VIDEO_DECODER 0
#define MEDIA_ENT_F_PROC_VIDEO_ENCODER 1
#define DEVICE_CAPS 0
#define VFL_DIR_M2M 1
#define VFL_TYPE_VIDEO 1
#define atomic_read(p) atomic_load(p)
enum msm_vidc_domain_type { MSM_VIDC_DECODER, MSM_VIDC_ENCODER };
struct completion { atomic_uint done; };
struct device { int dummy; };
struct platform_device { struct device dev; };
struct media_device { struct device *dev; char model[32]; void *ops; bool init, registered; };
struct v4l2_device { struct media_device *mdev; bool registered; };
struct v4l2_m2m_dev { bool registered; };
struct video_device { void (*release)(struct video_device *); void *fops, *ioctl_ops; int vfl_dir; struct v4l2_device *v4l2_dev; int device_caps; void *drvdata; atomic_int refs; bool published; };
/* LAYOUT */
struct workqueue_struct { bool flushed; };
struct msm_vidc_core {
    struct platform_device *pdev;
    struct msm_video_device vdev[2];
    struct v4l2_device v4l2_dev;
    struct media_device media_dev;
    struct { int value; } capabilities[1];
    void *v4l2_file_ops, *v4l2_ioctl_ops_dec, *v4l2_ioctl_ops_enc, *v4l2_m2m_ops, *media_device_ops;
    int instances_wait;
    atomic_int live_instances;
    struct workqueue_struct *release_workq;
};
static int fail, allocations, video_calls, m2m_calls, mc_calls;
static bool tearing_down;
static struct msm_vidc_core *current;
static atomic_int unpublished, waiting_instances, waiting_video, finished;
static void init_completion(struct completion *c) { atomic_init(&c->done,0); }
static void complete_all(struct completion *c) { atomic_store(&c->done,UINT_MAX); }
static void complete(struct completion *c) { atomic_fetch_add(&c->done,1); }
static void wait_for_completion(struct completion *c) { while (!atomic_load(&c->done)) { atomic_store(&waiting_video,1); sched_yield(); } if (atomic_load(&c->done)!=UINT_MAX) atomic_fetch_sub(&c->done,1); }
#define wait_event(q,c) do { while (!(c)) { atomic_store(&waiting_instances,1); sched_yield(); } } while(0)
static void flush_workqueue(struct workqueue_struct *q) { assert(!atomic_load(&current->live_instances)); q->flushed=true; }
static void video_set_drvdata(struct video_device *v, void *data) { v->drvdata=data; }
static int video_register_device(struct video_device *v, int type, int nr)
{
    assert(v->drvdata==current && v->release && v->v4l2_dev->registered);
    if (fail == 2 + video_calls++ * 3) return -EIO;
    atomic_store(&v->refs,1); v->published=true; return 0;
}
static void video_put(struct video_device *v) { assert(atomic_load(&v->refs)>0); if (atomic_fetch_sub(&v->refs,1)==1) v->release(v); }
static void video_unregister_device(struct video_device *v) { assert(v->published); v->published=false; video_put(v); atomic_fetch_add(&unpublished,1); }
static struct v4l2_m2m_dev *v4l2_m2m_init(void *ops)
{
    if (fail == 3 + m2m_calls++ * 3) return ERR_PTR(-EIO);
    allocations++; return calloc(1,sizeof(struct v4l2_m2m_dev));
}
static int v4l2_m2m_register_media_controller(struct v4l2_m2m_dev *m, struct video_device *v, int i)
{
    if (fail == 4 + mc_calls++ * 3) return -EIO;
    m->registered=true; return 0;
}
static void v4l2_m2m_unregister_media_controller(struct v4l2_m2m_dev *m) { assert(m->registered); m->registered=false; }
static void v4l2_m2m_release(struct v4l2_m2m_dev *m)
{
    assert(m && !IS_ERR(m) && !m->registered);
    if (tearing_down) {
        assert(!atomic_load(&current->vdev[0].vdev.refs) && !atomic_load(&current->vdev[1].vdev.refs));
        assert(!atomic_load(&current->live_instances) && current->release_workq->flushed);
    }
    allocations--; free(m);
}
static int v4l2_device_register(struct device *d, struct v4l2_device *v) { if (fail==1) return -EIO; assert(!v->registered); v->registered=true; return 0; }
static void v4l2_device_unregister(struct v4l2_device *v) { assert(v->registered && !allocations); assert(!atomic_load(&current->vdev[0].vdev.refs) && !atomic_load(&current->vdev[1].vdev.refs)); v->registered=false; }
static void media_device_init(struct media_device *m) { assert(!m->init); m->init=true; }
static int media_device_register(struct media_device *m) { assert(m->init); if (fail==8) return -EIO; m->registered=true; return 0; }
static void media_device_unregister(struct media_device *m) { assert(m->registered); m->registered=false; }
static void media_device_cleanup(struct media_device *m) { assert(m->init && !m->registered); m->init=false; }
#define strscpy(d,s,n) snprintf(d,n,"%s",s)
/* SOURCE */
static void *teardown(void *arg) { assert(!msm_vidc_deinitialize_media(arg)); atomic_store(&finished,1); return NULL; }
static void reset(struct msm_vidc_core *core)
{
    current=core; video_calls=m2m_calls=mc_calls=0; atomic_store(&unpublished,0); atomic_store(&waiting_instances,0); atomic_store(&waiting_video,0); atomic_store(&finished,0); core->release_workq->flushed=false;
}
int main(void)
{
    struct platform_device pdev={0}; struct workqueue_struct queue={0}; struct msm_vidc_core core={.pdev=&pdev,.release_workq=&queue};
    for (fail=1;fail<=8;fail++) {
        reset(&core);
        assert(msm_vidc_initialize_media(&core)==-EIO);
        assert(!core.v4l2_dev.registered && !core.media_dev.init && !allocations);
        assert(!atomic_load(&core.vdev[0].vdev.refs) && !atomic_load(&core.vdev[1].vdev.refs));
    }
    fail=0;
    for (int cycle=0;cycle<2;cycle++) {
        reset(&core); assert(!msm_vidc_initialize_media(&core)); assert(allocations==2);
        tearing_down=true;
        atomic_fetch_add(&core.vdev[0].vdev.refs,1); atomic_fetch_add(&core.vdev[1].vdev.refs,1);
        atomic_store(&core.live_instances,cycle);
        pthread_t thread; assert(!pthread_create(&thread,NULL,teardown,&core));
        while (atomic_load(&unpublished)<2) sched_yield();
        while (!atomic_load(&waiting_video) && !atomic_load(&finished)) sched_yield();
        assert(!atomic_load(&finished) && core.v4l2_dev.registered && allocations==2);
        video_put(&core.vdev[0].vdev);
        assert(!atomic_load(&finished)); video_put(&core.vdev[1].vdev);
        if (cycle) {
            while (!atomic_load(&waiting_instances) && !atomic_load(&finished)) sched_yield();
            assert(!atomic_load(&finished) && allocations==2);
            atomic_store(&core.live_instances,0);
        }
        pthread_join(thread,NULL);
        assert(atomic_load(&finished) && !allocations && !core.media_dev.init && !core.v4l2_dev.registered);
        tearing_down=false;
    }
    puts("10 partial-registration, retained-file, instance-drain and rebind cases passed");
    return 0;
}
