#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint32_t u32;
struct mutex {pthread_mutex_t value;};
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->value,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->value));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->value));}
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define DEFINE_STATIC_SRCU(n) static pthread_rwlock_t n=PTHREAD_RWLOCK_INITIALIZER
#define __rcu
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
#define rcu_access_pointer(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define srcu_dereference(p,s) rcu_access_pointer(p)
static int srcu_read_lock(pthread_rwlock_t *s){assert(!pthread_rwlock_rdlock(s));return 0;}
static void srcu_read_unlock(pthread_rwlock_t *s,int i){(void)i;assert(!pthread_rwlock_unlock(s));}
static void synchronize_srcu(pthread_rwlock_t *s){assert(!pthread_rwlock_wrlock(s));assert(!pthread_rwlock_unlock(s));}
#define ARRAY_SIZE(v) (sizeof(v)/sizeof(v[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(v) ((int)(intptr_t)(v))
#define IS_ERR(v) ((uintptr_t)(v)>=(uintptr_t)-4095)
#define GFP_KERNEL 1
#define WQ_MEM_RECLAIM 1
#define CONFIG_SYSFS 1
#define PAGE_SIZE 4096
#define min(a,b) ((a)<(b)?(a):(b))
#define msecs_to_jiffies(v) (v)
#define mca_log_err(...) ((void)0)
#define mca_log_info(...) ((void)0)
struct device_node {int n;};struct device {void *data;struct device_node *of_node;};struct platform_device {struct device dev;};
struct work_struct {void(*fn)(struct work_struct *);bool pending;};struct delayed_work {struct work_struct work;};struct workqueue_struct {int id;};
struct attribute {const char *name;};struct device_attribute {struct attribute attr;};struct mca_sysfs_attr_info {struct device_attribute attr;int sysfs_attr_name;};struct attribute_group {struct attribute **attrs;};
#define mca_sysfs_attr_rw(p,m,i,n) {{{#n}},i}
struct thermal_cooling_device {void *devdata;};struct thermal_cooling_device_ops {int(*get_max_state)(struct thermal_cooling_device *,unsigned long *);int(*get_cur_state)(struct thermal_cooling_device *,unsigned long *);int(*set_cur_state)(struct thermal_cooling_device *,unsigned long);};
struct mca_smartchg_if_ops {int type;void *data;int(*set_wls_super_sts)(void *,int);};struct mca_log_charge_log_ops {int(*dump_log_head)(void *,char *,int);int(*dump_log_context)(void *,char *,int);};
struct mca_votable {bool enabled;int ma;};
/* CONSTANTS */
/* HEADER */
/* GLOBALS */
/* DECLARATIONS */
/* TABLES */
static int sequence,fail_at,live_group,live_wq,live_tcd,live_strategy,live_smart,live_log,drains;
static int dt_wireless,dt_flip,dt_count=150,dt_bad_support,dt_sic_count,provider_error,qc_power=80,cell_error,smem_error,zero_speed,voter_error,missing_voter;
static unsigned int power=90;static unsigned long boottime=100;static u32 table[MAX_THERMAL_LEVEL*THERMAL_MODE_MAX];
static const char *cell="A";static struct mca_thermal_info *allocation;static struct mca_votable voters[THERMAL_MODE_MAX*2+2];
static bool stop_hook;static pthread_barrier_t reached_vote,release_vote;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;allocation=calloc(1,n);assert(allocation);return allocation;}
static void platform_set_drvdata(struct platform_device *d,void *p){d->dev.data=p;}
static void *platform_get_drvdata(struct platform_device *d){return d->dev.data;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static void *of_find_property(struct device_node *n,const char *p,void *out){(void)n;(void)out;if(!strcmp(p,"support_wireless"))return n;if(!strcmp(p,"wired_sic_select"))return dt_sic_count?n:NULL;return NULL;}
static int of_property_read_bool(struct device_node *n,const char *p){(void)n;(void)p;return dt_flip;}
static int of_property_read_u32(struct device_node *n,const char *p,u32 *v){(void)n;assert(!strcmp(p,"support_wireless"));if(fault())return -EINVAL;*v=dt_bad_support?2:dt_wireless;return 0;}
static int of_property_count_u32_elems(struct device_node *n,const char *p){(void)n;if(fault())return -EINVAL;return strstr(p,"flip")?16:dt_count;}
static int of_property_read_u32_array(struct device_node *n,const char *p,u32 *out,int count){(void)n;(void)p;if(fault())return -EIO;memcpy(out,table,count*sizeof(u32));return 0;}
static int of_property_count_strings(struct device_node *n,const char *p){(void)n;(void)p;return dt_sic_count;}
static int of_property_read_string_array(struct device_node *n,const char *p,const char **out,int count){(void)n;(void)p;assert(count<=3);out[0]="A";out[1]="B";out[2]="C";return count;}
#define INIT_DELAYED_WORK(w,f) do{(w)->work.fn=f;(w)->work.pending=false;}while(0)
static struct workqueue_struct *alloc_ordered_workqueue(const char *n,int f){(void)f;assert(!strcmp(n,"mca_thermal"));if(fault())return NULL;live_wq++;return calloc(1,sizeof(struct workqueue_struct));}
static int queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay){assert(q&&w->work.fn&&delay==1000);w->work.pending=true;return 1;}
static void cancel_delayed_work_sync(struct delayed_work *w){assert(w->work.fn);drains++;w->work.pending=false;}
static void destroy_workqueue(struct workqueue_struct *q){assert(live_wq==1&&!live_group&&!live_tcd&&!live_strategy&&!live_smart&&!live_log);live_wq--;free(q);}
static void mca_sysfs_init_attrs(struct attribute **a,struct mca_sysfs_attr_info *f,int n){(void)a;(void)f;assert(n==8);}
static int mca_sysfs_create_link_group(const char *p,const char *n,struct device *d,const struct attribute_group *g){(void)g;assert(!strcmp(p,"charger")&&!strcmp(n,"charger_thermal")&&d->data);if(fault())return -EIO;live_group++;return 0;}
static void mca_sysfs_remove_link_group(const char *p,const char *n,struct device *d,const struct attribute_group *g){(void)p;(void)n;(void)d;(void)g;assert(live_group==1);live_group--;}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *n,struct mca_sysfs_attr_info *t,int c){for(int i=0;i<c;i++)if(!strcmp(n,t[i].attr.attr.name))return t+i;return NULL;}
static int sysfs_emit(char *buf,const char *fmt,...){va_list ap;va_start(ap,fmt);int ret=vsnprintf(buf,PAGE_SIZE,fmt,ap);va_end(ap);return ret;}
static int scnprintf(char *buf,int size,const char *fmt,...){va_list ap;va_start(ap,fmt);int ret=vsnprintf(buf,size,fmt,ap);va_end(ap);return ret<size?ret:size-1;}
static int kstrtoint(const char *s,int base,int *out){char *end;errno=0;long v=strtol(s,&end,base);if(errno||end==s||(*end&&strcmp(end,"\n"))||v<INT_MIN||v>INT_MAX)return -EINVAL;*out=v;return 0;}
static int mca_strategy_ops_register(int t,int(*event)(int,int,void *),void *get,void *cfg,void *data){(void)t;(void)get;(void)cfg;assert(event&&data&&allocation->workqueue);if(fault())return -ENOMEM;live_strategy++;return 0;}
static void mca_strategy_ops_unregister(int t,void *d){(void)t;assert(live_strategy==1&&d==allocation);live_strategy--;}
static struct thermal_cooling_device *thermal_of_cooling_device_register(struct device_node *n,const char *name,void *data,const struct thermal_cooling_device_ops *ops){assert(n&&!strcmp(name,"battery")&&ops->get_max_state);if(fault())return ERR_PTR(-EIO);live_tcd++;struct thermal_cooling_device *t=calloc(1,sizeof(*t));t->devdata=data;return t;}
static void thermal_cooling_device_unregister(struct thermal_cooling_device *t){assert(live_tcd==1);live_tcd--;free(t);}
static int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops){assert(ops->data==allocation&&ops->set_wls_super_sts);if(fault())return -ENOMEM;live_smart++;return 0;}
static void mca_smartchg_if_ops_unregister(int t,void *d){(void)t;assert(live_smart==1&&d==allocation);live_smart--;}
static void mca_log_charge_log_register(int t,struct mca_log_charge_log_ops *o,void *d){(void)t;assert(o->dump_log_head&&o->dump_log_context&&d==allocation);live_log++;}
static void mca_log_charge_log_unregister(int t,void *d){(void)t;assert(live_log==1&&d==allocation);live_log--;}
static struct mca_votable *mca_find_votable(const char *n){if(missing_voter)return NULL;for(int i=0;i<10;i++){if(!strcmp(n,wired_voter_names[i]))return voters+i;if(!strcmp(n,wireless_voter_names[i]))return voters+10+i;}if(!strcmp(n,"thermal_flip"))return voters+20;if(!strcmp(n,"wls_thermal_flip"))return voters+21;assert(0);return NULL;}
static int mca_vote(struct mca_votable *v,const char *c,bool enabled,int ma){assert(v&&c&&ma>=0);if(stop_hook){stop_hook=false;pthread_barrier_wait(&reached_vote);pthread_barrier_wait(&release_vote);}v->enabled=enabled;v->ma=ma;return voter_error;}
static unsigned long ktime_get_boottime_seconds(void){return boottime;}
static int get_smem_battery_info(int *v){if(smem_error)return smem_error;*v=zero_speed;return 0;}
static int mca_strategy_func_process(int t,int e,int v){(void)t;(void)e;(void)v;return 0;}
static int mca_strategy_func_get_status(int t,int s,int *v){(void)t;(void)s;if(provider_error)return provider_error;*v=qc_power;return 0;}
static int protocol_class_get_adapter_pwr_max_power(int t,unsigned int *p){(void)t;if(provider_error)return provider_error;*p=power;return 0;}
static int protocol_class_get_adapter_max_power(int t,unsigned int *p){return protocol_class_get_adapter_pwr_max_power(t,p);}
static int platform_fg_ops_get_batt_cell_info(int t,const char **p){(void)t;if(cell_error)return cell_error;*p=cell;return 0;}
/* FUNCTIONS */
static void release(void){assert(!live_group&&!live_wq&&!live_tcd&&!live_strategy&&!live_smart&&!live_log&&!thermal_claimed&&!thermal_instance);if(allocation){pthread_mutex_destroy(&allocation->policy_lock.value);free(allocation);allocation=NULL;}}
static void reset(void){sequence=fail_at=drains=dt_wireless=dt_flip=dt_bad_support=dt_sic_count=provider_error=cell_error=smem_error=zero_speed=voter_error=missing_voter=0;dt_count=150;cell="A";boottime=100;power=90;qc_power=80;memset(voters,0,sizeof(voters));memcpy(table,stock_wired_table,sizeof(stock_wired_table));}
static void *setter(void *p){(void)p;assert(!mca_set_wls_charger_thermal_remove(true));return NULL;}
static void *remove_dev(void *p){mca_charger_thermal_remove(p);return NULL;}
int main(void){
 alarm(25);struct device_node node={0};struct platform_device dev={.dev.of_node=&node};reset();assert(!mca_charger_thermal_probe(&dev));int wired_faults=sequence;struct mca_thermal_info *i=allocation;unsigned long state=100;assert(!mca_charger_thermal_get_max_level(i->tcd,&state)&&state==15);assert(mca_charger_thermal_set_cur_level(i->tcd,16)==-ERANGE&&i->wired_ctrl_info.limit_level==0);assert(mca_charger_thermal_set_cur_level(i->tcd,ULONG_MAX)==-ERANGE);assert(mca_charger_thermal_process_event(MCA_EVENT_USB_CONNECT,2,i)==-EINVAL);assert(!mca_charger_thermal_process_event(MCA_EVENT_USB_CONNECT,1,i));int cases=0;
 for(int level=0;level<=15;level++){assert(!mca_charger_thermal_set_cur_level(i->tcd,level));for(int c=2;c<10;c++)assert(voters[c].ma==(level?(int)stock_wired_table[(level-1)*10+c]:0));cases++;}
 assert(!thermal_write_control(i,MCA_THERMAL_WIRED_CHG_CURR,2000000)&&i->wired_ctrl_info.chg_curr==2000);assert(!thermal_write_control(i,MCA_THERMAL_WIRED_CTRL_LIMIT,1)&&voters[4].ma==2000);assert(!thermal_write_control(i,MCA_THERMAL_WIRED_THERMAL_REMOVE,1));for(int c=0;c<10;c++)assert(!voters[c].enabled);assert(!thermal_write_control(i,MCA_THERMAL_WIRED_THERMAL_REMOVE,0));assert(thermal_write_control(i,MCA_THERMAL_WIRED_THERMAL_REMOVE,2)==-ERANGE);assert(thermal_write_control(i,MCA_THERMAL_WIRED_CTRL_LIMIT,-1)==-ERANGE);assert(thermal_write_control(i,MCA_THERMAL_WIRED_CTRL_LIMIT,16)==-ERANGE);assert(thermal_write_control(i,MCA_THERMAL_WIRELESS_CHG_CURR,1)==-EOPNOTSUPP);assert(mca_get_wls_charger_thermal_remove(NULL)==-EINVAL);bool remove=false;assert(mca_get_wls_charger_thermal_remove(&remove)==-EOPNOTSUPP);cases+=10;
 char buf[PAGE_SIZE];struct device_attribute attr={{"wired_ctrl_limit"}};const char *bad[]={"", "-1", "16", "abc", "1 junk", "2147483648"};for(unsigned c=0;c<ARRAY_SIZE(bad);c++){int before=i->wired_ctrl_info.limit_level;assert(mca_charger_thermal_sysfs_store(&dev.dev,&attr,bad[c],strlen(bad[c]))<0&&i->wired_ctrl_info.limit_level==before);cases++;}assert(mca_charger_thermal_sysfs_store(&dev.dev,&attr,"2\0xx",4)==-EINVAL);assert(mca_charger_thermal_sysfs_store(&dev.dev,&attr,"2\n",2)==2);assert(mca_charger_thermal_sysfs_show(&dev.dev,&attr,buf)>0&&!strcmp(buf,"2\n"));
 i->wired_sic_count=2;i->wired_sic_select[0]="A";i->wired_sic_select[1]="B";cell_error=-EAGAIN;assert(thermal_write_control(i,MCA_THERMAL_WIRED_CHG_CURR,1000)==-EAGAIN);cell_error=0;cell="A-junk";assert(thermal_write_control(i,MCA_THERMAL_WIRED_CHG_CURR,1000)==-ENODEV);cell="B";assert(!thermal_write_control(i,MCA_THERMAL_WIRED_CHG_CURR2,1000));i->real_type=XM_CHARGER_TYPE_PPS;provider_error=-EIO;int before=i->wired_ctrl_info.sic_init_fcc;assert(thermal_initial_current(i)==-EIO&&i->wired_ctrl_info.sic_init_fcc==before);provider_error=0;power=121;assert(!thermal_initial_current(i)&&i->wired_ctrl_info.sic_init_fcc==22000);power=0;assert(thermal_initial_current(i)==-ERANGE);voter_error=-EIO;assert(thermal_write_control(i,MCA_THERMAL_WIRED_CTRL_LIMIT,1)==-EIO);voter_error=0;assert(!mca_charger_thermal_process_event(MCA_EVENT_USB_DISCONNECT,0,i));for(int c=0;c<10;c++)assert(!voters[c].enabled);cases+=8;
 mca_charger_thermal_shutdown(&dev);assert(drains==1);mca_charger_thermal_remove(&dev);assert(drains==1&&!dev.dev.data);release();for(int f=1;f<=wired_faults;f++){reset();fail_at=f;assert(mca_charger_thermal_probe(&dev)<0&&!dev.dev.data);release();}
 reset();dt_wireless=1;assert(!mca_charger_thermal_probe(&dev));int wireless_faults=sequence;mca_charger_thermal_remove(&dev);release();for(int f=1;f<=wireless_faults;f++){reset();dt_wireless=1;fail_at=f;assert(mca_charger_thermal_probe(&dev)<0&&!dev.dev.data);release();}
 int schema=0;int counts[]={0,1,149,161,170,-EINVAL};for(unsigned c=0;c<ARRAY_SIZE(counts);c++){reset();dt_count=counts[c];assert(mca_charger_thermal_probe(&dev)==-EINVAL);release();schema++;}reset();table[0]=UINT32_MAX;assert(mca_charger_thermal_probe(&dev)==-ERANGE);release();schema++;reset();dt_bad_support=1;assert(mca_charger_thermal_probe(&dev)==-EINVAL);release();schema++;reset();dt_sic_count=4;assert(mca_charger_thermal_probe(&dev)==-EINVAL);release();schema++;
 reset();missing_voter=1;assert(!mca_charger_thermal_probe(&dev));i=allocation;assert(i->init_voter_work.work.pending);assert(!mca_charger_thermal_process_event(MCA_EVENT_USB_CONNECT,1,i));assert(!mca_charger_thermal_set_cur_level(i->tcd,3));missing_voter=0;i->init_voter_work.work.pending=false;mca_charger_thermal_init_voter_work(&i->init_voter_work.work);assert(i->wired_ctrl_info.voter_ok&&voters[4].ma==9000&&!i->init_voter_work.work.pending);mca_charger_thermal_remove(&dev);release();
 reset();dt_wireless=1;dt_count=160;for(int c=0;c<160;c++)table[c]=5000;assert(!mca_charger_thermal_probe(&dev));i=allocation;assert(!mca_charger_thermal_process_event(MCA_EVENT_WIRELESS_CONNECT,1,i));assert(!thermal_write_control(i,MCA_THERMAL_WIRELESS_QUICK_CHG_CTRL_LIMIT,16));assert(!mca_charger_thermal_wls_super_sts_callback(i,1));i->wireless_ctrl_info.quickchg_limit_level=17;assert(thermal_apply_wireless(i)==-ERANGE);i->wireless_ctrl_info.quickchg_limit_level=-1;assert(thermal_apply_wireless(i)==-ERANGE);i->wireless_ctrl_info.quickchg_limit_level=16;assert(!mca_get_wls_charger_thermal_remove(&remove)&&!remove);cases+=5;
 stop_hook=true;pthread_barrier_init(&reached_vote,NULL,2);pthread_barrier_init(&release_vote,NULL,2);pthread_t worker,removing;assert(!pthread_create(&worker,NULL,setter,NULL));pthread_barrier_wait(&reached_vote);assert(!pthread_create(&removing,NULL,remove_dev,&dev));pthread_barrier_wait(&release_vote);assert(!pthread_join(worker,NULL));assert(!pthread_join(removing,NULL));assert(mca_get_wls_charger_thermal_remove(&remove)==-ENODEV);release();
 reset();dt_flip=1;assert(!mca_charger_thermal_probe(&dev));i=allocation;assert(i->wired_ctrl_info.max_level==15);assert(!mca_charger_thermal_process_event(MCA_EVENT_USB_CONNECT,1,i));for(int level=0;level<16;level++){assert(!mca_charger_thermal_set_cur_level(i->tcd,level)&&voters[20].ma==(int)table[level]);cases++;}assert(mca_charger_thermal_set_cur_level(i->tcd,16)==-ERANGE);mca_charger_thermal_remove(&dev);assert(!voters[20].enabled);release();
 printf("{\"probe_failure_points\":%d,\"table_control_level_provider_cases\":%d,\"invalid_dt_schemas\":%d,\"actual_overlay_levels\":15,\"deferred_voter_replay\":true,\"selected_wireless_level_bounds\":true,\"active_exported_setter_removal\":true,\"all_registrations_and_work_drained\":true,\"shutdown_remove_idempotent\":true}\n",wired_faults+wireless_faults,cases,schema);
}
