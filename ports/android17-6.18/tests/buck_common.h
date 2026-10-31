#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef uint8_t u8;
typedef int64_t ktime_t;
struct mutex { pthread_mutex_t value; };
typedef pthread_mutex_t spinlock_t;
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->value, NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->value)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->value)); }
static struct mutex *take_guard(struct mutex *m) { mutex_lock(m); return m; }
static void drop_guard(struct mutex **m) { mutex_unlock(*m); }
#define guard(kind) struct mutex *__attribute__((cleanup(drop_guard))) locked_guard = take_guard
#define spin_lock_init(p) assert(!pthread_mutex_init((p),NULL))
#define spin_lock_irqsave(p,f) do { (f)=0; assert(!pthread_mutex_lock(p)); } while (0)
#define spin_unlock_irqrestore(p,f) do { (void)(f); assert(!pthread_mutex_unlock(p)); } while (0)
#define READ_ONCE(v) __atomic_load_n(&(v),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v),(x),__ATOMIC_RELEASE)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ARRAY_SIZE(v) (sizeof(v)/sizeof((v)[0]))
#define min(a,b) ((a)<(b)?(a):(b))
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(v) ((int)(intptr_t)(v))
#define IS_ERR(v) ((uintptr_t)(v)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(v) (!(v)||IS_ERR(v))
#define GFP_KERNEL 0
#define WQ_MEM_RECLAIM 0
#define EPROBE_DEFER 517
#define mca_log_info(...) ((void)0)
#define mca_log_err(...) ((void)0)
#define msecs_to_jiffies(v) (v)
#define MTBF_CLIENT_SUBSTR_UPPER "MTBF"
#define MTBF_CLIENT_SUBSTR_LOW "mtbf"
#define MTBF_ACTIVE_TEST_CLIENT_SUBSTR "shell"
#define MTBF_TEST_MAX_CURRENT_MA 1500
#define IS_ENABLED(config) 0
#define MCA_EVENT_WIRELESS_REVCHG 1000
#define current kernel_current_macro_must_not_be_used
enum { MCA_VOTE_MIN, MCA_VOTE_MAX, MCA_VOTE_OR, MCA_VOTE_AND };
enum { MAIN_BUCK_CHARGER, STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_SMARTCHG_IF_CHG_TYPE_BUCK,
 MCA_EVENT_TYPE_PANEL, MCA_EVENT_TYPE_THERMAL_TEMP, MCA_EVENT_TYPE_CHARGER_CONNECT,
 MCA_EVENT_USB_CONNECT };
enum { STRATEGY_STATUS_TYPE_ONLINE, STRATEGY_STATUS_TYPE_CHARGING,
 STRATEGY_STATUS_TYPE_QC_TYPE, STRATEGY_STATUS_TYPE_ENABLE, STRATEGY_STATUS_TYPE_VBUS,
 STRATEGY_STATUS_TYPE_IBUS };
