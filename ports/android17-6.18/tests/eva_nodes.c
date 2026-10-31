#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define CLASS_NAME "cvp"
#define DRIVER_NAME "cvp"
#define VM_TRUSTED 1
#define THIS_MODULE ((void*)1)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define dprintk(...) ((void)0)
struct mutex {pthread_mutex_t m;};
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->m,NULL));}
static void mutex_destroy(struct mutex *m){assert(!pthread_mutex_destroy(&m->m));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->m));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->m));}
typedef atomic_int refcount_t;
static void refcount_set(refcount_t *r,int x){atomic_store(r,x);}
static void refcount_inc(refcount_t *r){assert(atomic_fetch_add(r,1)>0);}
static bool refcount_dec_and_test(refcount_t *r){int old=atomic_fetch_sub(r,1);assert(old>0);return old==1;}
struct completion {atomic_bool done;};static void init_completion(struct completion *c){atomic_init(&c->done,false);}
static void complete(struct completion *c){atomic_store(&c->done,true);}
static void wait_for_completion(struct completion *c){while(!atomic_load(&c->done))usleep(100);}
struct kobject {int refs;};struct cdev {struct kobject kobj;void *owner;const void *ops;bool added;};
struct device {void *data;struct device *parent;struct kobject kobj;bool main;void *of_node;};
struct platform_device {struct device dev;};struct class {int unused;};
struct context_bank_info {struct device *dev;void *domain;int list;};
struct resources {bool dsp_enabled;int sku_version,irq_wd,context_banks;};
struct msm_cvp_core {struct mutex lock,clk_lock,idr_mtx;refcount_t users;struct completion users_zero;bool ready,dsp_initialized;struct cdev *cdev;struct class *class;struct device *dev;void *platform_data,*dev_ops,*debugfs_root,*capabilities;struct resources resources;int dev_num,hfi_type,sess_idr,ssr_work;long kernel_trans_id;};
struct msm_cvp_drv {struct mutex lock;struct msm_cvp_core *cvp_core;void *debugfs_root;int sku_version;};static struct msm_cvp_drv driver,*cvp_driver=&driver;
static int fail_step,steps,allocs,region,cdevs,sysfs,children,hfi,dsp,dsp_locks,work,stops;
static atomic_bool removed;static bool fail_child;static int cvp_hfi_defs[3];static int cvp_fops,msm_cvp_plat_match[1],msm_cvp_core_attr_group;
static bool fail(void){return ++steps==fail_step;}
static void *kzalloc(size_t s,int f){if(fail())return NULL;void *p=calloc(1,s);assert(p);allocs++;return p;}
static void kfree(void *p){if(p){assert(allocs>0);allocs--;free(p);}}
static void *cvp_get_drv_data(struct device *d){return (void*)1;}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static bool of_device_is_compatible(void *n,const char *s){return n==(void*)1;}
static int msm_cvp_initialize_core(struct platform_device *p,struct msm_cvp_core *c){if(fail())return -EIO;mutex_init(&c->lock);mutex_init(&c->clk_lock);mutex_init(&c->idr_mtx);c->resources.dsp_enabled=true;return 0;}
static int vm_start(struct msm_cvp_core *c){return fail()?-EIO:0;}struct vm_ops {int (*vm_start)(struct msm_cvp_core*);};static struct vm_ops ops={vm_start};static struct {struct vm_ops *vm_ops;int vm_id;}vm_manager={&ops,0};
static void cvp_handle_cmd_response(void){}
static void *cvp_hfi_initialize(int t,struct resources *r,void(*cb)(void)){if(fail())return ERR_PTR(-EIO);hfi++;return (void*)2;}
static void cvp_hfi_deinitialize(int t,void *o){assert(hfi==1&&!dsp_locks&&!work);hfi--;}
static void cvp_synx_ftbl_init(struct msm_cvp_core *c){}
static void enable_irq(int irq){}
static int of_platform_populate(void *n,void *m,void *v,struct device *p){children=2;return fail()?-EIO:0;}
static void of_platform_depopulate(struct device *d){children=0;}
static bool device_is_bound(struct device *d){return !fail_child;}
static int device_for_each_child(struct device *d,void *p,int(*fn)(struct device*,void*)){return fn(d,p);}
static void atomic64_set(long *p,long v){*p=v;}
static int cvp_dsp_device_init(void){if(fail())return -EIO;dsp++;dsp_locks++;return 0;}
static void cvp_dsp_device_exit(void){assert(dsp==1);dsp--;stops++;}
static void cvp_dsp_device_deinit(void){assert(dsp_locks==1&&!dsp);dsp_locks--;}
static void *msm_cvp_debugfs_init_drv(void){return (void*)3;}
static void *msm_cvp_debugfs_init_core(struct msm_cvp_core *c,void *p){return (void*)4;}
static void debugfs_remove_recursive(void *p){}
static void cancel_work_sync(int *w){work=0;}
static void msm_cvp_free_platform_resources(struct resources *r){}
static void idr_destroy(int *p){}
static int alloc_chrdev_region(int *d,int base,int count,const char *n){if(fail())return -ENOMEM;region++;*d=1;return 0;}
static void unregister_chrdev_region(int d,int n){assert(region==1);region--;}
static struct class *class_create(const char *n){void *p=kzalloc(sizeof(struct class),0);return p?p:ERR_PTR(-ENOMEM);}
static void class_destroy(struct class *c){kfree(c);}
static struct cdev *cdev_alloc(void){struct cdev *c=kzalloc(sizeof(*c),0);if(c)c->kobj.refs=1;return c;}
static void kobject_put(struct kobject *k){assert(k->refs>0);if(!--k->refs)kfree(k);}
static int cdev_add(struct cdev *c,int d,int n){if(fail())return -EIO;assert(!cvp_driver->cvp_core->ready);cdevs++;c->added=true;return 0;}
static void cdev_del(struct cdev *c){assert(cdevs==1&&c->added);cdevs--;c->added=false;kobject_put(&c->kobj);}
static struct device *device_create(struct class *c,struct device *p,int d,void *data,const char *n){struct device *v=kzalloc(sizeof(*v),0);if(!v)return ERR_PTR(-ENOMEM);v->parent=p;v->data=data;assert(!((struct msm_cvp_core*)data)->ready);return v;}
static void device_destroy(struct class *c,int d){kfree(cvp_driver->cvp_core->dev);}
static int sysfs_create_group(struct kobject *k,void *g){if(fail())return -EIO;assert(!cvp_driver->cvp_core->ready);sysfs++;return 0;}
static void sysfs_remove_group(struct kobject *k,void *g){assert(sysfs==1);sysfs--;}
#define list_for_each_entry_safe(cb,next,head,member) for((cb)=NULL,(next)=NULL;(cb)!=NULL && (next)!=NULL;)
#define list_del(p) ((void)0)
static void iommu_set_fault_handler(void *d,void *h,void *t){}
#include "nodes.inc"
static void clean(void){assert(!allocs&&!region&&!cdevs&&!sysfs&&!children&&!hfi&&!dsp&&!dsp_locks&&!cvp_driver->cvp_core);}
static void *remove_worker(void *v){msm_cvp_remove(v);atomic_store(&removed,true);return NULL;}
int main(void){mutex_init(&driver.lock);struct platform_device p={.dev={.of_node=(void*)1}},child={.dev={.parent=&p.dev}};int failures=0;
 for(int f=1;f<=12;f++){fail_step=f;steps=0;assert(msm_probe_cvp_device(&p)<0);clean();failures++;}
 fail_step=0;fail_child=true;steps=0;assert(msm_probe_cvp_device(&p)==-EPROBE_DEFER);clean();fail_child=false;
 for(int run=0;run<40;run++){steps=0;assert(!msm_probe_cvp_device(&p));struct msm_cvp_core *core=cvp_driver->cvp_core;assert(core->ready);assert(msm_probe_cvp_device(&p)==-EBUSY);msm_cvp_remove(&child);assert(cvp_driver->cvp_core==core);assert(cvp_get_core(core));struct cdev *old=core->cdev;old->kobj.refs++;work=1;atomic_store(&removed,false);pthread_t t;assert(!pthread_create(&t,NULL,remove_worker,&p));while(cvp_get_core(core)){cvp_put_core(core);usleep(100);}assert(!atomic_load(&removed)&&hfi==1);cvp_put_core(core);pthread_join(t,NULL);assert(atomic_load(&removed));assert(allocs==1);kobject_put(&old->kobj);clean();assert(!cvp_get_core(core));}
 mutex_destroy(&driver.lock);printf("{\"probe_failure_cases\":%d,\"core_drain_races\":40,\"child_remove_and_retained_cdev\":true}\n",failures+1);
}
