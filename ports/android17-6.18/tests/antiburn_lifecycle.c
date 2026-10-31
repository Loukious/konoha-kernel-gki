#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int64_t ktime_t;
typedef int64_t s64;
#define __rcu
#define GFP_KERNEL 0
#define WQ_MEM_RECLAIM 0
#define GPIOD_ASIS 0
#define PAGE_SIZE 4096
#define EPROBE_DEFER 517
#define ARRAY_SIZE(v) (sizeof(v)/sizeof(v[0]))
#define max(a,b) ((a)>(b)?(a):(b))
#define READ_ONCE(v) __atomic_load_n(&(v),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v),(x),__ATOMIC_RELEASE)
#define rcu_access_pointer(p) READ_ONCE(p)
#define rcu_assign_pointer(p,v) WRITE_ONCE(p,v)
#define srcu_dereference(p,s) READ_ONCE(p)
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(v) ((int)(intptr_t)(v))
#define IS_ERR(v) ((uintptr_t)(v)>=(uintptr_t)-4095)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define mca_log_info(...) ((void)0)
#define mca_log_err(...) ((void)0)
#define msecs_to_jiffies(v) (v)
struct mutex {pthread_mutex_t value;};
struct srcu_struct {pthread_rwlock_t readers;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define DEFINE_STATIC_SRCU(n) struct srcu_struct n={PTHREAD_RWLOCK_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->value,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->value));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->value));}
static struct mutex *get_mutex(struct mutex *m){mutex_lock(m);return m;}
static void drop_mutex(struct mutex **m){mutex_unlock(*m);}
static struct srcu_struct *get_srcu(struct srcu_struct *s){assert(!pthread_rwlock_rdlock(&s->readers));return s;}
static void drop_srcu(struct srcu_struct **s){assert(!pthread_rwlock_unlock(&(*s)->readers));}
#define GUARD_mutex struct mutex *__attribute__((cleanup(drop_mutex))) guard_m=get_mutex
#define GUARD_srcu struct srcu_struct *__attribute__((cleanup(drop_srcu))) guard_s=get_srcu
#define guard(kind) GUARD_##kind
struct device {void *data;};
struct platform_device {struct device dev;};
struct attribute {const char *name;};
struct device_attribute {struct attribute attr;};
struct mca_sysfs_attr_info {struct device_attribute attr;int sysfs_attr_name;};
struct attribute_group {struct attribute **attrs;};
struct notifier_block {int (*notifier_call)(struct notifier_block *,unsigned long,void *);};
struct work_struct {void (*fn)(struct work_struct *);bool pending,disabled;};
struct delayed_work {struct work_struct work;};
struct workqueue_struct {int id;};
struct gpio_desc {int raw;};
struct thermal_zone_device {int temperature,error;};
struct mca_hwid {int platform_version,major_version,minor_version;};
struct mca_log_charge_log_ops {int (*dump_log_context)(void *,char *,int);};
#define mca_sysfs_attr_ro(prefix,perm,id,label) {{{#label}},id}
#define mca_sysfs_attr_rw(prefix,perm,id,label) mca_sysfs_attr_ro(prefix,perm,id,label)
enum {MAIN_BUCK_CHARGER,TYPEC_PORT_0,ADAPTER_PROTOCOL_PD,SYSFS_DEV_5,MCA_EVENT_TYPE_HW_INFO,MCA_EVENT_CONN_ANTIBURN_CHANGE,
 MCA_EVENT_TYPE_THERMAL_TEMP,MCA_EVENT_TYPE_SUBPMIC_INFO,MCA_CHARGE_LOG_ID_USCP,
 MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE,MCA_EVENT_DEBUG_CTRL_DOUBLE85,MCA_EVENT_DEBUG_CTRL_REMOVE_TEMP_LIMIT,MCA_EVENT_DEBUG_CTRL_MEMORY_TEST,
 CHARGE_DFX_ANTI_BURN_TRIGGERED,CHARGE_DFX_ANTIBURN_ERR,MIEVENT_STATE_END,USBPD_UVDM_RESET_VSAFE0V};
