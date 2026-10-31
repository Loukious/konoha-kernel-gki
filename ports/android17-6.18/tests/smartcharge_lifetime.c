#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;typedef long long ktime_t;typedef int atomic_t;
#define mutex pthread_mutex_t
struct device{void *data;};struct work_struct{};struct delayed_work{struct work_struct work;int initialized,pending,disabled;};struct mca_votable{};struct notifier_block{int (*notifier_call)(struct notifier_block *,unsigned long,void *);};struct miscdevice{int minor;const char *name;void *fops;struct device *parent;};struct platform_device{struct device dev;};
/* SMART_HEADER */
static struct smart_charge_info *global_smartchg_info;
static pthread_mutex_t smart_instance_lock=PTHREAD_MUTEX_INITIALIZER;
static int smart_instance_srcu,system_wq,g_smt_chg_file_ops;
static int fail_stage,stage,resources[8],status_error,online_value;
#define GFP_KERNEL 0
#define SMART_CHARGE_FAKE_SCENE_DEFAULT -1
#define MISC_DYNAMIC_MINOR -1
#define MCA_EVENT_TYPE_PANEL 0
#define MCA_EVENT_TYPE_THERMAL_TEMP 1
#define STRATEGY_FUNC_TYPE_BUCK_CHARGE 0
#define STRATEGY_FUNC_TYPE_BASIC_WIRELESS 1
#define STRATEGY_FUNC_TYPE_SMARTCHG 2
#define STRATEGY_STATUS_TYPE_ONLINE 0
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define mutex_init(p) pthread_mutex_init((p),NULL)
#define atomic_set(p,v) (*(p)=(v))
#define rcu_access_pointer(p) (p)
#define rcu_assign_pointer(p,v) ((p)=(v))
struct mutex_guard {pthread_mutex_t *lock;};
static struct mutex_guard mutex_guard_init(pthread_mutex_t *l){pthread_mutex_lock(l);return (struct mutex_guard){l};}
static void mutex_guard_cleanup(struct mutex_guard *g){pthread_mutex_unlock(g->lock);}
#define guard(t) struct mutex_guard g __attribute__((cleanup(mutex_guard_cleanup)))=mutex_guard_init
static void synchronize_srcu(int *p){(void)p;assert(!global_smartchg_info);}
static void *devm_kzalloc(struct device *d,size_t n,int flags){(void)flags;if(fail_stage==-1)return NULL;d->data=calloc(1,n);return d->data;}
static void platform_set_drvdata(struct platform_device *p,void *v){p->dev.data=v;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
#define INIT_DELAYED_WORK(w,f) ((w)->initialized=1)
static void disable_delayed_work_sync(struct delayed_work *w){assert(w->initialized);w->pending=0;w->disabled=1;}
static void mod_delayed_work(int q,struct delayed_work *w,int ms){(void)q;(void)ms;assert(w->initialized&&!w->disabled);w->pending=1;}
static int get_resource(int n){assert(++stage==n);if(fail_stage==n)return -EREMOTEIO;resources[n]++;return 0;}
static void put_resource(int n){assert(resources[n]==1);resources[n]--;}
static int smart_charge_parse_dt(struct smart_charge_info *i){assert(i->fake_scene==-1);assert(i->smart_soc_limit_work.initialized);assert(++stage==1);return fail_stage==1?-EREMOTEIO:0;}
static int smart_charge_init_voter(struct smart_charge_info *i){(void)i;return get_resource(2);}
static void smart_charge_destroy_voters(struct smart_charge_info *i){(void)i;put_resource(2);}
static int misc_register(struct miscdevice *m){assert(!strcmp(m->name,"smart_charge_cdev"));return get_resource(3);}
static void misc_deregister(struct miscdevice *m){(void)m;put_resource(3);}
static int smart_charge_panel_notifier_cb(struct notifier_block *n,unsigned long e,void *v){(void)n;(void)e;(void)v;return 0;}
#define smart_charge_thermal_notifier_cb smart_charge_panel_notifier_cb
static int mca_event_block_notify_register(int type,struct notifier_block *n){assert(n->notifier_call);return get_resource(type?5:4);}
static void mca_event_block_notify_unregister(int type,struct notifier_block *n){(void)n;put_resource(type?5:4);}
static int mca_strategy_func_get_status(int type,int status,int *out){(void)status;if(status_error)return -EIO;*out=type?0:online_value;return 0;}
static int mca_smart_charge_process_event(int e,int v,void *p){(void)e;(void)v;(void)p;return 0;}
static int mca_strategy_ops_register(int type,int (*cb)(int,int,void *),void *a,void *b,void *data){(void)type;(void)cb;(void)a;(void)b;struct smart_charge_info *i=data;i->smart_charge_work.pending=1;return get_resource(6);}
static void mca_strategy_ops_unregister(int type,void *data){(void)type;(void)data;put_resource(6);}
static int smart_charge_sysfs_create_group(struct device *d){assert(global_smartchg_info==d->data);return get_resource(7);}
static void smart_charge_sysfs_remove_group(struct device *d){(void)d;put_resource(7);}
static void smart_charge_set_soc_limit(bool en){assert(!en);}
#define SMART_CHG_OUTDOOR_PWR_BOOST 0
static void smartcharging_handle_wls_super(struct smart_charge_info *i){(void)i;}
static void smartcharging_handle_wls_quiet(struct smart_charge_info *i){(void)i;}
static void smartcharging_handle_power_boost(struct smart_charge_info *i,int v){(void)i;(void)v;}
static int smart_charge_fv_control(struct mca_votable *v,void *i,int n,const char *c){(void)v;(void)i;(void)c;assert(!n);return 0;}
#define smart_charge_ichg_control smart_charge_fv_control
#define smart_charge_set_fcc_control smart_charge_fv_control
/* STAGED_DEFINITIONS */
int main(void){
 for(int f=-1;f<=7;f++){if(!f)continue;fail_stage=f;stage=0;struct platform_device p={0};int ret=smart_charge_probe(&p);assert(ret==(f==-1?-ENOMEM:-EREMOTEIO));assert(!global_smartchg_info);for(int i=2;i<=7;i++)assert(!resources[i]);if(p.dev.data){struct smart_charge_info *d=p.dev.data;assert(!d->smart_charge_work.pending);free(d);}}
 fail_stage=0;stage=0;status_error=1;struct platform_device p={0};assert(!smart_charge_probe(&p));struct smart_charge_info *i=p.dev.data;assert(!i->online&&i->replay_needed==1&&i->fake_scene==-1);struct platform_device duplicate={0};assert(smart_charge_probe(&duplicate)==-EBUSY);smart_charge_remove(&p);assert(!global_smartchg_info&&i->stopping&&i->smart_charge_work.disabled&&i->smart_soc_limit_work.disabled&&i->smart_sense_chg_work.disabled);for(int n=2;n<=7;n++)assert(!resources[n]);free(i);
 puts("{\"passed\":true,\"registration_failure_points\":8,\"complete_resource_unwind\":true,\"three_workers_disabled\":true,\"duplicate_device_rejected\":true,\"failed_initial_status_not_online\":true}");
}
