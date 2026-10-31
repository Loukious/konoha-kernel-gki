#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#define __init
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define GFP_KERNEL 0
#define WQ_HIGHPRI 1
#define WQ_UNBOUND 2
#define SYNX_WQ_CB_NAME "cb"
#define SYNX_WQ_CLEANUP_NAME "cleanup"
#define SYNX_WQ_CB_THREADS 2
#define SYNX_WQ_CLEANUP_THREADS 2
#define SYNX_DEVICE_NAME "synx"
#define THIS_MODULE ((void*)1)
#define IS_ENABLED(x) 1
#define dprintk(...) ((void)0)
#define smp_store_release(p,v) (*(p)=(v))
#define mutex_init(p) (*(p)=1)
#define INIT_LIST_HEAD(p) (*(p)=1)
struct kobject { int type; };
struct cdev { struct kobject kobj;void *owner,*ops; };
struct device { int value; };
struct native { int initialized; };
struct notifier_block { void (*notifier_call)(void); };
struct synx_device {
 int vtbl_lock,error_lock,error_list;
 void *wq_cb,*wq_cleanup;struct native *native;
 dev_t dev;struct cdev *cdev;void *class,*debugfs_root;
 struct {struct notifier_block nb;void *handle;} cdsp_ssr;
};
static struct synx_device *synx_dev;
static bool synx_ready;
static int step,fail_at,allocated,global_live,region_live,cdev_live,notifier_live,client_live,pins;
static int synx_fops,synx_hwfence_ops;
static struct device *device_live;
static struct {void (*share_handle_status)(void);void (*get_fence)(void);} synx_shared_ops,hwfence_shared_ops;
static bool fail(void){return ++step==fail_at;}
static void *allocate(size_t n){if(fail())return NULL;void *p=calloc(1,n);assert(p);allocated++;return p;}
static void release(void *p){if(p){assert(!IS_ERR(p));allocated--;free(p);}}
#define kzalloc(n,g) allocate(n)
#define vzalloc(n) allocate(n)
#define kfree(p) release(p)
#define vfree(p) release(p)
static void *alloc_workqueue(const char *n,int flags,int threads){return allocate(1);}
static void destroy_workqueue(void *p){release(p);}
static int synx_local_mem_init(void){if(fail())return -ENOMEM;synx_dev->native->initialized=1;return 0;}
static int synx_global_mem_init(void){if(fail())return -ENOMEM;assert(synx_dev->native->initialized);global_live=1;return 0;}
static void synx_global_mem_close(void){assert(global_live);global_live=0;}
static int alloc_chrdev_region(dev_t *d,int first,int count,const char *name){if(fail())return -ENOMEM;assert(!region_live);region_live=1;*d=77;return 0;}
static void unregister_chrdev_region(dev_t d,int count){assert(region_live&&d==77);region_live=0;}
static struct cdev *cdev_alloc(void){return allocate(sizeof(struct cdev));}
static int cdev_add(struct cdev *c,dev_t d,int n){if(fail())return -ENOMEM;cdev_live=1;return 0;}
static void kobject_put(struct kobject *k){release(k);}
static void cdev_del(struct cdev *c){assert(cdev_live);cdev_live=0;release(c);}
static void *class_create(const char *name){void *p=allocate(1);return p?p:ERR_PTR(-ENOMEM);}
static void class_destroy(void *p){release(p);}
static struct device *device_create(void *class,void *parent,dev_t d,void *data,const char *name){device_live=allocate(sizeof(struct device));return device_live?device_live:ERR_PTR(-ENOMEM);}
static void device_destroy(void *class,dev_t d){assert(device_live);release(device_live);device_live=NULL;}
static void synx_cdsp_restart_notifier(void){}
static void *qcom_register_ssr_notifier(const char *name,struct notifier_block *nb){if(fail())return ERR_PTR(-ENOMEM);assert(global_live&&synx_dev->native->initialized);notifier_live=1;return (void*)2;}
static void qcom_unregister_ssr_notifier(void *h,struct notifier_block *nb){assert(notifier_live);notifier_live=0;}
static void synx_ipc_callback(void){}
static int ipclite_register_client(void (*cb)(void),void *p){if(fail())return -EBUSY;assert(notifier_live&&global_live&&synx_dev->native->initialized);client_live=1;return 0;}
static void __module_get(void *p){assert(client_live);pins++;}
static int synx_hwfence_init_ops(void *p){return -EOPNOTSUPP;}
static void synx_internal_share_handle_status(void){}
static void synx_internal_get_dma_fence(void){}
static int synx_hwfence_init_interops(void *a,void *b){return 0;}
static void *synx_init_debugfs_dir(struct synx_device *d){return NULL;}
/* STAGED_DEFINITIONS */
static void reset_success(void){
 qcom_unregister_ssr_notifier(synx_dev->cdsp_ssr.handle,&synx_dev->cdsp_ssr.nb);
 device_destroy(synx_dev->class,synx_dev->dev);class_destroy(synx_dev->class);cdev_del(synx_dev->cdev);
 unregister_chrdev_region(synx_dev->dev,1);synx_global_mem_close();
 destroy_workqueue(synx_dev->wq_cb);destroy_workqueue(synx_dev->wq_cleanup);release(synx_dev->native);release(synx_dev);synx_dev=NULL;
 client_live=0;pins=0;synx_ready=false;
}
int main(void){
 unsigned cases=0;
 for(unsigned round=0;round<200;round++)for(fail_at=0;fail_at<=13;fail_at++){
  step=0;assert(!allocated&&!global_live&&!region_live&&!cdev_live&&!notifier_live&&!client_live&&!pins&&!synx_ready);
  int ret=synx_init();
  if(ret){assert(!synx_dev&&!allocated&&!global_live&&!region_live&&!cdev_live&&!notifier_live&&!client_live&&!pins&&!synx_ready);}
  else{assert(synx_ready&&client_live&&pins==1);reset_success();}
  cases++;
 }
 printf("{\"synx_init_failure_cases\":%u}\n",cases);
}