struct device { int n; };
struct platform_device { struct device dev; void *data; };
struct work_struct { void (*fn)(struct work_struct *); bool pending; bool disabled; };
struct delayed_work { struct work_struct work; };
struct workqueue_struct { int id; };
struct notifier_block { int (*notifier_call)(struct notifier_block *,unsigned long,void *); };
struct adapter_power_cap_info { int nums; int cap[16]; };
struct mca_votable;
/* HEADER */
/* DECLARATIONS */
static void strategy_buckchg_sw_cv_workfunc(struct work_struct *w) { (void)w; }
static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *w) { (void)w; }
static void strategy_wls_revchg_monitor_workfunc(struct work_struct *w) { (void)w; }
static void strategy_csd_pulse_process_workfunc(struct work_struct *w) { (void)w; }
static void strategy_source_status_monitor_workfunc(struct work_struct *w) { (void)w; }
static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *w) { (void)w; }
static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *w) { (void)w; }
static void strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *w) { (void)w; }
static int strategy_buckchg_thermal_notifier_cb(struct notifier_block *n,unsigned long e,void *v) { (void)n;(void)e;(void)v;return 0; }
static int strategy_buckchg_panel_notifier_cb(struct notifier_block *n,unsigned long e,void *v) { (void)n;(void)e;(void)v;return 0; }
struct mca_charge_if_ops { void *data; };
struct mca_smartchg_if_ops { void *data; };
static const struct mca_charge_if_ops g_strategy_buckchg_if_ops;
static const struct mca_smartchg_if_ops g_buck_smartchg_if_ops;
static atomic_int sequence, fail_at;
static int fault(void) { int n=atomic_fetch_add(&sequence,1)+1;return n==atomic_load(&fail_at); }
static void *last_info;
static int live_voters, live_wq, registrations, online_error, provider_error;
static int direct_disable, adc_disable, disables, cancel_events, handled;
static int last_vote, vote_calls, override_calls, hardware_enable;
static struct strategy_buckchg_event seen[16384];
static bool lock_cycle;
static bool drain_work;
static pthread_t running_work;
static pthread_barrier_t drain_started, work_entered, work_release;
static pthread_mutex_t wired_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t buck_entered, wire_entered;
static void strategy_buckchg_monitor_workfunc_locked(struct work_struct *work) {
 (void)work;
 if(drain_work){pthread_barrier_wait(&work_entered);pthread_barrier_wait(&work_release);}
 if(lock_cycle) {
  pthread_barrier_wait(&buck_entered);pthread_barrier_wait(&wire_entered);
  assert(!pthread_mutex_lock(&wired_lock));assert(!pthread_mutex_unlock(&wired_lock));
 }
}
static int strategy_buckchg_handle_event(int event,int value,void *data) {
 (void)data;assert(handled<(int)ARRAY_SIZE(seen));seen[handled++]=(struct strategy_buckchg_event){event,value};return 0;
}
static void *devm_kzalloc(struct device *d,size_t n,int f) { (void)d;(void)f;if(fault())return NULL;last_info=calloc(1,n);assert(last_info);return last_info; }
static void platform_set_drvdata(struct platform_device *p,void *v) { p->data=v; }
static void *platform_get_drvdata(struct platform_device *p) { return p->data; }
#define INIT_WORK(w,f) do {(w)->fn=(f);(w)->pending=false;(w)->disabled=false;}while(0)
#define INIT_DELAYED_WORK(w,f) INIT_WORK(&(w)->work,f)
static struct workqueue_struct *alloc_ordered_workqueue(const char *n,int f) { (void)n;(void)f;if(fault())return NULL;live_wq++;return calloc(1,sizeof(struct workqueue_struct)); }
static bool queue_work(struct workqueue_struct *q,struct work_struct *w) { assert(q);if(w->disabled)return false;w->pending=true;return true; }
static bool mod_delayed_work(struct workqueue_struct *q,struct delayed_work *w,unsigned d) { (void)d;return queue_work(q,&w->work); }
static bool queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,unsigned d) { return mod_delayed_work(q,w,d); }
static void disable_delayed_work_sync(struct delayed_work *w) {
 assert(!registrations);
 if(drain_work && w==&((struct strategy_buckchg_dev *)last_info)->monitor_work){pthread_barrier_wait(&drain_started);assert(!pthread_join(running_work,NULL));drain_work=false;}
 w->work.disabled=true;w->work.pending=false;disables++;
}
static void cancel_work_sync(struct work_struct *w) { assert(!registrations);w->pending=false;cancel_events++; }
static void destroy_workqueue(struct workqueue_struct *q) { assert(q);free(q);live_wq--; }
static struct mca_votable *all_voters[11];
struct mca_votable {
 const char *name;int value;void *data;bool enabled;
 int (*cb)(struct mca_votable *,void *,int,const char *);
 struct mutex lock;
};
static struct mca_votable *mca_create_votable(const char *n,int t,int (*cb)(struct mca_votable *,void *,int,const char *),int def,void *d) {
 (void)t;struct strategy_buckchg_dev *info=d;
 assert(info->wq && info->monitor_work.work.fn && info->soc_limit_stepper_work.work.fn);
 if(fault())return ERR_PTR(-ENOMEM);
 struct mca_votable *v=calloc(1,sizeof(*v));assert(v);v->name=n;v->value=def;v->data=d;v->cb=cb;mutex_init(&v->lock);
 assert(live_voters<11);all_voters[live_voters++]=v;return v;
}
static void mca_destroy_votable(struct mca_votable *v) { assert(v && !IS_ERR(v));assert(all_voters[live_voters-1]==v);live_voters--;pthread_mutex_destroy(&v->lock.value);free(v); }
static int mca_vote(struct mca_votable *v,const char *n,bool enable,int value) {
 assert(v && !IS_ERR(v));vote_calls++;if(fault())return -EIO;
 mutex_lock(&v->lock);v->enabled=enable;v->value=value;last_vote=value;
 int ret=v->cb(v,v->data,value,n);mutex_unlock(&v->lock);return ret;
}
static int mca_vote_override(struct mca_votable *v,const char *n,bool en,int value) { override_calls++;return mca_vote(v,n,en,value); }
static int mca_get_effective_result(struct mca_votable *v) { assert(v);return v->value; }
static int sink_cb(struct mca_votable *v,void *data,int value,const char *client) { (void)v;(void)data;(void)value;(void)client;return provider_error; }
#define strategy_buckchg_enable sink_cb
#define strategy_buckchg_input_limit sink_cb
#define strategy_buckchg_charge_limit sink_cb
#define strategy_buckchg_set_iterm sink_cb
#define strategy_buckchg_set_vterm sink_cb
static int platform_class_buckchg_ops_get_online(int role,int *value) { (void)role;if(fault()||online_error)return -EIO;*value=0;return 0; }
static int strategy_class_buckchg_ops_set_chg(struct strategy_buckchg_dev *i,bool en) { (void)i;hardware_enable=en;if(!en)direct_disable++;return 0; }
static int strategy_class_buckchg_ops_adc_enable(struct strategy_buckchg_dev *i,bool en) { (void)i;if(!en)adc_disable++;return 0; }
static int platform_class_buckchg_ops_set_ship_mode(int role,bool en) { (void)role;hardware_enable=en;return provider_error; }
static int sensor_error, sensor_voltage=4000, sensor_current=-1000000, sensor_temperature=250;
static int strategy_class_fg_ops_get_voltage(int *v) { if(sensor_error==1)return -EIO;*v=sensor_voltage;return 0; }
static int strategy_class_fg_ops_get_current(int *v) { if(sensor_error==2)return -EIO;*v=sensor_current;return 0; }
static int strategy_class_fg_ops_get_temperature(int *v) { if(sensor_error==3)return -EIO;*v=sensor_temperature;return 0; }
static void strategy_buckchg_clear_pmic_temp_term(struct strategy_buckchg_dev *i) { (void)i; }
static int mca_strategy_ops_register(int t,int (*event)(int,int,void *),int (*status)(int,void *,void *),int (*config)(int,int,void *),void *data) { (void)t;(void)event;(void)status;(void)config;(void)data;if(fault())return -EBUSY;registrations++;return 0; }
static int strategy_buckchg_set_config(int a,int b,void *c) { (void)a;(void)b;(void)c;return 0; }
static int mca_charge_if_ops_register(struct mca_charge_if_ops *o) { assert(o->data);if(fault())return -EBUSY;registrations++;return 0; }
static int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *o) { assert(o->data);if(fault())return -EBUSY;registrations++;return 0; }
static int mca_event_block_notify_register(int type,struct notifier_block *n) { (void)type;assert(n->notifier_call);if(fault())return -EBUSY;registrations++;return 0; }
static void mca_event_block_notify_unregister(int type,struct notifier_block *n) { (void)type;(void)n;assert(registrations>0);registrations--; }
static void mca_smartchg_if_ops_unregister(int type,void *data) { (void)type;(void)data;assert(registrations>0);registrations--; }
static void mca_charge_if_ops_unregister(const char *type,void *data) { (void)type;(void)data;assert(registrations>0);registrations--; }
static void mca_strategy_ops_unregister(int type,void *data) { (void)type;(void)data;assert(registrations>0);registrations--; }
static void mca_event_block_notify(int type,int event,void *data) { (void)type;(void)event;(void)data; }
static void strategy_buckchg_parse_dt(struct strategy_buckchg_dev *i) {
 i->sw_cv_fcc_step=50;i->sw_cv_fv_step=5;i->ffc_temp_low=15;i->ffc_temp_high=48;
 i->allow_start_ffc_batt_soc_thr=95;i->chg_batt_auth_failed=2000;
 for(unsigned j=0;j<REV_USBIN_TYPE_MAX;j++){i->rev_req_vadp[j]=9000;i->rev_vadp_valid_l[j]=8400;i->rev_vadp_valid_h[j]=9200;}
}
static int kstrtoint(const char *s,int base,int *v) { char *end;errno=0;long n=strtol(s,&end,base);if(errno||end==s||(*end&&strcmp(end,"\n"))||n<INT32_MIN||n>INT32_MAX)return -EINVAL;*v=n;return 0; }
/* FUNCTIONS */
static void free_info(void) {
 if(last_info){struct strategy_buckchg_dev *i=last_info;pthread_mutex_destroy(&i->policy_lock.value);pthread_mutex_destroy(&i->event_lock);free(i);last_info=NULL;}
}
static void reset_fault(void) { sequence=0;fail_at=0;provider_error=0;online_error=0; }
