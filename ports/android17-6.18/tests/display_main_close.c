#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
typedef uint32_t u32;typedef int spinlock_t;
struct list_head {int dummy;};struct inode {int dummy;};struct drm_device;struct drm_minor {struct drm_device *dev;};struct drm_file {struct drm_minor *minor;};struct file {void *private_data;};
struct msm_drm_private {struct list_head client_event_list;unsigned pending_crtcs;int pending_crtcs_event;};struct drm_device {struct msm_drm_private *dev_private;_Atomic int open_count;int event_lock;};
struct msm_drm_event {struct {struct list_head link;struct drm_file *file_priv;}base;struct {struct {unsigned type;}base;struct {unsigned event,object_id;}info;}event;};
#define LIST_HEAD(name) struct list_head name={0}
#define list_for_each_entry_safe(p,n,h,m) for(p=NULL,n=NULL;p;p=NULL)
#define list_for_each_entry(p,h,m) for(p=NULL;p;p=NULL)
#define list_del(p) ((void)(p))
#define list_add_tail(p,h) ((void)(p),(void)(h))
#define spin_lock_irqsave(p,f) ((void)(p),f=0)
#define spin_unlock_irqrestore(p,f) ((void)(p),(void)(f))
#define mutex_lock(p) pthread_mutex_lock(p)
#define mutex_unlock(p) pthread_mutex_unlock(p)
#define atomic_read(p) atomic_load(p)
#define msm_wait_event_timeout(w,p,t,r) (r=1)
#define LASTCLOSE_TIMEOUT_MS 1
#define DRM_INFO(...) ((void)0)
#define kfree(p) free(p)
static pthread_mutex_t msm_release_lock=PTHREAD_MUTEX_INITIALIZER,gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;static struct msm_drm_private private;static struct drm_device device={.dev_private=&private};static struct drm_minor minor={&device};static int references,lastcloses,precloses,hold,entered,unblock,open_started,open_done;
static int drm_open(struct inode*i,struct file*f){(void)i;struct drm_file*p=calloc(1,sizeof(*p));assert(p);p->minor=&minor;f->private_data=p;references++;atomic_fetch_add(&device.open_count,1);return 0;}
static void drm_dev_get(struct drm_device*d){assert(d==&device&&references>0);references++;}
static void drm_dev_put(struct drm_device*d){assert(d==&device&&references>0);references--;}
static int drm_release(struct inode*i,struct file*f){(void)i;free(f->private_data);atomic_fetch_sub(&device.open_count,1);assert(references>0);references--;return 0;}
static bool drm_is_current_master(struct drm_file*f){(void)f;return true;}
static void msm_preclose(struct drm_device*d,struct drm_file*f){assert(d&&f);precloses++;}
static u32 msm_event_client_count(struct drm_device*d,void*p,bool b){(void)d;(void)p;(void)b;return 0;}
static void msm_register_event(struct drm_device*d,void*p,struct drm_file*f,bool b){(void)d;(void)p;(void)f;(void)b;}
static void msm_lastclose(struct drm_device*d){assert(d==&device&&references>0&&!atomic_read(&device.open_count));lastcloses++;pthread_mutex_lock(&gate);if(hold){entered=1;pthread_cond_broadcast(&cond);while(!unblock)pthread_cond_wait(&cond,&gate);}pthread_mutex_unlock(&gate);}
/* FUNCTIONS */
static struct inode node;static struct file race_old,race_new;
static void *close_thread(void*p){(void)p;assert(!msm_release(&node,&race_old));return NULL;}
static void *open_thread(void*p){(void)p;pthread_mutex_lock(&gate);open_started=1;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&gate);assert(!msm_fops_open(&node,&race_new));pthread_mutex_lock(&gate);open_done=1;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&gate);return NULL;}
int main(void){
 struct file a={0},b={0};assert(!msm_fops_open(&node,&a)&&!msm_fops_open(&node,&b));assert(references==2&&atomic_read(&device.open_count)==2);assert(!msm_release(&node,&a)&&!lastcloses);assert(!msm_release(&node,&b)&&lastcloses==1&&references==0);assert(msm_release(&node,&a)==-EINVAL);assert(precloses==2);
 references=1;assert(!msm_fops_open(&node,&race_old));hold=1;pthread_t closer,opener;assert(!pthread_create(&closer,NULL,close_thread,NULL));pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);assert(!pthread_create(&opener,NULL,open_thread,NULL));pthread_mutex_lock(&gate);while(!open_started)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);struct timespec pause={.tv_nsec=10000000};nanosleep(&pause,NULL);pthread_mutex_lock(&gate);assert(!open_done);unblock=1;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&gate);pthread_join(closer,NULL);pthread_join(opener,NULL);assert(open_done&&atomic_read(&device.open_count)==1&&references==2);hold=0;assert(!msm_release(&node,&race_new));assert(references==1&&lastcloses==3);
 puts("{\"last_close_device_reference\":true,\"open_waits_for_last_close\":true,\"multiple_file_close_cases\":3}");
}
