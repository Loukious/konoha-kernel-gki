#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
typedef uint64_t u64;typedef uint32_t u32;
#define MAX_CRTCS 16
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define DISP_DEV_ERR(...) ((void)0)
#define SDE_ERROR(...) ((void)0)
struct drm_device {int dummy;};struct device {int dummy;};struct drm_crtc {struct {int id;}base;};struct task_struct {int live;};struct kthread_worker {struct task_struct *task;};struct kthread_work {void(*fn)(struct kthread_work*);};
struct msm_drm_thread {unsigned crtc_id;struct drm_device *dev;struct task_struct *thread;struct kthread_worker worker;};
struct msm_drm_private {unsigned num_crtcs;struct drm_crtc *crtcs[MAX_CRTCS];struct msm_drm_thread disp_thread[MAX_CRTCS],event_thread[MAX_CRTCS];struct kthread_worker pp_event_worker;struct task_struct *pp_event_thread;struct kthread_work thread_priority_work;};
static int fail_at,step,live;
static int kthread_worker_fn(void *p){(void)p;return 0;}
static void kthread_init_worker(struct kthread_worker *p) {p->task=NULL;}
#define kthread_run(fn,p,...) make_thread(p)
static struct task_struct *make_thread(struct kthread_worker *p) {if(++step==fail_at)return ERR_PTR(-EIO);p->task=malloc(sizeof(*p->task));assert(p->task);p->task->live=1;live++;return p->task;}
static void kthread_stop(struct task_struct *p) {assert(p&&p->live);live--;free(p);}
static void msm_drm_display_thread_priority_worker(struct kthread_work *p){(void)p;}
static void kthread_init_work(struct kthread_work *p,void(*fn)(struct kthread_work*)) {p->fn=fn;}
static bool kthread_queue_work(struct kthread_worker *p,struct kthread_work *w){assert(p->task&&!IS_ERR(p->task)&&p->task->live);w->fn(w);return true;}
static void kthread_flush_work(struct kthread_work *p){(void)p;}
struct dma_fence {int fd;};static struct dma_fence sync_fence;static struct dma_fence *sync_file_get_fence(int fd){assert(fd>=0);sync_fence.fd=fd;return &sync_fence;}
struct notifier_block {int unused;};static struct notifier_block sde_md_notify_blk;static bool sde_md_registered;static int register_error,registers,unregisters;
static int qcom_va_md_register(const char*n,struct notifier_block *p){assert(!strcmp(n,"display")&&p);registers++;return register_error;}
static void qcom_va_md_unregister(const char*n,struct notifier_block *p){assert(!strcmp(n,"display")&&p);unregisters++;}
struct va_md_entry {char owner[32];uintptr_t vaddr;uint64_t size;void(*cb)(void);};static int regions;
#define strscpy(dst,src,n) snprintf(dst,n,"%s",src)
static int qcom_va_md_add_region(struct va_md_entry *p){assert(!p->cb&&p->vaddr&&p->size==123&& !strcmp(p->owner,"test"));regions++;return 0;}
/* FUNCTIONS */
int main(void) {
 int faults=0,successes=0;struct drm_device dev={0};struct device pdev={0};struct drm_crtc crtc[MAX_CRTCS];for(int i=0;i<MAX_CRTCS;i++)crtc[i].base.id=i;
 for(unsigned n=0;n<=MAX_CRTCS;n++)for(int fault=0;fault<=2*(int)n+1;fault++){
  struct msm_drm_private p={.num_crtcs=n};for(unsigned i=0;i<n;i++)p.crtcs[i]=&crtc[i];step=0;fail_at=fault;int r=msm_drm_display_thread_create(&p,&dev,&pdev);
  if(fault){assert(r==-EIO&&!live);for(unsigned i=0;i<n;i++)assert(!p.disp_thread[i].thread&&!p.event_thread[i].thread);assert(!p.pp_event_thread);faults++;}
  else {assert(!r&&live==2*(int)n+1);for(unsigned i=0;i<n;i++){kthread_stop(p.disp_thread[i].thread);kthread_stop(p.event_thread[i].thread);}kthread_stop(p.pp_event_thread);assert(!live);successes++;}
 }
 uint64_t values[]={0,1,2147483647,2147483648,4294967296,4294967297,UINT64_MAX};for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)assert((sde_sync_get(values[i])!=NULL)==(values[i]<=2147483647));
 register_error=-EIO;assert(sde_register_md_panic_notifer()==-EIO);sde_unregister_md_panic_notifier();assert(!unregisters);register_error=0;assert(!sde_register_md_panic_notifer());sde_unregister_md_panic_notifier();sde_unregister_md_panic_notifier();assert(registers==2&&unregisters==1&&!sde_md_registered);sde_mini_dump_add_va_region("test",123,&dev);assert(regions==1);
 printf("{\"display_worker_failure_cases\":%d,\"display_worker_success_cases\":%d,\"sync_fd_cases\":7,\"minidump_registration_lifetime\":true,\"minidump_zeroed_entry\":true}\n",faults,successes);
}
