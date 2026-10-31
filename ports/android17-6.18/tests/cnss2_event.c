#include "cnss2_mock.h"
#define CNSS_EVENT_PENDING 2989
#define CNSS_EVENT_SYNC BIT(0)
#define CNSS_EVENT_UNINTERRUPTIBLE BIT(1)
#define CNSS_EVENT_UNKILLABLE BIT(2)
#define CNSS_DRIVER_IDLE_RESTART 4
#define CNSS_DRIVER_IDLE_SHUTDOWN 5
#define fallthrough __attribute__((fallthrough))
#define current NULL
#define in_interrupt() false
#define irqs_disabled() false
/* EVENT TYPES */
struct cnss_plat_data { struct list_head event_list;spinlock_t event_lock;struct work_struct event_work;struct workqueue_struct *event_wq;bool stopping;unsigned long driver_state;void *cnss_event_work_task;int awake; };
static struct cnss_plat_data context;
static unsigned handled;static int wait_mode;
static void cnss_pm_stay_awake(struct cnss_plat_data *p) { p->awake++; }
static void cnss_pm_relax(struct cnss_plat_data *p) { assert(p->awake>0);p->awake--; }
static void cnss_driver_event_work(struct work_struct *);
static void wait_for_completion(struct completion *c) { if(wait_mode==3)context.stopping=true;cnss_driver_event_work(&context.event_work);assert(c->done); }
static int wait_for_completion_interruptible(struct completion *c) { if(wait_mode==1)return -ERESTARTSYS;wait_for_completion(c);return wait_mode==2?-ERESTARTSYS:0; }
#define wait_for_completion_killable(c) wait_for_completion_interruptible(c)
static int simple(void) { handled++;return 7; }
static int payload(void *data) { handled++;kfree(data);return 7; }
#define cnss_wlfw_server_arrive(p,d) payload(d)
#define cnss_wlfw_server_exit(p) simple()
#define cnss_bus_alloc_fw_mem(p) 0
#define cnss_wlfw_respond_mem_send_sync(p) simple()
#define cnss_fw_mem_ready_hdlr(p) simple()
#define cnss_fw_ready_hdlr(p) simple()
#define cnss_cold_boot_cal_start_hdlr(p) simple()
#define cnss_cold_boot_cal_done_hdlr(p,d) payload(d)
#define cnss_bus_register_driver_hdlr(p,d) simple()
#define cnss_bus_unregister_driver_hdlr(p) simple()
#define cnss_driver_recovery_hdlr(p,d) payload(d)
#define cnss_bus_force_fw_assert_hdlr(p) simple()
#define cnss_power_up_hdlr(p) simple()
#define cnss_power_down_hdlr(p) simple()
#define cnss_process_wfc_call_ind_event(p,d) payload(d)
#define cnss_process_twt_cfg_ind_event(p,d) payload(d)
#define cnss_qdss_trace_req_mem_hdlr(p) simple()
#define cnss_fw_mem_file_save_hdlr(p,d) payload(d)
#define cnss_qdss_trace_free_hdlr(p) simple()
#define cnss_qdss_trace_req_data_hdlr(p,d) payload(d)
#define cnss_resume_post_sol_hdlr(p,d) payload(d)
/* FUNCTIONS */
static bool owns(int type)
{
 return type==CNSS_DRIVER_EVENT_SERVER_ARRIVE || type==CNSS_DRIVER_EVENT_COLD_BOOT_CAL_DONE || type==CNSS_DRIVER_EVENT_RECOVERY || type==CNSS_DRIVER_EVENT_IMS_WFC_CALL_IND || type==CNSS_DRIVER_EVENT_WLFW_TWT_CFG_IND || type==CNSS_DRIVER_EVENT_FW_MEM_FILE_SAVE || type==CNSS_DRIVER_EVENT_QDSS_TRACE_REQ_DATA || type==CNSS_DRIVER_EVENT_RESUME_POST_SOL;
}
static void *data_for(int type) { static int driver;return owns(type)?kzalloc(4,0):type==CNSS_DRIVER_EVENT_REGISTER_DRIVER?&driver:NULL; }
int main(void)
{
 INIT_LIST_HEAD(&context.event_list);context.event_wq=create_singlethread_workqueue("events");unsigned cases=0;
 assert(cnss_driver_event_post(&context,-1,0,NULL)==-EINVAL);assert(cnss_driver_event_post(&context,CNSS_DRIVER_EVENT_MAX,0,NULL)==-EINVAL);
 for(int type=0;type<CNSS_DRIVER_EVENT_MAX;type++) {
  void *data=data_for(type);alloc_fault=alloc_step+1;assert(cnss_driver_event_post(&context,type,0,data)==-ENOMEM && !live);alloc_fault=0;cases++;
  unsigned before=handled;assert(!cnss_driver_event_post(&context,type,0,data_for(type)));assert(!list_empty(&context.event_list));
  cnss_driver_event_work(&context.event_work);assert(handled==before+1 && !live && !context.awake);cases++;
  assert(!cnss_driver_event_post(&context,type,0,data_for(type)));context.stopping=true;before=handled;
  cnss_driver_event_work(&context.event_work);assert(handled==before && !live && !context.awake);cases++;
  assert(cnss_driver_event_post(&context,type,0,data_for(type))==-ESHUTDOWN && !live && list_empty(&context.event_list));context.stopping=false;cases++;
  for(unsigned flags=1;flags<8;flags+=2) {
   wait_mode=0;assert(cnss_driver_event_post(&context,type,flags,data_for(type))==7 && !live && !context.awake);cases++;
  }
  wait_mode=3;assert(cnss_driver_event_post(&context,type,CNSS_EVENT_SYNC,data_for(type))==-ESHUTDOWN && !live);context.stopping=false;cases++;
  wait_mode=1;assert(cnss_driver_event_post(&context,type,CNSS_EVENT_SYNC,data_for(type))==-EINTR);assert(!list_empty(&context.event_list));cnss_driver_event_work(&context.event_work);assert(!live);cases++;
  wait_mode=2;assert(cnss_driver_event_post(&context,type,CNSS_EVENT_SYNC,data_for(type))==7 && !live);cases++;
 }
 assert(!context.awake);destroy_workqueue(context.event_wq);
 printf("{\"event_ownership_wait_and_stop_cases\":%u,\"async_payloads_released_on_failure\":true,\"borrowed_driver_ops_retained\":true,\"stop_drops_queued_events_and_completes_sync_waiters\":true}\n",cases);
}
