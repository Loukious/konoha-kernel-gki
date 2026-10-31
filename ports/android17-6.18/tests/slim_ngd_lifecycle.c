#include "slim_mock.h"
#define dev_get_drvdata(d) ((d)->data)
#define platform_set_drvdata(p,x) ((p)->dev.data=(x))
#define platform_get_drvdata(p) ((p)->dev.data)
#define QCOM_SLIM_NGD_AUTOSUSPEND 100
/* DEVICE EXTRA */
struct platform_device{struct device dev;};struct pdr_service{int live;};struct pdr_handle{int live;};
struct qcom_slim_ngd_ctrl{struct{struct device*dev;}ctrl;struct workqueue_struct*mwq;struct work_struct m_work,ngd_up_work;struct completion qmi_up;struct mutex tx_lock;bool stopping,dma_stopping,restart_vote;struct notifier_block nb;void*notifier;struct pdr_handle*pdr;struct{int live;}qmi;};
static int failure,steps,pm_votes,pm_enabled,controller_live,notifier_live,pdr_live,svc_live,dma_live,irq_live,handle_live;
static struct device *parent;
static bool fail(void){return ++steps==failure;}
static void pm_runtime_use_autosuspend(struct device*d){}
static void pm_runtime_set_autosuspend_delay(struct device*d,int n){}
static void pm_runtime_set_suspended(struct device*d){}
static void pm_runtime_enable(struct device*d){assert(!pm_enabled);pm_enabled=1;}
static void pm_runtime_disable(struct device*d){pm_enabled=0;}
static void pm_runtime_get_noresume(struct device*d){pm_votes++;}
static void pm_runtime_put_noidle(struct device*d){assert(pm_votes);pm_votes--;}
static int slim_register_controller(void*c){if(fail())return -EIO;controller_live=1;return 0;}
static int slim_unregister_controller(void*c){assert(!notifier_live&&!pdr_live&&!dma_live&&!irq_live&&!handle_live);controller_live=0;return 0;}
static int qcom_slim_ngd_qmi_svc_event_init(struct qcom_slim_ngd_ctrl*c){assert(controller_live&&c->mwq);if(fail())return -EIO;svc_live=1;return 0;}
static void qcom_slim_ngd_qmi_svc_event_deinit(void*q){assert(svc_live);svc_live=0;}
static int qcom_slim_ngd_ssr_notify(struct notifier_block*n,unsigned long x,void*p){return 0;}
static void*qcom_register_ssr_notifier(const char*n,struct notifier_block*b){assert(controller_live&&svc_live);if(fail())return ERR_PTR(-EIO);notifier_live=1;dma_live=irq_live=handle_live=1;return b;}
static void qcom_unregister_ssr_notifier(void*n,struct notifier_block*b){assert(notifier_live);notifier_live=0;}
static void slim_pd_status(int state,char*path,void*p){}
static struct pdr_handle*pdr_handle_alloc(void(*cb)(int,char*,void*),void*p){if(fail())return ERR_PTR(-EIO);pdr_live=1;return (void*)0x111;}
static struct pdr_service*pdr_add_lookup(struct pdr_handle*h,const char*a,const char*b){if(fail())return ERR_PTR(-EIO);return (void*)0x222;}
static void pdr_handle_release(struct pdr_handle*h){assert(pdr_live);pdr_live=0;}
static void qcom_slim_ngd_disable_irq(struct qcom_slim_ngd_ctrl*c){irq_live=0;}
static int qcom_slim_ngd_exit_dma(struct qcom_slim_ngd_ctrl*c){assert(c->stopping&&c->tx_lock.held);dma_live=0;return 0;}
static void qcom_slim_qmi_exit(struct qcom_slim_ngd_ctrl*c){handle_live=0;}
/* FUNCTIONS */
int main(void){struct device par={0};struct platform_device dev={.dev={.parent=&par}};int cases=0;for(int f=1;f<=6;f++){struct qcom_slim_ngd_ctrl c={0};par.data=&c;steps=0;failure=f-1;alloc_fault=f==1?alloc_step+1:0;int ret=qcom_slim_ngd_probe(&dev);assert(ret<0&&!controller_live&&!pm_enabled&&!pm_votes&&!notifier_live&&!pdr_live&&!svc_live&&!queues);alloc_fault=0;cases++;}
struct qcom_slim_ngd_ctrl c={0};par.data=&c;failure=0;steps=0;assert(!qcom_slim_ngd_probe(&dev));assert(controller_live&&pm_enabled&&pm_votes==1&&notifier_live&&pdr_live&&svc_live&&queues==1&&!c.stopping);dma_live=irq_live=handle_live=1;c.ngd_up_work.pending=c.m_work.pending=1;qcom_slim_ngd_remove(&dev);assert(c.stopping&&!controller_live&&!pm_enabled&&!pm_votes&&!notifier_live&&!pdr_live&&!svc_live&&!queues&&!dma_live&&!irq_live&&!handle_live&&!c.m_work.pending&&!c.ngd_up_work.pending);printf("{\"probe_failure_stages\":%d,\"remove_quiesces_producers_before_controller\":true,\"PM_votes_balanced\":true}\n",cases);}
