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
#define GFP_KERNEL 1
#define WQ_MEM_RECLAIM 1
#define CONFIG_SYSFS 1
#define PAGE_SIZE 4096
#define EPROBE_DEFER 517
#define GPIOF_OUT_INIT_LOW 0
#define BIT(n) (1u<<(n))
#define msecs_to_jiffies(v) (v)
#define mca_log_err(...) ((void)0)
#define mca_log_info(...) ((void)0)
struct device_node {int n;};struct device {void *data;struct device_node *of_node;};struct platform_device {struct device dev;};
struct work_struct {void(*fn)(struct work_struct *);bool pending;};struct delayed_work {struct work_struct work;};struct workqueue_struct {int id;};
struct attribute {const char *name;};struct device_attribute {struct attribute attr;};struct mca_sysfs_attr_info {struct device_attribute attr;int sysfs_attr_name;};struct attribute_group {struct attribute **attrs;};
#define mca_sysfs_attr_rw(p,m,i,n) {{{#n}},i}
/* CONSTANTS */
/* HEADER */
/* GLOBALS */
/* DECLARATIONS */
/* TABLE */
static int sequence,fail_at,live_group,live_wq,owned_gpio,drains,provider_error,cp_error,cp_calls,cp_enable,usb_value,otg_value,boost_source=2,chip_value,chip_count=1,gpio_lookup_error,gpio_direction_error;
static const char *bad_name,*bad_value,*bad_count_name;static int bad_index,bad_count;
static struct mca_path_control *allocation;static void *resources[1024];static int resource_count;
static bool stop_hook;static pthread_barrier_t reached_provider,release_provider;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kcalloc(struct device *d,size_t n,size_t size,int f){(void)d;(void)f;if(fault())return NULL;void *p=calloc(n,size);assert(p&&resource_count<1024);resources[resource_count++]=p;return p;}
static void *devm_kzalloc(struct device *d,size_t n,int f){allocation=devm_kcalloc(d,1,n,f);return allocation;}
static void platform_set_drvdata(struct platform_device *d,void *p){d->dev.data=p;}
static void *platform_get_drvdata(struct platform_device *d){return d->dev.data;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static const struct dt_property *property(const char *name){for(unsigned i=0;i<ARRAY_SIZE(dt_properties);i++)if(!strcmp(name,dt_properties[i].name))return dt_properties+i;return NULL;}
static int of_property_count_strings(struct device_node *n,const char *name){(void)n;if(fault())return -EIO;if(bad_count_name&&!strcmp(name,bad_count_name))return bad_count;const struct dt_property *p=property(name);return p?p->count:-EINVAL;}
static int of_property_read_string_index(struct device_node *n,const char *name,int index,const char **out){(void)n;if(fault())return -EIO;if(bad_name&&!strcmp(name,bad_name)&&index==bad_index){*out=bad_value;return 0;}const struct dt_property *p=property(name);if(!p||index<0||index>=p->count)return -EINVAL;*out=p->values[index];return 0;}
static int of_property_count_u32_elems(struct device_node *n,const char *name){(void)n;if(strcmp(name,"ovpgate,cp_chip_role"))return -EINVAL;if(fault())return -EIO;return chip_count;}
static int of_property_read_u32(struct device_node *n,const char *name,u32 *out){(void)n;assert(!strcmp(name,"ovpgate,cp_chip_role"));if(fault())return -EIO;*out=chip_value;return 0;}
static int of_get_named_gpio(struct device_node *n,const char *name,int index){(void)n;assert(!strcmp(name,"legacy_gpio")&&index==0);if(fault())return -EPROBE_DEFER;return gpio_lookup_error?gpio_lookup_error:15;}
static int gpio_is_valid(int gpio){return gpio>=0;}
static int devm_gpio_request_one(struct device *d,int gpio,int flags,const char *name){(void)d;assert(gpio==15&&flags==GPIOF_OUT_INIT_LOW&&!strcmp(name,"legacy_gpio"));if(fault())return -EIO;owned_gpio++;return 0;}
static int gpio_direction_output(int gpio,int value){assert(gpio==15&&owned_gpio&&value>=0&&value<=1);return gpio_direction_error;}
#define INIT_DELAYED_WORK(w,f) do{(w)->work.fn=f;(w)->work.pending=false;}while(0)
static struct workqueue_struct *alloc_ordered_workqueue(const char *n,int f){(void)f;assert(!strcmp(n,"mca_path_control"));if(fault())return NULL;live_wq++;return calloc(1,sizeof(struct workqueue_struct));}
static int queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay){assert(q&&w->work.fn&&(delay==1000||delay==10000));w->work.pending=true;return 1;}
static void cancel_delayed_work_sync(struct delayed_work *w){assert(w->work.fn);drains++;w->work.pending=false;}
static void destroy_workqueue(struct workqueue_struct *q){assert(live_wq==1&&!live_group);live_wq--;free(q);}
static void mca_sysfs_init_attrs(struct attribute **a,struct mca_sysfs_attr_info *f,int n){(void)a;(void)f;assert(n==6);}
static int mca_sysfs_create_link_group(int p,const char *n,struct device *d,const struct attribute_group *g){(void)p;(void)g;assert(!strcmp(n,"path_control")&&d->data);if(fault())return -EIO;live_group++;return 0;}
static void mca_sysfs_remove_link_group(int p,const char *n,struct device *d,const struct attribute_group *g){(void)p;(void)n;(void)d;(void)g;assert(live_group==1);live_group--;}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *n,struct mca_sysfs_attr_info *t,int c){for(int i=0;i<c;i++)if(!strcmp(n,t[i].attr.attr.name))return t+i;return NULL;}
static int sysfs_emit(char *buf,const char *fmt,...){va_list ap;va_start(ap,fmt);int ret=vsnprintf(buf,PAGE_SIZE,fmt,ap);va_end(ap);return ret;}
static int kstrtoint(const char *s,int base,int *out){char *end;errno=0;long v=strtol(s,&end,base);if(errno||end==s||(*end&&strcmp(end,"\n"))||v<INT_MIN||v>INT_MAX)return -EINVAL;*out=v;return 0;}
static int platform_class_buckchg_ops_get_otg_boost_src(int role,int *v){(void)role;if(provider_error)return provider_error;*v=boost_source;return 0;}
static int platform_class_buckchg_ops_get_online(int role,int *v){(void)role;if(provider_error)return provider_error;*v=usb_value;return 0;}
static int platform_class_buckchg_ops_get_otg_boost_enable_status(int role,int *v){(void)role;if(provider_error)return provider_error;*v=otg_value;return 0;}
static int platform_class_cp_enable_ovpgate(int role,bool enable){assert(role>=0&&role<CP_ROLE_MAX);cp_calls++;if(stop_hook){stop_hook=false;pthread_barrier_wait(&reached_provider);pthread_barrier_wait(&release_provider);}if(cp_error)return cp_error;cp_enable=enable;return 0;}
/* FUNCTIONS */
static void release(void){assert(!live_group&&!live_wq&&!path_claimed&&!path_instance);if(allocation)pthread_mutex_destroy(&allocation->enable_handling_lock.value);for(int c=resource_count-1;c>=0;c--)free(resources[c]);resource_count=owned_gpio=0;allocation=NULL;}
static void reset(void){assert(!resource_count);sequence=fail_at=drains=provider_error=cp_error=cp_calls=cp_enable=usb_value=otg_value=chip_value=gpio_lookup_error=gpio_direction_error=0;chip_count=1;boost_source=2;bad_name=bad_value=bad_count_name=NULL;}
static void *setter(void *p){(void)p;assert(!mca_path_control_enable_gate(PATH_CONTROL_USB,true));return NULL;}
static void *remove_dev(void *p){mca_path_control_remove(p);return NULL;}
static void *initial_worker(void *p){struct mca_path_control *i=p;mca_path_control_update_status_work(&i->update_condition_work.work);return NULL;}
int main(void){
 alarm(25);struct device_node node={0};struct platform_device dev={.dev.of_node=&node};reset();assert(mca_path_control_enable_gate(PATH_CONTROL_USB,1)==-ENODEV);assert(!mca_path_control_probe(&dev));int checkpoints=sequence;struct mca_path_control *i=allocation;assert(i->condition_count==10&&!i->ready&&i->control_scheme[0].managed);for(int role=1;role<MAX_ROLE;role++)assert(!i->control_scheme[role].managed&&!i->gate_known[role]);char buf[PAGE_SIZE];struct device_attribute usb={{"usb_in"}},otg={{"otg_in"}},all={{"control_value"}};assert(path_control_sysfs_show(&dev.dev,&usb,buf)==-EAGAIN);
 provider_error=-EAGAIN;assert(mca_path_control_enable_gate(PATH_CONTROL_USB,1)==-EAGAIN&&!i->ready&&!cp_calls);provider_error=0;usb_value=2;assert(mca_path_control_enable_gate(PATH_CONTROL_USB,1)==-ERANGE&&!i->ready);usb_value=0;boost_source=4;assert(mca_path_control_enable_gate(PATH_CONTROL_USB,1)==-ERANGE);boost_source=2;cp_error=-EIO;assert(mca_path_control_enable_gate(PATH_CONTROL_USB,1)==-EIO&&!i->ready&&!i->gate_known[0]);cp_error=0;assert(!mca_path_control_enable_gate(PATH_CONTROL_USB,1)&&i->ready&&i->condition_value==1&&cp_enable==1);assert(path_control_sysfs_show(&dev.dev,&usb,buf)>0&&!strcmp(buf,"1\n"));int controls=7;
 assert(mca_path_control_enable_gate(PATH_CONTROL_OTG,1)==-ENOENT&&i->condition_value==1&&cp_enable==1);assert(!mca_path_control_enable_gate(PATH_CONTROL_USB,0)&&i->condition_value==0&&cp_enable==0);assert(!mca_path_control_enable_gate(PATH_CONTROL_OTG,1)&&i->condition_value==8&&cp_enable==0);assert(path_control_sysfs_show(&dev.dev,&otg,buf)>0&&!strcmp(buf,"1\n"));assert(mca_path_control_enable_gate(PATH_CONTROL_WLS,1)==-EOPNOTSUPP&&i->condition_value==8);assert(mca_path_control_enable_gate((CONTROL_SRC)-1,1)==-EINVAL);assert(mca_path_control_enable_gate((CONTROL_SRC)5,1)==-EINVAL);assert(path_control_sysfs_store(&dev.dev,&all,"1",1)==1&&i->condition_value==1);assert(path_control_sysfs_store(&dev.dev,&all,"32",2)==-ERANGE);assert(path_control_sysfs_store(&dev.dev,&all,"2",1)==-EOPNOTSUPP);controls+=10;
 const char *bad[]={"", "-1", "2", "abc", "1 junk", "2147483648"};for(unsigned c=0;c<ARRAY_SIZE(bad);c++){int before=i->condition_value;assert(path_control_sysfs_store(&dev.dev,&usb,bad[c],strlen(bad[c]))<0&&i->condition_value==before);controls++;}assert(path_control_sysfs_store(&dev.dev,&usb,"0\0xx",4)==-EINVAL);assert(path_control_sysfs_store(&dev.dev,&usb,"0\n",2)==2);cp_error=-EIO;int before=i->condition_value;assert(path_control_sysfs_store(&dev.dev,&usb,"1",1)==-EIO&&i->condition_value==before&&!i->gate_sts[0]);cp_error=0;assert(path_control_sysfs_store(&dev.dev,&usb,"1",1)==1&&i->gate_sts[0]);int calls=cp_calls;i->update_condition_work.work.pending=false;mca_path_control_update_status_work(&i->update_condition_work.work);assert(i->condition_value==1&&cp_calls==calls);controls+=5;
 mca_path_control_shutdown(&dev);assert(drains==1);mca_path_control_remove(&dev);assert(drains==1&&!dev.dev.data);release();for(int f=1;f<=checkpoints;f++){reset();fail_at=f;assert(mca_path_control_probe(&dev)<0&&!dev.dev.data);release();}
 struct invalid {const char *name;int index;const char *value;};const struct invalid malformed[]={
 {"control_scheme",0,"-1"},{"control_scheme",0,"5"},{"control_scheme",1,"3"},{"control_scheme",3,"0"},{"control_scheme",2,"bad"},
 {"default_path",0,"-1"},{"default_path",0,"2"},{"default_path",0,"abc"},{"default_path",2,"0"},{"default_path",1,"4"},{"default_path",1,"-2"},
 {"default_ctrl",0,"-1"},{"default_ctrl",0,"5"},{"default_ctrl",1,"2"},{"default_ctrl",1,"-1"},{"default_ctrl",2,"0"},
 {"path_condition",0,"-1"},{"path_condition",0,"32"},{"path_condition",0,"abc"},{"path_condition",2,"0"}};
 int schemas=0;for(unsigned c=0;c<ARRAY_SIZE(malformed);c++){reset();bad_name=malformed[c].name;bad_index=malformed[c].index;bad_value=malformed[c].value;assert(mca_path_control_probe(&dev)<0&&!dev.dev.data);release();schemas++;}
 const char *bad_groups[]={"control_scheme","default_path","default_ctrl","path_condition"};for(unsigned c=0;c<ARRAY_SIZE(bad_groups);c++){reset();bad_count_name=bad_groups[c];bad_count=1;assert(mca_path_control_probe(&dev)==-EINVAL);release();schemas++;}
 reset();chip_count=2;assert(mca_path_control_probe(&dev)==-EINVAL);release();schemas++;reset();chip_value=3;assert(mca_path_control_probe(&dev)==-ERANGE);release();schemas++;reset();bad_name="control_scheme";bad_index=8;bad_value="legacy_gpio";gpio_lookup_error=-EPROBE_DEFER;assert(mca_path_control_probe(&dev)==-EPROBE_DEFER);release();schemas++;
 reset();bad_name="control_scheme";bad_index=8;bad_value="legacy_gpio";assert(!mca_path_control_probe(&dev)&&owned_gpio==1);i=allocation;assert(!mca_path_control_enable_gate(PATH_CONTROL_OTG,1)&&i->gate_known[2]&&i->gate_sts[2]);assert(!mca_path_control_enable_gate(PATH_CONTROL_OTG,0));gpio_direction_error=-EIO;assert(mca_path_control_enable_gate(PATH_CONTROL_OTG,1)==-EIO&&i->condition_value==0&&!i->gate_sts[2]);mca_path_control_remove(&dev);release();controls+=3;
 reset();assert(!mca_path_control_probe(&dev));i=allocation;provider_error=-EAGAIN;i->update_condition_work.work.pending=false;mca_path_control_update_status_work(&i->update_condition_work.work);assert(i->update_condition_work.work.pending&&!i->ready);provider_error=0;i->update_condition_work.work.pending=false;mca_path_control_update_status_work(&i->update_condition_work.work);assert(i->ready&&!i->update_condition_work.work.pending);stop_hook=true;pthread_barrier_init(&reached_provider,NULL,2);pthread_barrier_init(&release_provider,NULL,2);pthread_t worker,removing;assert(!pthread_create(&worker,NULL,setter,NULL));pthread_barrier_wait(&reached_provider);assert(!pthread_create(&removing,NULL,remove_dev,&dev));pthread_barrier_wait(&release_provider);assert(!pthread_join(worker,NULL));assert(!pthread_join(removing,NULL));assert(mca_path_control_enable_gate(PATH_CONTROL_USB,1)==-ENODEV);release();
 reset();assert(!mca_path_control_probe(&dev));i=allocation;stop_hook=true;assert(!pthread_create(&worker,NULL,initial_worker,i));pthread_barrier_wait(&reached_provider);assert(!pthread_create(&removing,NULL,remove_dev,&dev));pthread_barrier_wait(&release_provider);assert(!pthread_join(worker,NULL));assert(!pthread_join(removing,NULL));assert(drains==1);release();
 printf("{\"probe_failure_points\":%d,\"invalid_dt_schema_cases\":%d,\"provider_control_and_bitmask_cases\":%d,\"stock_condition_count\":10,\"ordinal_API_and_DT_bitmasks\":true,\"actual_CP_error_preserved\":true,\"unsupported_wireless_rejected\":true,\"null_roles_unmanaged\":true,\"checked_initial_snapshot_and_retry\":true,\"active_exported_setter_removal\":true,\"active_worker_removal\":true,\"work_and_sysfs_drained\":true,\"shutdown_remove_idempotent\":true}\n",checkpoints,schemas,controls);
}
