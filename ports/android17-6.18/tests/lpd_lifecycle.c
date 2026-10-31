#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __rcu
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define MAIN_BUCK_CHARGER 0
#define ARRAY_SIZE(v) (sizeof(v)/sizeof(v[0]))
#define READ_ONCE(v) __atomic_load_n(&(v),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v),(x),__ATOMIC_RELEASE)
#define rcu_access_pointer(p) READ_ONCE(p)
#define rcu_assign_pointer(p,v) WRITE_ONCE(p,v)
#define srcu_dereference(p,s) READ_ONCE(p)
struct mutex {pthread_mutex_t value;};
struct srcu_struct {pthread_rwlock_t readers;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define DEFINE_STATIC_SRCU(n) struct srcu_struct n={PTHREAD_RWLOCK_INITIALIZER}
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
#define mca_sysfs_attr_ro(prefix,perm,id,label) {{ {#label} },id}
#define mca_sysfs_attr_rw(prefix,perm,id,label) mca_sysfs_attr_ro(prefix,perm,id,label)
/* HEADER */
/* GLOBALS */
/* TABLE */
static int group,sequence,fail_at,provider_error,last_set,last_channel;
static struct mca_lpd_dev *allocation;
static bool drain_hook;
static pthread_barrier_t reached_drain;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;allocation=calloc(1,n);assert(allocation);return allocation;}
static void platform_set_drvdata(struct platform_device *p,void *d){p->dev.data=d;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static int lpd_sysfs_create_group(struct device *d){assert(d->data);if(fault())return -EIO;assert(!group);group=1;return 0;}
static void lpd_sysfs_remove_group(struct device *d){assert(d->data&&group);group=0;}
static void synchronize_srcu(struct srcu_struct *s){if(drain_hook)pthread_barrier_wait(&reached_drain);assert(!pthread_rwlock_wrlock(&s->readers));assert(!pthread_rwlock_unlock(&s->readers));}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,struct mca_sysfs_attr_info *table,size_t count){for(size_t i=0;i<count;i++)if(!strcmp(name,table[i].attr.attr.name))return table+i;return NULL;}
static int sysfs_emit(char *out,const char *format,int value){return snprintf(out,PAGE_SIZE,format,value);}
static int kstrtoint(const char *s,int base,int *v){char *end;errno=0;long n=strtol(s,&end,base);if(errno||s==end||(*end&&strcmp(end,"\n"))||n<INT32_MIN||n>INT32_MAX)return -EINVAL;*v=n;return 0;}
#define GETTER(label,id) static int platform_class_buckchg_ops_get_lpd_##label(int role,int *v){(void)role;if(provider_error)return provider_error;last_channel=id;*v=id;return 0;}
GETTER(enable,0) GETTER(status,1) GETTER(sbu1,2) GETTER(sbu2,3) GETTER(cc1,4) GETTER(cc2,5) GETTER(dp,6) GETTER(dm,7) GETTER(control,8) GETTER(uart_control,9)
#define SETTER(label) static int platform_class_buckchg_ops_set_lpd_##label(int role,int v){(void)role;if(provider_error)return provider_error;last_set=v;return 0;}
SETTER(sbu1) SETTER(control) SETTER(uart_control)
/* FUNCTIONS */
static void *remove_lpd(void *arg){mca_lpd_detect_remove(arg);return NULL;}
static struct device_attribute property(const char *name){return(struct device_attribute){{name}};}
int main(void){
 struct platform_device dev={0},duplicate={0};char buf[PAGE_SIZE];
 assert(lpd_is_charging_limit()==-ENODEV);
 for(int fail=1;fail<=2;fail++){sequence=0;fail_at=fail;assert(mca_lpd_detect_probe(&dev)<0);assert(!group&&!g_lpd&&!dev.dev.data);free(allocation);allocation=NULL;}
 sequence=0;fail_at=0;assert(!mca_lpd_detect_probe(&dev));assert(lpd_is_charging_limit()==0);
 assert(mca_lpd_detect_probe(&duplicate)==-EEXIST);assert(!duplicate.dev.data);
 int cases=0;
 for(unsigned n=0;n<ARRAY_SIZE(lpd_sysfs_field_tbl);n++){
  struct device_attribute attr=lpd_sysfs_field_tbl[n].attr;
  assert(lpd_sysfs_show(&dev.dev,&attr,buf)>0);cases++;
  if(lpd_sysfs_field_tbl[n].sysfs_attr_name!=LPD_PROP_CHARGING){provider_error=-EAGAIN;assert(lpd_sysfs_show(&dev.dev,&attr,buf)==-EAGAIN);provider_error=0;cases++;}
 }
 struct device_attribute dm=property("dm");assert(lpd_sysfs_show(&dev.dev,&dm,buf)>0&&last_channel==7);
 struct device_attribute dp=property("dp");assert(lpd_sysfs_show(&dev.dev,&dp,buf)>0&&last_channel==6);
 struct device_attribute charging=property("lpd_charging");
 assert(lpd_sysfs_store(&dev.dev,&charging,"1",1)==1&&lpd_is_charging_limit()==1);
 assert(lpd_sysfs_store(&dev.dev,&charging,"0",1)==1&&lpd_is_charging_limit()==0);
 const char *invalid[]={"-1","2","2147483648","","garbage"};
 for(unsigned n=0;n<ARRAY_SIZE(invalid);n++)assert(lpd_sysfs_store(&dev.dev,&charging,invalid[n],strlen(invalid[n]))==-EINVAL);
 const char *setters[]={"sbu1","lpd_control","uart_control"};
 for(unsigned n=0;n<ARRAY_SIZE(setters);n++){struct device_attribute attr=property(setters[n]);provider_error=-EIO;assert(lpd_sysfs_store(&dev.dev,&attr,"1",1)==-EIO);provider_error=0;assert(lpd_sysfs_store(&dev.dev,&attr,"1",1)==1&&last_set==1);}
 assert(!pthread_rwlock_rdlock(&lpd_srcu.readers));drain_hook=true;pthread_barrier_init(&reached_drain,NULL,2);
 pthread_t removal;assert(!pthread_create(&removal,NULL,remove_lpd,&dev));pthread_barrier_wait(&reached_drain);
 assert(!g_lpd&&!group&&allocation->stopping);assert(lpd_is_charging_limit()==-ENODEV);
 assert(lpd_sysfs_store(&dev.dev,&charging,"1",1)==-ENODEV);
 assert(!pthread_rwlock_unlock(&lpd_srcu.readers));assert(!pthread_join(removal,NULL));drain_hook=false;pthread_barrier_destroy(&reached_drain);
 assert(!dev.dev.data);mca_lpd_detect_shutdown(&dev);free(allocation);
 printf("{\"probe_failure_points\":2,\"duplicate_owner_rejected\":true,\"property_success_error_cases\":%d,\"invalid_charging_controls\":5,\"setter_errors\":3,\"dm_channel_mapping\":true,\"active_reader_removal_drains\":true,\"removed_controls_rejected\":true}\n",cases);
}
