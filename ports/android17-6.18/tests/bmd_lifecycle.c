#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint32_t u32;
struct mutex {pthread_mutex_t value;};typedef pthread_mutex_t spinlock_t;
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->value,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->value));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->value));}
static struct mutex *take_guard(struct mutex *m){mutex_lock(m);return m;}
static void drop_guard(struct mutex **m){mutex_unlock(*m);}
#define guard(kind) struct mutex *__attribute__((cleanup(drop_guard))) held_guard=take_guard
#define spin_lock_init(m) assert(!pthread_mutex_init(m,NULL))
#define spin_lock_irqsave(m,f) do{f=0;assert(!pthread_mutex_lock(m));}while(0)
#define spin_unlock_irqrestore(m,f) do{(void)f;assert(!pthread_mutex_unlock(m));}while(0)
#define READ_ONCE(v) __atomic_load_n(&(v),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v),(x),__ATOMIC_RELEASE)
#define ARRAY_SIZE(v) (sizeof(v)/sizeof(v[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(v) ((int)(intptr_t)(v))
#define IS_ERR(v) ((uintptr_t)(v)>=(uintptr_t)-4095)
#define EPROBE_DEFER 517
#define GPIOF_IN 1
#define WQ_MEM_RECLAIM 1
#define GFP_KERNEL 1
#define PAGE_SIZE 4096
#define SYSFS_DEV_5 5
#define msecs_to_jiffies(v) (v)
#define mca_log_err(...) ((void)0)
#define mca_log_info(...) ((void)0)
/* ENUMS */
struct device_node {int n;};
struct device {void *data;struct device_node *of_node;};
struct platform_device {struct device dev;};
struct work_struct {void(*fn)(struct work_struct *);bool pending;int delay;};
struct delayed_work {struct work_struct work;};
struct workqueue_struct {int id;};
struct iio_channel {int id;};
struct attribute {const char *name;};struct device_attribute {struct attribute attr;};
struct mca_sysfs_attr_info {struct device_attribute attr;int sysfs_attr_name;};
struct attribute_group {struct attribute **attrs;};
#define mca_sysfs_attr_ro(prefix,mode,id,name) {{{#name}},id}
/* HEADER */
/* DECLARATIONS */
/* TABLE */
static int sequence,fail_at,live_wq,live_group,live_strategy,owned_gpio,owned_iio,drains,dt_schemes[2]={3,1},dt_fg;
static int gpio_value,fg_values[2]={1,1},adc_value,adc_error,notices,last_missing,last_fake;
static int gpio_lookup_error,channel_error,scheme_count=2;
static struct mca_bmd_dev *allocation;static struct mca_bmd_dev *registered;
static struct iio_channel channel;
static bool stop_hook;static pthread_t active_thread;
static pthread_barrier_t reached_provider,release_provider,reached_drain;
static int fault(void){return ++sequence==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int f){(void)d;(void)f;if(fault())return NULL;allocation=calloc(1,n);assert(allocation);return allocation;}
static void devres_release(void){if(allocation){assert(!allocation->workqueue&&!live_wq&&!live_group&&!live_strategy);free(allocation);allocation=NULL;}owned_gpio=owned_iio=0;}
static void platform_set_drvdata(struct platform_device *d,void *p){d->dev.data=p;}
static void *platform_get_drvdata(struct platform_device *d){return d->dev.data;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static int of_property_count_u32_elems(struct device_node *n,const char *p){(void)n;(void)p;return scheme_count;}
static int mca_parse_dts_u32_array(struct device_node *n,const char *name,int *out,int count){(void)n;(void)name;assert(count==2);if(fault())return -EINVAL;memcpy(out,dt_schemes,sizeof(dt_schemes));return 0;}
static int mca_parse_dts_u32(struct device_node *n,const char *name,int *out,int def){(void)n;(void)name;(void)def;if(fault())return -EINVAL;*out=dt_fg;return 0;}
static int of_get_named_gpio(struct device_node *n,const char *name,int index){(void)n;assert(!strcmp(name,"btb_gpio")&&index==0);if(fault())return -EPROBE_DEFER;return gpio_lookup_error?gpio_lookup_error:15;}
static int devm_gpio_request_one(struct device *d,int gpio,int flags,const char *name){(void)d;assert(gpio==15&&flags==GPIOF_IN&&!strcmp(name,"btb_gpio")&&!owned_gpio);if(fault())return -EIO;owned_gpio++;return 0;}
static struct iio_channel *devm_iio_channel_get(struct device *d,const char *name){(void)d;assert(!strcmp(name,"btb_adc"));if(fault())return ERR_PTR(-EPROBE_DEFER);if(channel_error)return ERR_PTR(channel_error);owned_iio++;return &channel;}
static int iio_read_channel_processed(struct iio_channel *c,int *out){assert(c==&channel);if(adc_error)return adc_error;*out=adc_value;return 0;}
static int gpio_get_value_cansleep(int gpio){assert(gpio==15&&owned_gpio);return gpio_value;}
static int strategy_class_fg_is_chip_ok(void){if(stop_hook){pthread_barrier_wait(&reached_provider);pthread_barrier_wait(&release_provider);}return fg_values[0];}
static int strategy_class_fg_dual_is_chip_ok(int index){return fg_values[index];}
#define INIT_DELAYED_WORK(w,f) do{(w)->work.fn=f;(w)->work.pending=false;}while(0)
static struct workqueue_struct *alloc_ordered_workqueue(const char *name,int flags){(void)flags;assert(!strcmp(name,"mca_bmd"));if(fault())return NULL;live_wq++;return calloc(1,sizeof(struct workqueue_struct));}
static int mod_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay){assert(q&&w->work.fn);w->work.pending=true;w->work.delay=delay;return 1;}
static void cancel_delayed_work_sync(struct delayed_work *w){assert(w->work.fn);drains++;w->work.pending=false;if(stop_hook&&w==&allocation->monitor_bmd_work){pthread_barrier_wait(&reached_drain);assert(!pthread_join(active_thread,NULL));stop_hook=false;}}
static void destroy_workqueue(struct workqueue_struct *q){assert(live_wq==1&&!live_strategy&&!live_group);live_wq--;free(q);}
static int mca_strategy_ops_register(int type,int(*event)(int,int,void *),int(*status)(int,void *,void *),void *config,void *data){(void)type;(void)config;struct mca_bmd_dev *i=data;assert(i->monitor_bmd_work.work.fn&&i->delay_report_bmd_sts_work.work.fn&&i->workqueue&&i->batt_missing);if(fault())return -ENOMEM;if(live_strategy)return -EEXIST;u32 v=0;assert(!status(STRATEGY_STATUS_TYPE_BMD_BATT_MISSING,&v,i)&&v==1);assert(!event(MCA_EVENT_USB_CONNECT,1,i));live_strategy++;registered=i;return 0;}
static int mca_strategy_ops_unregister(int type,void *data){(void)type;assert(live_strategy==1&&registered==data);live_strategy--;registered=NULL;return 0;}
static void mca_sysfs_init_attrs(struct attribute **attrs,struct mca_sysfs_attr_info *fields,int count){(void)attrs;(void)fields;assert(count==3);}
static int mca_sysfs_create_link_group(int id,const char *name,struct device *d,const struct attribute_group *group){(void)id;(void)group;assert(!strcmp(name,"bmd")&&d->data);if(fault())return -EIO;live_group++;return 0;}
static void mca_sysfs_remove_link_group(int id,const char *name,struct device *d,const struct attribute_group *group){(void)id;(void)d;(void)group;assert(!strcmp(name,"bmd")&&live_group==1);live_group--;}
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,struct mca_sysfs_attr_info *table,int count){for(int n=0;n<count;n++)if(!strcmp(name,table[n].attr.attr.name))return table+n;return NULL;}
static int sysfs_emit(char *buf,const char *fmt,int value){return snprintf(buf,PAGE_SIZE,fmt,value);}
static void mca_event_block_notify(int type,int event,void *data){(void)type;if(event==MCA_EVENT_BATT_BTB_CHANGE){last_missing=*(bool *)data;notices++;}else if(event==MCA_EVENT_BATTERY_FAKE_POWER)last_fake=*(int *)data;}
static void mca_charge_mievent_set_state(int state,int event){(void)state;(void)event;}
static void mca_charge_mievent_report(int event,void *data,int n){(void)event;(void)data;(void)n;}
/* FUNCTIONS */
static void reset(void){sequence=fail_at=drains=notices=dt_fg=gpio_value=adc_error=channel_error=gpio_lookup_error=0;dt_schemes[0]=STOCK_MASTER_SCHEME;dt_schemes[1]=STOCK_SLAVE_SCHEME;fg_values[0]=fg_values[1]=1;scheme_count=2;assert(!live_wq&&!live_group&&!live_strategy&&!owned_gpio&&!owned_iio);}
static void *monitor(void *arg){struct mca_bmd_dev *i=arg;mca_bmd_monitor_workfunc(&i->monitor_bmd_work.work);return NULL;}
static void *remove_dev(void *arg){mca_bmd_remove(arg);return NULL;}
int main(void){
 alarm(25);struct device_node node={0};struct platform_device dev={.dev.of_node=&node};reset();assert(!mca_bmd_probe(&dev));int checkpoints=sequence;struct mca_bmd_dev *i=dev.dev.data;assert(i->batt_missing&&i->fake_batt==1&&owned_gpio==1);mca_bmd_monitor_workfunc(&i->monitor_bmd_work.work);assert(!i->batt_missing&&!i->fake_batt&&notices==1&&i->monitor_bmd_work.work.delay==500);assert(!mca_bmd_process_event(MCA_EVENT_USB_CONNECT,1,i)&&i->monitor_bmd_work.work.delay==0);mca_bmd_shutdown(&dev);assert(drains==2);mca_bmd_remove(&dev);assert(drains==2&&!dev.dev.data);devres_release();
 for(int n=1;n<=checkpoints;n++){reset();fail_at=n;assert(mca_bmd_probe(&dev)<0&&!dev.dev.data);devres_release();}
 reset();dt_schemes[0]=ADC_SCHEME;channel_error=-EPROBE_DEFER;assert(mca_bmd_probe(&dev)==-EPROBE_DEFER&&!dev.dev.data);devres_release();reset();dt_schemes[0]=INT_SCHEME;assert(mca_bmd_probe(&dev)==-EOPNOTSUPP);devres_release();reset();dt_fg=2;assert(mca_bmd_probe(&dev)==-EINVAL);devres_release();reset();scheme_count=3;assert(mca_bmd_probe(&dev)==-EINVAL);devres_release();
 reset();assert(!mca_bmd_probe(&dev));i=dev.dev.data;bool online=false;assert(mca_bmd_get_btb_status(i,-1,&online)==-EINVAL&&mca_bmd_get_btb_status(i,2,&online)==-EINVAL);int cases=0;
 for(int dual=0;dual<=1;dual++){i->fg_type=dual;for(int a=0;a<=1;a++)for(int b=0;b<=1;b++){fg_values[0]=a;gpio_value=!b;mca_bmd_monitor_workfunc(&i->monitor_bmd_work.work);assert(i->batt_missing==(!a||!b)&&i->fake_batt==(dual?(!a||!b):(!a&&!b)));cases++;}}
 i->fg_type=0;fg_values[0]=-EAGAIN;gpio_value=0;mca_bmd_monitor_workfunc(&i->monitor_bmd_work.work);assert(i->batt_missing&&i->fake_batt);struct device_attribute master={{"btb_master_status"}};char buf[PAGE_SIZE];assert(btb_sysfs_show(&dev.dev,&master,buf)==-EAGAIN);gpio_value=-EIO;fg_values[0]=1;assert(mca_bmd_get_btb_status(i,SLAVE_BTB,&online)==-EIO);fg_values[0]=2;assert(mca_bmd_get_btb_status(i,MASTER_BTB,&online)==-ERANGE);cases+=3;
 i->bmd_scheme[MASTER_BTB].scheme=ADC_SCHEME;i->bmd_scheme[MASTER_BTB].channel=&channel;for(int v=0;v<=2000;v++){adc_value=v;assert(!mca_bmd_get_btb_status(i,MASTER_BTB,&online)&&online==!(v>1700&&v<1900));cases++;}adc_error=-EIO;assert(mca_bmd_get_btb_status(i,MASTER_BTB,&online)==-EIO);adc_error=0;adc_value=-1;assert(mca_bmd_get_btb_status(i,MASTER_BTB,&online)==-ERANGE);cases+=2;
 i->bmd_scheme[MASTER_BTB].scheme=IIC_SCHEME;fg_values[0]=1;gpio_value=0;stop_hook=true;pthread_barrier_init(&reached_provider,NULL,2);pthread_barrier_init(&release_provider,NULL,2);pthread_barrier_init(&reached_drain,NULL,2);assert(!pthread_create(&active_thread,NULL,monitor,i));pthread_barrier_wait(&reached_provider);pthread_t removing;assert(!pthread_create(&removing,NULL,remove_dev,&dev));pthread_barrier_wait(&release_provider);pthread_barrier_wait(&reached_drain);assert(!pthread_join(removing,NULL));assert(i->stopping&&mca_bmd_process_event(MCA_EVENT_USB_CONNECT,1,i)==-ENODEV);devres_release();
 printf("{\"probe_failure_points\":%d,\"sensor_boundary_error_and_presence_cases\":%d,\"active_worker_removal\":true,\"two_work_drains\":true,\"shutdown_remove_idempotent\":true,\"plugged_in_monitoring\":true,\"failed_reads_report_missing\":true,\"gpio_binding_and_ownership\":true,\"deferred_iio_and_invalid_schema_rejected\":true}\n",checkpoints,cases);
}
