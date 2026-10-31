#define MCA_EVENT_USB_CONNECT 1
#define MCA_EVENT_WIRELESS_CONNECT 2
#define MCA_EVENT_USB_DISCONNECT 3
#define MCA_EVENT_WIRELESS_DISCONNECT 4
#define MCA_EVENT_IS_EU_MODEL 5
#define MCA_EVENT_CHARGE_TYPE_CHANGE 6
#define MCA_EVENT_BQ_FG_ERROR 7
#define EU_MODEL_RECHARGE_SOC 100
#define STRATEGY_FG_WORK_INTERVAL_FAST 1000
#define STRATEGY_FG_WORK_INTERVAL_DTPT 5000
#define NVT1000_OTA_UPDATE_FLAG 1
#define NVT1000_OTA_MONITOR_INTERVAL 1000
static int scheduled,vote_error,votes,cancels;
static struct workqueue_struct queue={1};
static time64_t ktime_get_boottime_seconds(void){return 100;}
static int msecs_to_jiffies(int v){return v;}
static int mca_vote_named(const char *name,const char *client,bool enabled,int value){(void)name;(void)client;(void)enabled;(void)value;votes++;return vote_error;}
static int mod_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int ms){(void)w;(void)ms;assert(q==&queue);scheduled++;return 0;}
static int queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int ms){return mod_delayed_work(q,w,ms);}
static int cancel_delayed_work_sync(struct delayed_work *w){(void)w;cancels++;return 0;}
static int platform_fg_ops_get_ota_update_flag(int n,int *v){(void)n;*v=0;return 0;}
static int strategy_fg_reset_co_to_default(struct strategy_fg *f){(void)f;return 0;}
/* STAGED_DEFINITIONS */
int main(void){
 struct strategy_fg f={.fg_init_flag=true,.wq=&queue,.cfg={.support_dtpt=1}};
 assert(strategy_fg_process_event(MCA_EVENT_USB_CONNECT,0,&f)==-EAGAIN&&!scheduled);f.ready=true;
 assert(!strategy_fg_process_event(MCA_EVENT_USB_CONNECT,0,&f)&&f.wired_present&&f.power_present&&scheduled==2);f.charging_done=true;f.keep_full_flag=true;
 assert(!strategy_fg_process_event(MCA_EVENT_WIRELESS_CONNECT,0,&f)&&f.wireless_present&&f.charging_done);int n=votes;assert(!strategy_fg_process_event(MCA_EVENT_USB_DISCONNECT,0,&f)&&f.power_present&&f.wireless_present&&!f.wired_present&&votes==n);
 assert(!strategy_fg_process_event(MCA_EVENT_WIRELESS_DISCONNECT,0,&f)&&!f.power_present&&!f.wireless_present&&!f.charging_done);
 vote_error=-EIO;assert(strategy_fg_ops_set_charging_done(&f,true)==-EIO&&!f.charging_done);vote_error=0;assert(!strategy_fg_ops_set_charging_done(&f,true)&&f.charging_done);n=scheduled;assert(!strategy_fg_process_event(MCA_EVENT_BQ_FG_ERROR,0,&f)&&f.sample_error==-EIO&&scheduled==n+1);
 f.stopping=true;n=scheduled;assert(strategy_fg_process_event(MCA_EVENT_USB_CONNECT,0,&f)==-ENODEV&&scheduled==n);
 puts("{\"wired_wireless_lifetime\":true,\"early_and_removed_events_rejected\":true,\"vote_failure_preserves_state\":true,\"error_sample_queued\":true}");
}
