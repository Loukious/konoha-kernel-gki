#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;typedef long long ktime_t;typedef int atomic_t;
struct device{};struct miscdevice{};struct work_struct{};struct delayed_work{struct work_struct work;};struct mca_votable{};struct notifier_block{};struct inode{};struct file{};
#define mutex pthread_mutex_t
/* SMART_HEADER */
#define mca_log_info(...) ((void)0)
#define mca_log_err(...) ((void)0)
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((UINT32_MAX>>(31-(h)))&~((1u<<(l))-1))
#define is_power_of_2(v) ((v)&&!((v)&((v)-1)))
#define check_mul_overflow(a,b,p) __builtin_mul_overflow(a,b,p)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define SMART_CHG_SOC_HYS 2
#define SMART_CHG_SOCLMT_CANCEL_DEFAULT 5
#define SMART_CHG_SOCLMT_TRIG_DEFAULT 80
#define SMART_CHARGE_DTPT_DELTA_VALUE 15
#define CHARGE_DFX_SMART_ENDURANCE_SOC_ERR 0
#define CHARGE_DFX_SMART_NAVIGATION_SOC_ERR 1
#define MCA_EVENT_USB_CONNECT 1
#define MCA_EVENT_USB_DISCONNECT 2
#define MCA_EVENT_WIRELESS_CONNECT 3
#define MCA_EVENT_WIRELESS_DISCONNECT 4
#define MCA_EVENT_BATTERY_DTPT 5
static int system_wq,soc,scheduled,limit_calls,last_limit,vote_error,vote_value,control_calls;
struct mutex_guard {pthread_mutex_t *lock;};
static struct mutex_guard mutex_guard_init(pthread_mutex_t *l){pthread_mutex_lock(l);return (struct mutex_guard){l};}
static void mutex_guard_cleanup(struct mutex_guard *g){pthread_mutex_unlock(g->lock);}
#define guard(t) struct mutex_guard g __attribute__((cleanup(mutex_guard_cleanup)))=mutex_guard_init
static void smartcharging_handle_controlmessage(struct smart_charge_info *i){control_calls++;i->smart_chg_data.ret_code=0;}
static int mca_vote(struct mca_votable *v,const char *c,bool e,int n){(void)v;(void)c;(void)e;vote_value=n;return vote_error;}
static int mod_delayed_work(int q,struct delayed_work *w,int t){(void)q;(void)w;(void)t;scheduled++;return 0;}
static int schedule_delayed_work(struct delayed_work *w,int t){return mod_delayed_work(0,w,t);}
static int cancel_delayed_work(struct delayed_work *w){(void)w;return 0;}
static int msecs_to_jiffies(int v){return v;}
static int strategy_class_fg_ops_get_soc(void){return soc;}
static int strategy_class_fg_ops_get_rsoc(int *v){*v=soc;return soc<0?soc:0;}
static void smart_charge_set_soc_limit(bool en){limit_calls++;last_limit=en;}
static void mca_charge_mievent_report(int a,void *d,int c){(void)a;(void)d;(void)c;}
static void smart_charge_plugin_or_plugout_reset(struct smart_charge_info *i){(void)i;}
/* STAGED_DEFINITIONS */
int main(void){
 struct smart_charge_info i={0};pthread_mutex_init(&i.state_lock,NULL);i.cell_type=1;
 assert(sizeof(union SMART_CHG_HEADER)==4);
 for(unsigned f=0;f<32768;f++){int val=(f<<1)|1;int r=smartcharging_receive_control_message(&i,val);bool allowed=is_power_of_2(f)&&f<=512&&f!=1&&f!=8&&f!=512;assert((r==0)==allowed);}
 for(unsigned n=0;n<65536;n++){int r=smartcharging_receive_control_message(&i,(n<<16)|3);assert((r==0)==(n>=1&&n<=100));}
 assert(smartcharging_receive_control_message(&i, (80<<16)|3)==0);
 assert(!handle_smart_batt_message(&i,20)&&vote_value==20&&i.smart_batt_val==20);i.cell_type=2;
 assert(handle_smart_batt_message(&i,INT_MAX)==-ERANGE);assert(handle_smart_batt_message(&i,-1)==-ERANGE);vote_error=-EIO;assert(handle_smart_batt_message(&i,15)==-EIO&&i.smart_batt_val==20);vote_error=0;
 i.smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT].enable=1;i.smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT].func_value=80;smartcharging_handle_algorithm_conflict(&i);assert(i.soc_limit==80);
 i.smart_chg_data.smart_chgcfg[SMART_CHG_NAVIGATION].enable=1;i.smart_chg_data.smart_chgcfg[SMART_CHG_NAVIGATION].func_value=75;smartcharging_handle_algorithm_conflict(&i);assert(i.soc_limit==75);
 i.smart_chg_data.smart_chgcfg[SMART_CHG_ENDURANCE_PRO].enable=1;i.smart_chg_data.smart_chgcfg[SMART_CHG_ENDURANCE_PRO].func_value=70;smartcharging_handle_algorithm_conflict(&i);assert(i.soc_limit==70);
 i.online=1;soc=70;smart_charge_soc_limit_workfunc(&i.smart_soc_limit_work.work);assert(i.soc_limit_enable&&last_limit==1);int calls=limit_calls;soc=-EIO;smart_charge_soc_limit_workfunc(&i.smart_soc_limit_work.work);assert(i.soc_limit_enable&&limit_calls==calls);soc=101;smart_charge_soc_limit_workfunc(&i.smart_soc_limit_work.work);assert(i.soc_limit_enable&&limit_calls==calls);soc=66;smart_charge_soc_limit_workfunc(&i.smart_soc_limit_work.work);assert(i.soc_limit_enable);soc=65;smart_charge_soc_limit_workfunc(&i.smart_soc_limit_work.work);assert(!i.soc_limit_enable&&last_limit==0);
 assert(!mca_smart_charge_process_event(MCA_EVENT_USB_CONNECT,0,&i));assert(i.online&&i.usb_online);assert(!mca_smart_charge_process_event(MCA_EVENT_WIRELESS_CONNECT,0,&i));assert(!mca_smart_charge_process_event(MCA_EVENT_USB_DISCONNECT,0,&i));assert(i.online&&!i.usb_online&&i.wls_online);assert(!mca_smart_charge_process_event(MCA_EVENT_WIRELESS_DISCONNECT,0,&i));assert(!i.online);
 assert(smart_charge_file_open(NULL,NULL)==-EOPNOTSUPP);
 puts("{\"passed\":true,\"feature_masks\":32768,\"soc_inputs\":65536,\"soc_hysteresis_and_read_errors\":true,\"dual_supply_disconnect\":true,\"overflow_and_vote_errors\":true,\"unverified_blob_rejected\":true}");
}