enum {XM_CHARGER_TYPE_UNKNOW,XM_CHARGER_TYPE_HVDCP2,XM_CHARGER_TYPE_HVDCP3,XM_CHARGER_TYPE_HVDCP3_B,XM_CHARGER_TYPE_HVDCP3P5,XM_CHARGER_TYPE_PD,XM_CHARGER_TYPE_PD_VERIFY,XM_CHARGER_TYPE_PPS};
enum {OTG_EN_BOOST,WIRELESS_EN_BOOST,EXTERNAL_BOOST,BOOST_SRC_MAX=4};
#define NOTIFY_DONE 0
/* HEADER */
/* GLOBALS */
/* TABLE */
/* DECLARATIONS */
static const struct mca_log_charge_log_ops g_connector_antiburn_log_ops={connector_antiburn_dump_log_context};
static int sequence,fail_at,notifiers,log_live,sysfs_live,wq_live,provider_error,direction_error;
static int usb_online,otg,cid,bus_voltage=5000000,adapter_type;
static int events,mievents,last_sensor_vote;
static int64_t tick;
static struct connector_antiburn *allocation;
static struct gpio_desc *gpio_allocation;
static struct thermal_zone_device zones[2]={{25000,0},{25000,0}};
static struct mca_hwid hwid={2,2,2};
static bool work_draining;
static pthread_t active_work;
static pthread_barrier_t work_entered,work_release,drain_entered;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;allocation=calloc(1,n);assert(allocation);return allocation;}
static const struct mca_hwid *mca_get_hwid_info(void){return &hwid;}
static void platform_set_drvdata(struct platform_device *p,void *d){p->dev.data=d;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
static void *dev_get_drvdata(struct device *d){return d->data;}
#define INIT_DELAYED_WORK(w,f) do{(w)->work.fn=(f);(w)->work.pending=false;(w)->work.disabled=false;}while(0)
static struct workqueue_struct *alloc_ordered_workqueue(const char *n,int f){(void)n;(void)f;if(fault())return NULL;wq_live++;return calloc(1,sizeof(struct workqueue_struct));}
static bool queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,unsigned d){(void)d;assert(q);if(w->work.disabled)return false;w->work.pending=true;return true;}
static bool mod_delayed_work(struct workqueue_struct *q,struct delayed_work *w,unsigned d){return queue_delayed_work(q,w,d);}
static void disable_delayed_work_sync(struct delayed_work *w){assert(!notifiers&&!log_live&&!sysfs_live);if(work_draining){pthread_barrier_wait(&drain_entered);assert(!pthread_join(active_work,NULL));work_draining=false;}w->work.disabled=true;w->work.pending=false;}
static void destroy_workqueue(struct workqueue_struct *q){assert(q);free(q);wq_live--;}
static struct gpio_desc *devm_gpiod_get(struct device *d,const char *name,int flag){(void)d;(void)flag;assert(!strcmp(name,"mos-ctrl"));if(fault())return ERR_PTR(-ENODEV);gpio_allocation=calloc(1,sizeof(*gpio_allocation));assert(gpio_allocation);return gpio_allocation;}
static int gpiod_direction_output_raw(struct gpio_desc *p,int raw){assert(p&&!IS_ERR(p));if(fault()||direction_error)return -EIO;p->raw=raw;return 0;}
static int gpiod_get_raw_value_cansleep(struct gpio_desc *p){assert(p);return direction_error?direction_error:p->raw;}
static struct thermal_zone_device *thermal_zone_get_zone_by_name(const char *name){if(fault())return ERR_PTR(-ENODEV);return &zones[!strcmp(name,"conn_therm2")];}
static int thermal_zone_get_temp(struct thermal_zone_device *p,int *value){assert(p&&!IS_ERR(p));if(work_draining){pthread_barrier_wait(&work_entered);pthread_barrier_wait(&work_release);}if(p->error)return p->error;*value=p->temperature;return 0;}
static ktime_t ktime_get(void){return tick;}
static ktime_t ktime_sub(ktime_t a,ktime_t b){return a-b;}
static int64_t ktime_to_ms(ktime_t t){return t;}
static int64_t div64_s64(int64_t a,int64_t b){assert(b);return a/b;}
static void msleep(unsigned ms){(void)ms;}
static void connector_temp_uevent(int value){(void)value;}
static void adapter_reset_vsafe0V_uevent(int value){(void)value;}
static void connector_ntc_alarm_uevent(int value){(void)value;}
static int protocol_class_get_adapter_type(int role,int *value){(void)role;*value=adapter_type;return provider_error;}
static int protocol_class_pd_get_otg_plugin_status(int role,bool *value){(void)role;*value=otg;return provider_error;}
static int protocol_class_pd_get_cid_status(int role,bool *value){(void)role;*value=cid;return provider_error;}
static int platform_class_buckchg_ops_get_online(int role,int *value){(void)role;*value=usb_online;return provider_error;}
static int platform_class_buckchg_ops_get_bus_volt(int role,int *value){(void)role;*value=bus_voltage;return provider_error;}
static int platform_class_buckchg_ops_set_boost_enable(int role,int value){(void)role;(void)value;return provider_error;}
static int protocol_class_pd_request_vdm_cmd(int role,int cmd,unsigned *data,int n){(void)role;(void)cmd;(void)data;(void)n;return provider_error;}
static void mca_event_block_notify(int type,int event,void *data){(void)type;(void)event;(void)data;events++;}
static void mca_charge_mievent_report(int type,void *data,int n){(void)type;(void)data;(void)n;mievents++;}
static void mca_charge_mievent_set_state(int state,int event){(void)state;(void)event;}
static int mca_vote_named(const char *name,const char *client,bool enabled,int value){assert(!strcmp(name,"chg_enable")&&!strcmp(client,"antiburn_sensor_error")&&value==0);last_sensor_vote=enabled;return 0;}
static int mca_event_block_notify_register(int type,struct notifier_block *n){(void)type;assert(n->notifier_call);if(fault())return -EBUSY;notifiers++;return 0;}
static void mca_event_block_notify_unregister(int type,struct notifier_block *n){(void)type;(void)n;assert(notifiers>0);notifiers--;}
static void mca_log_charge_log_register(int type,const struct mca_log_charge_log_ops *ops,void *data){(void)type;assert(ops&&data);log_live++;}
static void mca_log_charge_log_unregister(int type,void *data){(void)type;assert(data&&log_live);log_live--;}
static int antiburn_sysfs_create_group(struct device *d){assert(d->data);if(fault())return -EIO;sysfs_live++;return 0;}
static void mca_sysfs_remove_link_group(int type,const char *name,struct device *d,const struct attribute_group *g){(void)type;(void)name;(void)d;(void)g;assert(sysfs_live);sysfs_live--;}
static void synchronize_srcu(struct srcu_struct *s){assert(!pthread_rwlock_wrlock(&s->readers));assert(!pthread_rwlock_unlock(&s->readers));}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,struct mca_sysfs_attr_info *table,size_t count){for(size_t i=0;i<count;i++)if(!strcmp(name,table[i].attr.attr.name))return table+i;return NULL;}
static int sysfs_emit(char *out,const char *format,int value){return snprintf(out,PAGE_SIZE,format,value);}
#define scnprintf snprintf
static int kstrtoint(const char *s,int base,int *v){char *end;errno=0;long n=strtol(s,&end,base);if(errno||s==end||(*end&&strcmp(end,"\n"))||n<INT32_MIN||n>INT32_MAX)return -EINVAL;*v=n;return 0;}
static void connector_antiburn_parse_dt(struct connector_antiburn *c){c->trigger_temp=65;c->recharge_temp=55;c->monitor_interval=1000;c->max_temp_increase_rate=4;c->support_hw_antiburn=1;c->support_soft_antiburn=1;c->use_double_ntc=1;c->thermal_zone_name="conn_therm";c->thermal_zone_name2="conn_therm2";c->comb_sensorboard_con_trigger_temp=60;c->comb_rate_conn_trigger_temp=35;c->max_thermal_board_temp=50;}
/* FUNCTIONS */
/* STOCK */
static void release_devm(void){if(allocation){pthread_mutex_destroy(&allocation->policy_lock.value);free(allocation);allocation=NULL;}free(gpio_allocation);gpio_allocation=NULL;}
static void *run_work(void *arg){struct connector_antiburn *c=arg;connector_antiburn_monitor_workfunc(&c->monitor_work.work);return NULL;}
static void *remove_device(void *arg){connector_antiburn_remove(arg);return NULL;}
int main(void){
 struct platform_device dev={0},duplicate={0};assert(connector_antiburn_is_triggered()==-ENODEV);
 sequence=0;assert(!connector_antiburn_probe(&dev));int points=sequence;
 assert(notifiers==2&&log_live==1&&sysfs_live==1&&wq_live==1);
 assert(connector_antiburn_probe(&duplicate)==-EEXIST);connector_antiburn_shutdown(&dev);connector_antiburn_remove(&dev);assert(!conn_claimed&&!g_conn&&!notifiers&&!log_live&&!sysfs_live&&!wq_live);release_devm();
 for(int f=1;f<=points;f++){sequence=0;fail_at=f;assert(connector_antiburn_probe(&dev)<0);assert(!conn_claimed&&!g_conn&&!notifiers&&!log_live&&!sysfs_live&&!wq_live&&!dev.dev.data);release_devm();}
 sequence=0;fail_at=0;assert(!connector_antiburn_probe(&dev));struct connector_antiburn *c=dev.dev.data;
 int value=777;zones[0].error=-EIO;assert(connector_antiburn_get_temperature(c,0,&value)==-EIO&&value==777);
 tick=1000;connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(last_sensor_vote&&!c->have_sample);zones[0].error=0;
 zones[0].temperature=INT32_MAX;assert(connector_antiburn_get_temperature(c,0,&value)==-ERANGE);zones[0].temperature=25000;
 c->use_double_ntc=0;assert(connector_antiburn_get_temperature(c,1,&value)==-EINVAL);connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(!last_sensor_vote);c->use_double_ntc=1;
 tick=2000;usb_online=1;zones[0].temperature=65000;zones[1].temperature=25000;connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(c->triggered&&c->mos_ctrl->raw==1);
 tick=3000;zones[0].temperature=56000;connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(c->triggered);
 tick=4000;zones[0].temperature=54000;direction_error=-EIO;connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(c->triggered&&last_sensor_vote);direction_error=0;
 tick=5000;connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(!c->triggered&&!c->mos_ctrl->raw&&!last_sensor_vote);
 provider_error=-EIO;tick=6000;connector_antiburn_monitor_workfunc(&c->monitor_work.work);assert(last_sensor_vote);provider_error=0;usb_online=0;
 struct connector_antiburn other=*c;c->have_sample=other.have_sample=false;c->temperature[0]=other.temperature[0]=25;
 tick=1000;connector_antiburn_get_temp_increase_rate(c);connector_antiburn_get_temp_increase_rate(&other);
 tick=1500;c->temperature[0]=35;other.temperature[0]=26;connector_antiburn_get_temp_increase_rate(c);connector_antiburn_get_temp_increase_rate(&other);assert(c->temp_increase_rate[0]==20&&other.temp_increase_rate[0]==2);
 tick=INT32_MAX+(int64_t)1500;c->temperature[0]=36;connector_antiburn_get_temp_increase_rate(c);assert(!c->temp_increase_rate[0]);
 struct device_attribute mos={{"mos_ctrl"}};assert(antiburn_sysfs_store(&dev.dev,&mos,"-1",2)==-EINVAL);assert(antiburn_sysfs_store(&dev.dev,&mos,"2",1)==-EINVAL);direction_error=-EIO;assert(antiburn_sysfs_store(&dev.dev,&mos,"1",1)==-EIO);direction_error=0;
 c->en_src=INT32_MAX;assert(connector_antiburn_validate_config(c)==-EINVAL);c->en_src=0;
 test_stock_config();
 struct device_attribute temp_attr={{"connector_temp_1"}};char text[64];int control_cases=0;
 for(int n=-600;n<=1600;n+=17){snprintf(text,sizeof(text),"%d",n);ssize_t ret=antiburn_sysfs_store(&dev.dev,&temp_attr,text,strlen(text));assert(ret==(n>=-500&&n<=1500?(ssize_t)strlen(text):-EINVAL));control_cases++;}
 c->fake_connector_temp[0]=0;
 zones[0].temperature=25000;zones[1].temperature=25000;c->have_sample=false;work_draining=true;
 pthread_barrier_init(&work_entered,NULL,2);pthread_barrier_init(&work_release,NULL,2);pthread_barrier_init(&drain_entered,NULL,2);
 assert(!pthread_create(&active_work,NULL,run_work,c));pthread_barrier_wait(&work_entered);
 pthread_t removal;assert(!pthread_create(&removal,NULL,remove_device,&dev));pthread_barrier_wait(&drain_entered);
 assert(!g_conn&&c->stopping&&wq_live==1);assert(connector_antiburn_is_triggered()==-ENODEV);
 /* The thermal mock blocks each read; release the second sensor as well. */
 pthread_barrier_wait(&work_release);pthread_barrier_wait(&work_entered);pthread_barrier_wait(&work_release);
 assert(!pthread_join(removal,NULL));assert(!conn_claimed&&!notifiers&&!log_live&&!sysfs_live&&!wq_live);release_devm();
 pthread_barrier_destroy(&work_entered);pthread_barrier_destroy(&work_release);pthread_barrier_destroy(&drain_entered);
 printf("{\"probe_failure_points\":%d,\"callback_work_unwind\":true,\"active_worker_removal_drains\":true,\"sensor_error_range_and_single_sensor_cases\":3,\"trigger_hysteresis_and_gpio_failure\":true,\"failed_reads_disable_software_charging\":true,\"independent_rate_history_and_long_uptime\":true,\"numeric_and_gpio_controls\":true,\"fake_temperature_control_cases\":%d}\n",points,control_cases);
}
