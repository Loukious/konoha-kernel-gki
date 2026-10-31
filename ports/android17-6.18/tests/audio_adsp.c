#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#define __init
#define __exit
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define ENOPARAM 519
#define THIS_MODULE NULL
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELEASE)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((intptr_t)(p))
typedef uint32_t u32,phandle;
struct mutex {pthread_mutex_t lock;};
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
static int live,fail_allocation=-1;
static void *kzalloc(size_t size,int flags){(void)flags;if(fail_allocation==0)return NULL;if(fail_allocation>0)fail_allocation--;void *p=calloc(1,size);assert(p);live++;return p;}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
struct device_node {int unused;};
struct property {const void *value;};
struct device {struct device_node *of_node;void *data;void *managed[16];unsigned allocations;};
struct platform_device {struct device dev;};
static void platform_set_drvdata(struct platform_device *p,void *d){p->dev.data=d;}
static void *platform_get_drvdata(struct platform_device *p){return p->dev.data;}
static void *devm_kzalloc(struct device *d,size_t size,int flags){void *p=kzalloc(size,flags);if(p){assert(d->allocations<16);d->managed[d->allocations++]=p;}return p;}
static char *devm_kstrdup(struct device *d,const char *s,int flags){char *p=devm_kzalloc(d,strlen(s)+1,flags);if(p)strcpy(p,s);return p;}
static void devm_kfree(struct device *d,void *p){for(unsigned i=0;i<d->allocations;i++)if(d->managed[i]==p){d->managed[i]=NULL;kfree(p);return;}assert(0);}
static void managed_cleanup(struct device *d){for(unsigned i=0;i<d->allocations;i++)kfree(d->managed[i]);d->allocations=0;}
static size_t strscpy(char *d,const char *s,size_t n){assert(strlen(s)<n);strcpy(d,s);return strlen(s);}
struct work_struct {void (*fn)(struct work_struct *);bool initialized,queued,running;};
static pthread_mutex_t work_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_changed=PTHREAD_COND_INITIALIZER;
#define INIT_WORK(w,f) ((w)->fn=(f),(w)->initialized=true)
static bool schedule_work(struct work_struct *w){assert(w->initialized);pthread_mutex_lock(&work_gate);bool was=w->queued;w->queued=true;pthread_mutex_unlock(&work_gate);return !was;}
static void model_run_work(struct work_struct *w){pthread_mutex_lock(&work_gate);if(!w->queued){pthread_mutex_unlock(&work_gate);return;}w->queued=false;w->running=true;pthread_mutex_unlock(&work_gate);w->fn(w);pthread_mutex_lock(&work_gate);w->running=false;pthread_cond_broadcast(&work_changed);pthread_mutex_unlock(&work_gate);}
static bool cancel_work_sync(struct work_struct *w){pthread_mutex_lock(&work_gate);bool was=w->queued;w->queued=false;while(w->running)pthread_cond_wait(&work_changed,&work_gate);pthread_mutex_unlock(&work_gate);return was;}
static bool flush_work(struct work_struct *w){model_run_work(w);return true;}
struct kobject;
struct kobj_attribute;
struct attribute {const char *name;unsigned mode;};
struct kobj_attribute {struct attribute attr;void *show;ssize_t (*store)(struct kobject *,struct kobj_attribute *,const char *,size_t);};
#define __ATTR(n,m,r,w) {.attr={.name=#n,.mode=(m)},.show=(r),.store=(w)}
struct attribute_group {struct attribute **attrs;};
struct sysfs_ops {int dummy;};
static const struct sysfs_ops kobj_sysfs_ops={0};
struct kobj_type {void (*release)(struct kobject *);const struct sysfs_ops *sysfs_ops;};
struct kobject {const struct kobj_type *type;const struct attribute_group *group;unsigned refs;bool added;};
static struct kobject kernel_root={.refs=1};
static struct kobject *kernel_kobj=&kernel_root,*published;
static bool fail_kobject,fail_group,publish_callback;
static ssize_t call_store(struct kobject *k,const char *name,const char *value){assert(k&&k->group);for(struct attribute **a=k->group->attrs;*a;a++)if(!strcmp((*a)->name,name)){struct kobj_attribute *attr=container_of(*a,struct kobj_attribute,attr);return attr->store(k,attr,value,strlen(value));}assert(0);}
static int kobject_init_and_add(struct kobject *k,const struct kobj_type *t,struct kobject *parent,const char *name){assert(parent==kernel_kobj&&!strcmp(name,"boot_adsp"));k->refs=1;k->type=t;if(fail_kobject)return -ENOMEM;if(published)return -EEXIST;k->added=true;return 0;}
static void plain_kobj_release(struct kobject *k){kfree(k);}
static const struct kobj_type plain_ktype={.release=plain_kobj_release};
static struct kobject *kobject_create_and_add(const char *name,struct kobject *parent){struct kobject *k=kzalloc(sizeof(*k),0);if(!k)return NULL;if(kobject_init_and_add(k,&plain_ktype,parent,name)){k->type->release(k);return NULL;}return k;}
static int sysfs_create_group(struct kobject *k,const struct attribute_group *g){if(fail_group)return -ENOMEM;k->group=g;published=k;if(publish_callback)assert(call_store(k,"boot","1\n")==2);return 0;}
static void sysfs_remove_group(struct kobject *k,const struct attribute_group *g){assert(k->group==g);k->group=NULL;if(published==k)published=NULL;}
static void kobject_del(struct kobject *k){k->added=false;if(published==k)published=NULL;}
static void kobject_put(struct kobject *k){assert(k->refs);if(!--k->refs){if(published==k)published=NULL;k->type->release(k);}}
static int kstrtoint(const char *s,unsigned base,int *value){char *end;errno=0;long n=strtol(s,&end,base);if(end==s||errno==ERANGE||n>INT_MAX||n<INT_MIN)return -EINVAL;if(*end=='\n')end++;if(*end)return -EINVAL;*value=n;return 0;}
static u32 main_handle=1,image_handle,state_value,fuse_value=1,unsupported;
static bool state_missing,main_missing,malformed_main,malformed_image;
static const char *fallback_name="adsp_alt.mdt",*firmware_names[4]={"one.mdt","two.mdt","three.mdt","four.mdt"};
static u32 firmware_bits[4]={1,2,3,4};
static int firmware_count=4,returned_names=4;
static struct property prop_main,prop_image;
static struct property *of_find_property(struct device_node *node,const char *name,int *size){assert(node);bool main=!strcmp(name,"qcom,rproc-handle");if((main&&main_missing)||(!main&&!image_handle))return NULL;struct property *p=main?&prop_main:&prop_image;p->value=main?(void *)&main_handle:(void *)&image_handle;if(size)*size=4;return p;}
static u32 be32_to_cpup(const void *p){return *(const u32 *)p;}
static int of_property_read_u32(struct device_node *node,const char *name,u32 *value){assert(node);if(!strcmp(name,"qcom,rproc-handle")){if(main_missing||malformed_main)return -EINVAL;*value=main_handle;return 0;}if(!strcmp(name,"qcom,proc-img-to-load")){if(!image_handle||malformed_image)return -EINVAL;*value=image_handle;return 0;}if(!strcmp(name,"qcom,adsp-state")){if(state_missing)return -EINVAL;*value=state_value;return 0;}if(!strcmp(name,"adsp-fuse-not-supported")){*value=unsupported;return 0;}return -EINVAL;}
static int of_property_read_string(struct device_node *n,const char *name,const char **value){assert(n);if(strcmp(name,"adsp-fw-name")||!fallback_name)return -EINVAL;*value=fallback_name;return 0;}
static int of_property_count_strings(struct device_node *n,const char *name){assert(n&&!strcmp(name,"adsp-fw-names"));return firmware_count;}
static int of_property_read_u32_array(struct device_node *n,const char *name,u32 *value,int count){assert(n&&!strcmp(name,"adsp-fw-bit-values"));assert(count>=0&&count<=4);memcpy(value,firmware_bits,count*sizeof(u32));return 0;}
static int of_property_read_string_array(struct device_node *n,const char *name,const char **value,int count){assert(n&&!strcmp(name,"adsp-fw-names"));int copied=returned_names<count?returned_names:count;if(copied<0)return copied;memcpy(value,firmware_names,copied*sizeof(char *));return copied;}
struct nvmem_cell {int dummy;};
static struct nvmem_cell cell;
static int cell_error,read_error;
static unsigned cell_gets,cell_puts;
static size_t fuse_length=4;
static struct nvmem_cell *nvmem_cell_get(struct device *d,const char *name){assert(d&&!strcmp(name,"adsp_variant"));if(cell_error)return ERR_PTR(cell_error);cell_gets++;return &cell;}
static void nvmem_cell_put(struct nvmem_cell *c){assert(c==&cell);cell_puts++;}
static void *nvmem_cell_read(struct nvmem_cell *c,size_t *len){assert(c==&cell);if(read_error)return ERR_PTR(read_error);*len=fuse_length;void *p=kzalloc(fuse_length?fuse_length:1,0);if(!p)return ERR_PTR(-ENOMEM);memcpy(p,&fuse_value,fuse_length<4?fuse_length:4);return p;}
enum rproc_state {RPROC_OFFLINE,RPROC_RUNNING,RPROC_ATTACHED,RPROC_CRASHED};
enum rproc_crash_type {RPROC_FATAL_ERROR};
struct rproc {const char *name;struct mutex lock;struct work_struct crash_handler;enum rproc_state state;bool recovery_disabled;unsigned refs,power,boots,shutdowns,recoveries;int fail_boot,fail_shutdown,fail_firmware;char firmware[64];};
static struct rproc adsp={.name="adsp"},modem={.name="modem"};
static bool provider_missing,ready,disable_after_recovery;
static struct rproc *rproc_get_by_phandle(phandle handle){if(provider_missing||handle>2||!handle)return NULL;struct rproc *r=handle==1?&adsp:&modem;r->refs++;return r;}
static void rproc_put(struct rproc *r){assert(r&&r->refs>1);r->refs--;}
static int rproc_boot(struct rproc *r){assert(r&&!IS_ERR(r));if(r->fail_boot)return r->fail_boot;r->power++;r->boots++;r->state=RPROC_RUNNING;return 0;}
static int rproc_shutdown(struct rproc *r){assert(r&&!IS_ERR(r));if(!r->power||r->state!=RPROC_RUNNING&&r->state!=RPROC_ATTACHED)return -EINVAL;if(r->fail_shutdown)return r->fail_shutdown;r->power--;r->shutdowns++;if(!r->power)r->state=RPROC_OFFLINE;return 0;}
static int rproc_set_firmware(struct rproc *r,const char *name){assert(r);if(r->fail_firmware)return r->fail_firmware;assert(strlen(name)<sizeof(r->firmware));strcpy(r->firmware,name);return 0;}
static void model_recovery(struct work_struct *w){struct rproc *r=container_of(w,struct rproc,crash_handler);if(r->state==RPROC_OFFLINE)return;r->state=RPROC_CRASHED;if(!r->recovery_disabled){r->recoveries++;r->state=RPROC_RUNNING;if(disable_after_recovery)r->recovery_disabled=true;}}
static void rproc_report_crash(struct rproc *r,enum rproc_crash_type t){assert(t==RPROC_FATAL_ERROR);schedule_work(&r->crash_handler);}
static bool block_ready,ready_entered,release_ready;
static pthread_mutex_t ready_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_changed=PTHREAD_COND_INITIALIZER;
static bool spf_core_is_apm_ready(int timeout){assert(timeout==10000);if(block_ready){pthread_mutex_lock(&ready_gate);ready_entered=true;pthread_cond_broadcast(&ready_changed);while(!release_ready)pthread_cond_wait(&ready_changed,&ready_gate);pthread_mutex_unlock(&ready_gate);}return ready;}
struct of_device_id {const char *compatible;};
struct platform_driver {struct {const char *name;void *owner;const struct of_device_id *of_match_table;bool suppress_bind_attrs;} driver;int (*probe)(struct platform_device *);void (*remove)(struct platform_device *);};
static int platform_driver_register(struct platform_driver *d){(void)d;return 0;}
static void platform_driver_unregister(struct platform_driver *d){(void)d;}
/* SOURCE */
static struct device_node node;
static struct platform_device platform={.dev.of_node=&node};
static struct work_struct *loader_work(void){
#ifdef NATIVE
 return &adsp_ldr_work;
#else
 struct adsp_loader_private *p=platform_get_drvdata(&platform);return &p->work;
#endif
}
static void init_model(unsigned borrowed){mutex_init(&adsp.lock);mutex_init(&modem.lock);adsp.refs=modem.refs=1;adsp.power=borrowed;adsp.state=borrowed?RPROC_RUNNING:RPROC_OFFLINE;modem.state=RPROC_OFFLINE;adsp.recovery_disabled=true;INIT_WORK(&adsp.crash_handler,model_recovery);INIT_WORK(&modem.crash_handler,model_recovery);cell_error=-ENOENT;}
static int bind(void){int ret=adsp_loader_probe(&platform);if(ret)managed_cleanup(&platform.dev);return ret;}
static void unbind(void){adsp_loader_remove(&platform);managed_cleanup(&platform.dev);assert(!published&&!platform.dev.data&&adsp.refs==1&&modem.refs==1&&!live);}
static ssize_t command(const char *name,const char *value){return call_store(published,name,value);}
static void run_loader(void){model_run_work(loader_work());}
static unsigned votes(void){init_model(2);ready=true;assert(!bind());assert(command("boot","1\n")==2);run_loader();assert(adsp.power==2&&!adsp.boots);assert(command("boot","0\n")==2&&adsp.power==2);unbind();return 3;}
static unsigned idempotent(void){init_model(0);assert(!bind());assert(command("boot","1")==1);run_loader();assert(adsp.power==1&&adsp.boots==1);assert(command("boot","1")==1);run_loader();assert(adsp.power==1&&adsp.boots==1);assert(command("boot","0")==1&&adsp.power==0);assert(command("boot","0")==1&&adsp.shutdowns==1);unbind();return 4;}
static unsigned restart(void){init_model(2);ready=true;assert(!bind());assert(command("ssr","1\n")==2);run_loader();assert(adsp.power==2&&adsp.recoveries==1&&adsp.recovery_disabled);adsp.recovery_disabled=false;assert(command("ssr","1")==1);run_loader();assert(adsp.power==2&&adsp.recoveries==2&&!adsp.recovery_disabled);disable_after_recovery=true;assert(command("ssr","1")==1);run_loader();assert(adsp.power==2&&adsp.recoveries==3&&adsp.recovery_disabled);unbind();return 3;}
static unsigned firmware(void){
 init_model(0);cell_error=0;returned_names=1;fuse_value=2;assert(!bind());assert(command("boot","1")==1);run_loader();assert(!adsp.firmware[0]);unbind();
 returned_names=4;assert(!bind());assert(command("boot","1")==1);run_loader();assert(!strcmp(adsp.firmware,"two.mdt"));unbind();
 cell_error=-ENOENT;unsupported=1;assert(!bind());assert(command("boot","1")==1);run_loader();assert(!strcmp(adsp.firmware,"adsp_alt.mdt"));unbind();assert(cell_gets==cell_puts);return 3;
}
static unsigned provider(void){init_model(0);main_missing=true;assert(bind()<0&&adsp.refs==1&&!live);main_missing=false;malformed_main=true;assert(bind()<0&&adsp.refs==1&&!live);malformed_main=false;provider_missing=true;assert(bind()==-EPROBE_DEFER&&!live);provider_missing=false;cell_error=-EPROBE_DEFER;assert(bind()==-EPROBE_DEFER&&adsp.refs==1&&!live);return 4;}
static unsigned cleanup(void){init_model(0);fail_group=true;assert(bind()==-ENOMEM&&adsp.refs==1&&!published&&!live);fail_group=false;fail_kobject=true;assert(bind()==-ENOMEM&&adsp.refs==1&&!live);fail_kobject=false;for(int fail=0;fail<2;fail++){fail_allocation=fail;assert(bind()==-ENOMEM&&adsp.refs==1&&!live);}return 4;}
static unsigned parsing(void){init_model(0);assert(!bind());for(const char **s=(const char *[]){"2","-1","1junk","2147483648","","1\nextra",NULL};*s;s++)assert(command("boot",*s)==-EINVAL&&!loader_work()->queued);for(const char **s=(const char *[]){"0","2","1junk","",NULL};*s;s++)assert(command("ssr",*s)==-EINVAL&&!loader_work()->queued);unbind();return 10;}
static unsigned optional(void){init_model(0);image_handle=2;assert(!bind());assert(command("boot","1")==1);run_loader();assert(adsp.power==1&&modem.power==1&&adsp.refs==2&&modem.refs==2);unbind();assert(!adsp.power&&!modem.power);return 2;}
static unsigned faults(void){
 init_model(0);image_handle=2;adsp.fail_boot=-EIO;assert(!bind());assert(command("boot","1")==1);run_loader();assert(!adsp.power&&!modem.power&&!adsp.shutdowns&&modem.shutdowns==1);unbind();
 adsp.fail_boot=0;unsupported=1;adsp.fail_firmware=-EINVAL;assert(!bind());assert(command("boot","1")==1);run_loader();assert(!adsp.power&&!modem.power);unbind();
 adsp.fail_firmware=0;assert(!bind());assert(command("boot","1")==1);run_loader();assert(adsp.power==1&&modem.power==1);adsp.fail_shutdown=-EIO;assert(command("boot","0")==-EIO&&adsp.power==1&&!modem.power);adsp.fail_shutdown=0;assert(command("boot","0")==1&&!adsp.power);unbind();
 image_handle=1;assert(!bind()&&adsp.refs==2);assert(command("boot","1")==1);run_loader();adsp.power++;assert(command("boot","0")==1&&adsp.power==1);unbind();assert(adsp.power==1);return 6;
}
static unsigned restart_states(void){
 init_model(0);state_missing=true;assert(!bind());assert(command("ssr","1")==1);run_loader();assert(adsp.power==1&&adsp.boots==1);unbind();
 state_missing=false;adsp.power=2;adsp.state=RPROC_CRASHED;assert(!bind());assert(command("ssr","1")==1);run_loader();assert(adsp.power==2&&adsp.recoveries==0&&adsp.recovery_disabled);unbind();
 adsp.state=RPROC_RUNNING;assert(!bind());assert(command("ssr","1")==1);assert(command("boot","0")==1);run_loader();assert(adsp.power==2&&adsp.recoveries==0);unbind();return 3;
}
static unsigned providers_rebind(void){
 init_model(0);assert(!bind());struct platform_device other={.dev.of_node=&node};assert(adsp_loader_probe(&other)==-EEXIST);managed_cleanup(&other.dev);assert(adsp.refs==2);
 assert(command("boot","1")==1);run_loader();assert(adsp.power==1);unbind();assert(!bind());assert(command("boot","1")==1);run_loader();assert(adsp.power==1);unbind();return 2;
}
static unsigned publication(void){init_model(0);publish_callback=true;assert(!bind());run_loader();assert(adsp.power==1);unbind();return 1;}
static void *worker_thread(void *p){model_run_work(p);return NULL;}
static bool removal_done;
static void *removal_thread(void *p){adsp_loader_remove(p);WRITE_ONCE(removal_done,true);return NULL;}
static unsigned removal(void){init_model(0);assert(!bind());block_ready=true;assert(command("boot","1")==1);pthread_t worker,remover;assert(!pthread_create(&worker,NULL,worker_thread,loader_work()));pthread_mutex_lock(&ready_gate);while(!ready_entered)pthread_cond_wait(&ready_changed,&ready_gate);pthread_mutex_unlock(&ready_gate);assert(!pthread_create(&remover,NULL,removal_thread,&platform));struct timespec pause={.tv_nsec=20000000};nanosleep(&pause,NULL);assert(!READ_ONCE(removal_done));pthread_mutex_lock(&ready_gate);release_ready=true;pthread_cond_broadcast(&ready_changed);pthread_mutex_unlock(&ready_gate);assert(!pthread_join(worker,NULL)&&!pthread_join(remover,NULL));managed_cleanup(&platform.dev);assert(!adsp.power&&!adsp.boots&&!published&&adsp.refs==1&&!live&&!platform.dev.data);return 1;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;if(!strcmp(argv[1],"votes"))n=votes();else if(!strcmp(argv[1],"idempotent"))n=idempotent();else if(!strcmp(argv[1],"restart"))n=restart();else if(!strcmp(argv[1],"firmware"))n=firmware();else if(!strcmp(argv[1],"provider"))n=provider();else if(!strcmp(argv[1],"cleanup"))n=cleanup();else if(!strcmp(argv[1],"parsing"))n=parsing();else if(!strcmp(argv[1],"optional"))n=optional();else if(!strcmp(argv[1],"faults"))n=faults();else if(!strcmp(argv[1],"restart_states"))n=restart_states();else if(!strcmp(argv[1],"providers_rebind"))n=providers_rebind();else if(!strcmp(argv[1],"publication"))n=publication();else if(!strcmp(argv[1],"removal"))n=removal();else abort();printf("{\"adsp_%s_cases\":%u}\n",argv[1],n);}
