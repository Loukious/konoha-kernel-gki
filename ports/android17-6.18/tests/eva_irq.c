#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define GFP_KERNEL 0
#define IRQF_TRIGGER_HIGH 1
#define IRQF_NO_AUTOEN 2
#define VM_TRUSTED 1
#define CVP_IFACEQ_VAR_HUGE_PKT_SIZE 2048
#define dprintk(...) ((void)0)
#define unlikely(x) (x)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>(uintptr_t)-4096)
typedef uint64_t phys_addr_t;typedef uint8_t u8;typedef uint32_t u32;typedef void(*hfi_cmd_response_callback)(void);
struct device {int unused;};struct platform_device {struct device dev;};
struct msm_cvp_platform_resources {phys_addr_t firmware_base,register_base,gcc_reg_base;u32 register_size,gcc_reg_size;int irq,irq_wd;struct platform_device *pdev;};
struct cvp_hal_data {int irq,irq_wd;phys_addr_t firmware_base;void *register_base,*gcc_reg_base;u32 register_size,gcc_reg_size;};
struct mutex {bool initialized;};struct iris_hfi_device {struct msm_cvp_platform_resources *res;hfi_cmd_response_callback callback;int *response_pkt;void *raw_packet,*cvp_workq,*iris_pm_workq;struct mutex lock;int sess_head;struct cvp_hal_data *cvp_hal_data;};
static int fail_at,step,live,irqs,queues,irq_mask,devres_maps;static bool cancelled;static int disabled;
static struct {struct {int ssr_work;} *cvp_core;} driver,*cvp_driver=&driver;
static int cvp_max_packets=32,iris_hfi_pm_work,iris_hfi_wd_work;static int regspace;
static bool failure(void){return ++step==fail_at;}
static void *kzalloc(size_t s,int f){if(failure())return NULL;live++;return calloc(1,s);}
static void *kmalloc_array(size_t n,size_t s,int f){return kzalloc(n*s,f);}
static void kfree(void *p){if(p){live--;assert(live>=0);free(p);}}
static void mutex_init(struct mutex *m){m->initialized=true;}
static void mutex_destroy(struct mutex *m){assert(m->initialized&&!irqs);m->initialized=false;}
#define INIT_LIST_HEAD(p) (*(p)=1)
static void *create_singlethread_workqueue(const char *n){if(failure())return NULL;queues++;return (void*)(intptr_t)queues;}
static void destroy_workqueue(void *p){assert(!irqs&&queues>0);queues--;}
static void cancel_delayed_work_sync(int *p){assert(disabled==irqs);cancelled=true;}
static void cancel_work_sync(int *p){assert(disabled==irqs);}
static void __init_cvp_ops(struct iris_hfi_device *d){assert(d->res&&d->callback);}
static int __check_core_registered(struct iris_hfi_device *d,phys_addr_t f,u8 *r,u32 s,phys_addr_t irq){return -EINVAL;}
static void *devm_ioremap(struct device *d,phys_addr_t p,u32 s){if(failure())return NULL;devres_maps++;return &regspace;}
static void cvp_hfi_isr(void){}static void iris_hfi_core_work_handler(void){}static void iris_hfi_isr_wd(void){}
static int request_threaded_irq(int irq,void(*a)(void),void(*b)(void),int flags,const char *n,struct iris_hfi_device *d){assert(d->lock.initialized&&d->cvp_workq&&d->iris_pm_workq&&(flags&IRQF_NO_AUTOEN));if(failure())return -EIO;irqs++;irq_mask|=1<<irq;return 0;}
static int request_irq(int irq,void(*fn)(void),int flags,const char *n,struct iris_hfi_device *d){return request_threaded_irq(irq,fn,fn,flags,n,d);}
static void disable_irq(int irq){assert(irq_mask&(1<<irq));disabled++;}
static void free_irq(int irq,struct iris_hfi_device *d){assert(irq_mask&(1<<irq));irq_mask&=~(1<<irq);irqs--;if(disabled)disabled--;}
static int msm_cvp_vm_init_reg_and_irq(struct iris_hfi_device *,struct msm_cvp_platform_resources *);
struct vm_ops {int (*vm_init_reg_and_irq)(struct iris_hfi_device*,struct msm_cvp_platform_resources*);};static struct vm_ops ops={msm_cvp_vm_init_reg_and_irq};static struct {int vm_id;struct vm_ops *vm_ops;} vm_manager={0,&ops};
#include "irq.inc"
static void response(void){}
int main(void){struct platform_device p={0};struct msm_cvp_platform_resources r={.pdev=&p,.register_size=4096,.irq=1,.irq_wd=2};int failures=0;
 for(int f=1;f<=9;f++){fail_at=f;step=0;void *d=__add_device(&r,response);assert(IS_ERR_OR_NULL(d));assert(!live&&!irqs&&!queues&&!irq_mask);devres_maps=0;failures++;}
 for(int f=0;f<50;f++){fail_at=0;step=0;cancelled=false;void *d=__add_device(&r,response);assert(!IS_ERR_OR_NULL(d));assert(irqs==2&&queues==2&&live==4&&devres_maps==1);cvp_iris_hfi_delete_device(d);assert(cancelled&&!live&&!irqs&&!queues&&!irq_mask);assert(devres_maps==1);devres_maps=0;}
 printf("{\"hfi_setup_failure_cases\":%d,\"irq_workqueue_teardowns\":50}\n",failures);
}
