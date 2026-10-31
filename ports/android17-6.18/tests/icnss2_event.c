#include "icnss2_mock.h"
#define ICNSS_EVENT_PENDING 2989
#define ICNSS_EVENT_SYNC BIT(0)
#define ICNSS_EVENT_UNINTERRUPTIBLE BIT(1)
#define ICNSS_EVENT_UNKILLABLE BIT(2)
#define ICNSS_DRIVER_IDLE_RESTART 4
#define ICNSS_DRIVER_IDLE_SHUTDOWN 5
#define fallthrough __attribute__((fallthrough))
#define current NULL
static bool atomic_context;
#define preemptible() (!atomic_context)
#define rcu_preempt_depth() 0
#define in_interrupt() false
#define irqs_disabled() false
/* EVENT TYPES */
struct icnss_priv { struct list_head event_list,soc_wake_msg_list;spinlock_t event_lock,soc_wake_msg_lock;struct work_struct event_work,soc_wake_msg_work;struct workqueue_struct *event_wq,*soc_wake_wq;bool stopping;unsigned long state;int awake;struct {struct {unsigned posted,processed;}events[ICNSS_DRIVER_EVENT_MAX],soc_wake_events[ICNSS_SOC_WAKE_EVENT_MAX];}stats; };
static struct icnss_priv context;
static unsigned handled;static int wait_mode;
static void icnss_pm_stay_awake(struct icnss_priv *p) { p->awake++; }
static void icnss_pm_relax(struct icnss_priv *p) { assert(p->awake>0);p->awake--; }
static void icnss_driver_event_work(struct work_struct *);
static void wait_for_completion(struct completion *c) { if(wait_mode==3)context.stopping=true;icnss_driver_event_work(&context.event_work);assert(c->done); }
static int wait_for_completion_interruptible(struct completion *c) { if(wait_mode==1)return -ERESTARTSYS;wait_for_completion(c);return wait_mode==2?-ERESTARTSYS:0; }
#define wait_for_completion_killable(c) wait_for_completion_interruptible(c)
static int simple(void) { handled++;return 7; }
static int payload(void *data) { handled++;kfree(data);return 7; }
#define icnss_driver_event_server_arrive(p,d) payload(d)
#define icnss_driver_event_server_exit(...) simple()
#define icnss_driver_event_fw_ready_ind(...) simple()
#define icnss_driver_event_register_driver(...) simple()
#define icnss_driver_event_unregister_driver(...) simple()
#define icnss_driver_event_pd_service_down(p,d) payload(d)
#define icnss_driver_event_early_crash_ind(...) simple()
#define icnss_driver_event_idle_shutdown(...) simple()
#define icnss_driver_event_idle_restart(...) simple()
#define icnss_driver_event_fw_init_done(...) simple()
#define icnss_qdss_trace_req_mem_hdlr(...) simple()
#define icnss_qdss_trace_save_hdlr(p,d) payload(d)
#define icnss_qdss_trace_free_hdlr(...) simple()
#define icnss_m3_dump_upload_req_hdlr(p,d) payload(d)
#define icnss_qdss_trace_req_data_hdlr(p,d) payload(d)
#define icnss_subsys_restart_level(p,d) payload(d)
#define icnss_process_wfc_call_ind_event(p,d) payload(d)
#define icnss_process_twt_cfg_ind_event(p,d) payload(d)
#undef spin_lock_irqsave
#undef spin_unlock_irqrestore
#define spin_lock_irqsave(p,f) do { assert(!*(p));*(p)=1;(f)=0; } while(0)
#define spin_unlock_irqrestore(p,f) do { assert(*(p));*(p)=0;(void)(f); } while(0)
/* FUNCTIONS */
static bool owns(int type)
{
 return type==ICNSS_DRIVER_EVENT_SERVER_ARRIVE || type==ICNSS_DRIVER_EVENT_PD_SERVICE_DOWN || type==ICNSS_DRIVER_EVENT_QDSS_TRACE_SAVE || type==ICNSS_DRIVER_EVENT_M3_DUMP_UPLOAD_REQ || type==ICNSS_DRIVER_EVENT_QDSS_TRACE_REQ_DATA || type==ICNSS_DRIVER_EVENT_SUBSYS_RESTART_LEVEL || type==ICNSS_DRIVER_EVENT_IMS_WFC_CALL_IND || type==ICNSS_DRIVER_EVENT_WLFW_TWT_CFG_IND;
}
static void *data_for(int type) { static int driver;return owns(type)?kzalloc(4,0):type==ICNSS_DRIVER_EVENT_REGISTER_DRIVER?&driver:NULL; }
int main(void)
{
 INIT_LIST_HEAD(&context.event_list);context.event_wq=create_singlethread_workqueue("events");unsigned cases=0;
 assert(icnss_driver_event_post(&context,-1,0,NULL)==-EINVAL);assert(icnss_driver_event_post(&context,ICNSS_DRIVER_EVENT_MAX,0,NULL)==-EINVAL);
 for(int type=0;type<ICNSS_DRIVER_EVENT_MAX;type++) {
  void *data=data_for(type);alloc_fault=alloc_step+1;assert(icnss_driver_event_post(&context,type,0,data)==-ENOMEM && !live);alloc_fault=0;cases++;
  atomic_context=true;assert(icnss_driver_event_post(&context,type,ICNSS_EVENT_SYNC,data_for(type))==-EWOULDBLOCK&&!live);atomic_context=false;cases++;
  unsigned before=handled;assert(!icnss_driver_event_post(&context,type,0,data_for(type)));assert(!list_empty(&context.event_list));
  icnss_driver_event_work(&context.event_work);assert(handled==before+1 && !live && !context.awake);cases++;
  assert(!icnss_driver_event_post(&context,type,0,data_for(type)));context.stopping=true;before=handled;
  icnss_driver_event_work(&context.event_work);assert(handled==before && !live && !context.awake);cases++;
  assert(icnss_driver_event_post(&context,type,0,data_for(type))==-ENODEV && !live && list_empty(&context.event_list));context.stopping=false;cases++;
  for(unsigned flags=1;flags<8;flags+=2) {
   wait_mode=0;assert(icnss_driver_event_post(&context,type,flags,data_for(type))==7 && !live && !context.awake);cases++;
  }
  wait_mode=3;assert(icnss_driver_event_post(&context,type,ICNSS_EVENT_SYNC,data_for(type))==-ESHUTDOWN && !live);context.stopping=false;cases++;
  wait_mode=1;assert(icnss_driver_event_post(&context,type,ICNSS_EVENT_SYNC,data_for(type))==-EINTR);assert(!list_empty(&context.event_list));icnss_driver_event_work(&context.event_work);assert(!live);cases++;
  wait_mode=2;assert(icnss_driver_event_post(&context,type,ICNSS_EVENT_SYNC,data_for(type))==7 && !live);cases++;
 }
 assert(!context.awake && !context.event_lock);destroy_workqueue(context.event_wq);
 printf("{\"event_ownership_wait_and_stop_cases\":%u,\"async_payloads_released_on_failure\":true,\"borrowed_driver_ops_retained\":true,\"stop_drops_queued_events_and_completes_sync_waiters\":true}\n",cases);
}
