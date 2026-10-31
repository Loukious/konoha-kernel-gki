#include "mock.h"
#define MSM_PCIE_INT_GLOBAL_INT 0
#define IRQF_TRIGGER_RISING 1
#define IRQF_ONESHOT 2
#define IRQF_TRIGGER_FALLING 4
struct device{int x;};struct platform_device{struct device dev;};struct wakeup_source{int x;};struct work_struct{int initialized,pending;};struct msm_pcie_dev_t{struct platform_device *pdev;struct wakeup_source *ws;struct {int num;const char *name;}irq[1];int wake_n;bool global_irq_requested,wake_irq_requested,wake_irq_enabled;struct work_struct handle_wake_work,handle_sbr_work,link_recover_wq,drv_disable_pc_work,drv_enable_pc_work;};
static int fault,step,live_ws,live_irq,wake_depth,drains;static struct msm_pcie_dev_t *current;static struct wakeup_source ws;
static const char *dev_name(struct device *p){return "pcie";}
static struct wakeup_source *wakeup_source_register(struct device *p,const char *s){if(++step==fault)return NULL;live_ws++;return &ws;}
static void wakeup_source_unregister(struct wakeup_source *p){if(p){assert(!live_irq&&drains>=5);live_ws--;}}
static void handle_global_irq(void){}
static void handle_aer_irq(void){}
static void handle_wake_irq(void){}
static int devm_request_threaded_irq(struct device *p,int irq,void (*a)(void),void (*b)(void),int f,const char *s,void *v){assert(current->handle_wake_work.initialized&&current->handle_sbr_work.initialized);if(++step==fault)return -EIO;live_irq++;return 0;}
static int devm_request_irq(struct device *p,int irq,void (*a)(void),int f,const char *s,void *v){return devm_request_threaded_irq(p,irq,a,NULL,f,s,v);}
static int enable_irq_wake(int i){if(++step==fault)return -EIO;wake_depth++;return 0;}
static void disable_irq_wake(int i){assert(wake_depth==1);wake_depth--;}
static void devm_free_irq(struct device *p,int i,void *d){assert(live_irq>0&&!wake_depth);live_irq--;}
static void cancel_work_sync(struct work_struct *p){assert(p->initialized);p->pending=0;drains++;}
/* FUNCTIONS */
int main(void){struct platform_device p={0};unsigned cases=0;for(int g=0;g<2;g++)for(int w=0;w<2;w++)for(fault=0;fault<=5;fault++){struct msm_pcie_dev_t d={.pdev=&p,.irq={{g?7:0,"global"}},.wake_n=w?8:0,.handle_wake_work={1,1},.handle_sbr_work={1,1},.link_recover_wq={1,1},.drv_disable_pc_work={1,1},.drv_enable_pc_work={1,1}};current=&d;step=drains=0;int ret=msm_pcie_irq_init(&d);if(!ret){assert(live_irq==g+w&&wake_depth==w);msm_pcie_irq_deinit(&d);}else{assert(ret==-EIO||ret==-ENOMEM);}
assert(!live_irq&&!wake_depth&&!live_ws);msm_pcie_irq_deinit(&d);assert(!live_irq&&!live_ws);cases++;}
printf("{\"irq_fault_and_cleanup_cases\":%u,\"wake_balance_and_work_drains\":true}\n",cases);}
