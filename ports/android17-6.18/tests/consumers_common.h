#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
typedef uint32_t u32;
typedef uint8_t u8;
typedef int32_t s32;
#define GFP_KERNEL 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define clamp(v,a,b) ((v)<(a)?(a):(v)>(b)?(b):(v))
#define min(a,b) ((a)<(b)?(a):(b))
#define READ_ONCE(v) __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v),(x),__ATOMIC_RELEASE)
#define mca_log_err(...) ((void)0)
#define mca_log_info(...) ((void)0)
#define FG_IC_MASTER 0
#define FG_IC_MAX 2
#define FG_SITE_MAX 2
#define TEMP_PARA_MAX_GROUP 10
#define VOLT_PARA_MAX_GROUP 10
#define MCA_QUICK_CHG_NAME_LEN 16
#define CHG_MODE_MAX 3
#define CHG_MODE_DIV1 0
#define CHG_MODE_DIV2 1
#define CHG_MODE_DIV4 2
#define CP_ROLE_MAX 2
#define CP_ROLE_MASTER 0
#define CP_ROLE_SLAVE 1
#define MCA_QUICK_CHG_MODE_DIV_1 1
#define MCA_QUICK_CHG_MODE_DIV_2 2
#define MCA_QUICK_CHG_MODE_DIV_4 4
#define JEITA_DATA_MAX_GROUP 15
#define JEITA_VOLT_DATA_MAX_GROUP 4
#define EPROBE_DEFER 517
struct mutex { pthread_mutex_t lock; };
static pthread_mutex_t phase_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t phase_cond=PTHREAD_COND_INITIALIZER;
static int phase;
static struct mutex *tracked_jeita;
static pthread_t transaction_thread;
static bool track_cycle;
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->lock,NULL)); }
static void mutex_lock(struct mutex *m) {
 assert(!pthread_mutex_lock(&m->lock));
 if(track_cycle && m==tracked_jeita && pthread_equal(pthread_self(),transaction_thread)) {
  pthread_mutex_lock(&phase_lock);phase=2;pthread_cond_broadcast(&phase_cond);pthread_mutex_unlock(&phase_lock);
 }
}
static bool mutex_trylock(struct mutex *m) { int r=pthread_mutex_trylock(&m->lock);assert(r==0 || r==EBUSY);return r==0; }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->lock)); }
struct lock_guard { struct mutex *m; };
static struct lock_guard make_mutex_guard(struct mutex *m) { mutex_lock(m);return (struct lock_guard){m}; }
static void mutex_guard_cleanup(struct lock_guard *g) { mutex_unlock(g->m); }
struct srcu_guard { pthread_rwlock_t *rw; };
static struct srcu_guard make_srcu_guard(pthread_rwlock_t *rw) {assert(!pthread_rwlock_rdlock(rw));return (struct srcu_guard){rw};}
static void srcu_guard_cleanup(struct srcu_guard *g) {assert(!pthread_rwlock_unlock(g->rw));}
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define guard(kind) GUARD_##kind
#define GUARD_mutex struct lock_guard __attribute__((cleanup(mutex_guard_cleanup))) CAT(mg_,__COUNTER__)=make_mutex_guard
#define GUARD_srcu struct srcu_guard __attribute__((cleanup(srcu_guard_cleanup))) CAT(sg_,__COUNTER__)=make_srcu_guard
static int allocations,attempt,fail_at;
static void *kcalloc(size_t n,size_t s,int flags) {
 (void)flags;if(++attempt==fail_at)return NULL;
 void *p=calloc(n,s);if(p)allocations++;return p;
}
static void *kmemdup(const void *in,size_t size,int flags) {void *p=kcalloc(1,size,flags);if(p)memcpy(p,in,size);return p;}
static void kfree(void *p) {if(p){allocations--;free(p);}}
struct work_struct {int unused;};
struct delayed_work {struct work_struct work;};
static struct work_struct *last_scheduled;
static int scheduled,disabled,stopped,voters_destroyed;
static void schedule_work(struct work_struct *work) {last_scheduled=work;scheduled++;}
static void mod_delayed_work(void *wq,struct delayed_work *work,int delay) {(void)wq;(void)work;(void)delay;scheduled++;}
static void disable_delayed_work_sync(struct delayed_work *work) {(void)work;disabled++;}
static void cancel_work_sync(struct work_struct *work) {(void)work;}
static void *system_wq;
struct mca_votable {int unused;};
struct notifier_block {int unused;};
struct device {int unused;};
struct platform_device {void *data;};
static void *platform_get_drvdata(struct platform_device *p) {return p->data;}
/* HEADER */
/* TABLE_STRUCTS */
struct mca_qc_basp_pending;
struct mca_quick_charge_info {
 struct device *dev;
 struct mutex policy_lock;
 struct work_struct stop_work;
 bool ready,stopping,online,init_volt_para;
 int force_stop;
 struct {int chg_enable;int cp_path_enable[CHG_MODE_MAX][CP_ROLE_MAX];} sysfs_data;
 struct mca_quick_charge_batt_para_info batt_para[FG_IC_MAX],base_flip_para[FG_SITE_MAX];
 struct mca_qc_basp_pending *pending_basp;
 u32 pending_basp_count;
 struct {int zone_changed,cur_stage[FG_IC_MAX];struct {int secure_cur;} secure_info;} proc_data;
 struct delayed_work monitor_work,pps_ptf_work,vfc_work;
 struct notifier_block shutdown_notifier;
};
struct mca_buckchg_jeita_dev {
 struct mutex state_lock;
 bool ready,stopping,baacfg_update,online;
 struct mca_buckchg_jeita_para jeita_para;
 struct smart_batt_jeita_term_para *pending_basp;
 u32 pending_ffc_size,pending_normal_size;
 struct {int cur_jeita_index;} proc_data;
 struct delayed_work monitor_work;
};
struct smart_charge_info {struct mutex state_lock;bool stopping;int enable_fv_dec_by_cc;};
struct smart_basp_transport {struct mutex lock;struct smart_charge_info *info;};
struct smart_basp_decoded {struct mca_basp_update update[3];};
static pthread_rwlock_t smart_ops_srcu=PTHREAD_RWLOCK_INITIALIZER;
static struct mca_smartchg_if_ops *mock_ops[MCA_SMARTCHG_IF_CHG_TYPE_END];
static struct mca_smartchg_if_ops *mca_smartchg_if_get_ops(unsigned int type) {return mock_ops[type];}
static int removal_entered;
void mca_smartchg_if_ops_unregister(unsigned int type,void *data) {
 WRITE_ONCE(removal_entered,1);
 assert(!pthread_rwlock_wrlock(&smart_ops_srcu));
 assert(!mock_ops[type] || mock_ops[type]->data==data);
 mock_ops[type]=NULL;
 assert(!pthread_rwlock_unlock(&smart_ops_srcu));
}
static void mca_charge_if_ops_unregister(const char *name,void *data) {(void)name;(void)data;}
static void mca_strategy_ops_unregister(int type,void *data) {(void)type;(void)data;}
#define STRATEGY_FUNC_TYPE_QUICK_CHARGE 0
static void mca_quick_charge_remove_group(struct device *dev) {(void)dev;}
static void unregister_reboot_notifier(struct notifier_block *nb) {(void)nb;}
static void mca_quick_charge_stop_charging(struct mca_quick_charge_info *info) {(void)info;stopped++;}
static void mca_quick_charge_destroy_voters(struct mca_quick_charge_info *info) {(void)info;voters_destroyed++;}
static int measured_voltage,voltage_error;
static int strategy_class_fg_ops_get_voltage(int *value) {*value=measured_voltage;return voltage_error;}
/* FUNCTIONS */
static struct smart_batt_spec_curve curve[]={{4200,8000,1000},{4500,4000,500}};
static struct smart_batt_spec specs[2];
static struct smart_batt_jeita_term_para terms[2];
static struct mca_quick_charge_info qc;
static struct mca_buckchg_jeita_dev jeita;
static struct smart_charge_info smart;
static struct smart_basp_transport transport;
static struct smart_basp_decoded decoded;
static struct mca_smartchg_if_ops qc_ops,jeita_ops;
static void setup(void) {
 memset(&qc,0,sizeof(qc));memset(&jeita,0,sizeof(jeita));memset(&smart,0,sizeof(smart));memset(&transport,0,sizeof(transport));
 mutex_init(&qc.policy_lock);mutex_init(&jeita.state_lock);mutex_init(&smart.state_lock);mutex_init(&transport.lock);
 qc.ready=jeita.ready=true;smart.enable_fv_dec_by_cc=1;transport.info=&smart;
 struct mca_quick_charge_batt_para_info *b=&qc.batt_para[0];b->temp_para_size=1;b->temp_info=kcalloc(1,sizeof(*b->temp_info),0);
 b->temp_info[0].temp_para=(struct mca_quick_charge_temp_para){.temp_low=10,.temp_high=50,.max_current=10000,.normal_max_fv=4600,.ffc_max_fv=4600};
 for(int f=0;f<2;f++) {
  struct mca_quick_charge_volt_para_info *v=f?&b->temp_info[0].volt_ffc_info:&b->temp_info[0].volt_info;
  v->volt_para_size=2;v->volt_para=kcalloc(2,sizeof(*v->volt_para),0);v->stage_para=kcalloc(4,sizeof(int),0);
  v->volt_para[0]=(struct mca_quick_charge_volt_para){4200,8000,1000};v->volt_para[1]=(struct mca_quick_charge_volt_para){4600,4000,500};v->stage_para[0]=1;
 }
 jeita.jeita_para.size=jeita.jeita_para.fcc_size=1;
 jeita.jeita_para.jeita_data=kcalloc(1,sizeof(*jeita.jeita_para.jeita_data),0);
 jeita.jeita_para.jeita_ffc_data=kcalloc(1,sizeof(*jeita.jeita_para.jeita_ffc_data),0);
 for(int f=0;f<2;f++) {
  struct mca_buckchg_jeita_data *t=f?jeita.jeita_para.jeita_ffc_data:jeita.jeita_para.jeita_data;
  *t=(struct mca_buckchg_jeita_data){.temp_low=10,.temp_high=50,.max_current=10000,.vterm=4600,.design_vterm=4600,.iterm=500};
  t->volt_para.size=0;
  terms[f]=(struct smart_batt_jeita_term_para){.t_range={.idx=0,.min=10,.max=50},.vterm=4500,.iterm=450};
  specs[f]=(struct smart_batt_spec){.type=1,.ffc=f==0,.t_range={.idx=0,.min=10,.max=50},.step_size=2,.steps=curve};
 }
 jeita_ops=(struct mca_smartchg_if_ops){.data=&jeita,.prepare_baa_para=mca_jeita_basp_prepare,.commit_baa_para=mca_jeita_basp_commit,.abort_baa_para=mca_jeita_basp_abort};
 qc_ops=(struct mca_smartchg_if_ops){.data=&qc,.prepare_baa_para=mca_qc_basp_prepare,.commit_baa_para=mca_qc_basp_commit,.abort_baa_para=mca_qc_basp_abort};
 mock_ops[MCA_SMARTCHG_IF_CHG_TYPE_JEITA]=&jeita_ops;mock_ops[MCA_SMARTCHG_IF_CHG_TYPE_QC]=&qc_ops;
 decoded=(struct smart_basp_decoded){.update={{.ffc_size=1,.normal_size=1,.jeita=terms},{.ffc_size=1,.normal_size=1,.spec=specs}}};
 attempt=fail_at=0;
}
static void cleanup(void) {
 mca_qc_free_tables(&qc);kfree(jeita.jeita_para.jeita_data);kfree(jeita.jeita_para.jeita_ffc_data);
 assert(allocations==0);
 assert(!pthread_mutex_destroy(&qc.policy_lock.lock));assert(!pthread_mutex_destroy(&jeita.state_lock.lock));assert(!pthread_mutex_destroy(&smart.state_lock.lock));assert(!pthread_mutex_destroy(&transport.lock.lock));
}
