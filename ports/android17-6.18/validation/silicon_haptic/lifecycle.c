#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define I2C_FUNC_I2C 1
#define SIH_HAPTIC_MMAP_DEV_INDEX 0
#define SIH_HAPTIC_DEV_NUM 1
#define FF_EFFECT_COUNT_MAX 32
#define FF_CONSTANT 1
#define FF_GAIN 2
#define FF_PERIODIC 3
#define FF_CUSTOM 4
#define EV_FF 1
#define HRTIMER_MODE_REL 0
#define FW_ACTION_UEVENT 1
#define THIS_MODULE NULL
#define hp_info(...) ((void)0)
#define hp_err(...) ((void)0)
#define XM_HAP_REGISTER_EXCEPTION(...) ((void)0)
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((intptr_t)(p))
struct mutex {bool initialized;pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={.initialized=true,.lock=PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){m->initialized=true;assert(!pthread_mutex_init(&m->lock,NULL));}
static void mutex_lock(struct mutex *m){assert(m->initialized);assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
typedef atomic_int atomic_t;
#define atomic_set(a,v) atomic_store(a,v)
#define atomic_inc(a) atomic_fetch_add(a,1)
#define atomic_read(a) atomic_load(a)
#define atomic_dec_and_test(a) (atomic_fetch_sub(a,1)==1)
typedef struct {bool initialized;} wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t *w){w->initialized=true;}
static void wake_up_interruptible(wait_queue_head_t *w){assert(w->initialized);}
static void wake_up_all(wait_queue_head_t *w){assert(w->initialized);}
static void finish_fw(bool success);
#define wait_event(w,c) do {assert((w).initialized);if(!(c))finish_fw(true);assert(c);} while(0)
struct work_struct {bool initialized;};
#define INIT_WORK(w,f) ((w)->initialized=true)
static void cancel_work_sync(struct work_struct *w){assert(w->initialized);}
struct hrtimer {bool initialized;};
static void hrtimer_setup(struct hrtimer *t,void *fn,int clock,int mode){t->initialized=true;}
static void hrtimer_cancel(struct hrtimer *t){assert(t->initialized);}
struct device_node {int unused;};
struct device {struct device_node *of_node;void *data;int kobj;};
struct i2c_client {struct device dev;void *adapter;};
struct input_dev;
struct ff_device {int (*upload)(struct input_dev*,void*,void*);int (*playback)(struct input_dev*,int,int);int (*erase)(struct input_dev*,int);void (*set_gain)(struct input_dev*,uint16_t);};
struct input_dev {const char *name;struct ff_device *ff;void *data;};
struct workqueue_struct {int unused;};
struct regmap {int unused;};
typedef struct sih_haptic sih_haptic_t;
struct chip_ops {void (*stop)(sih_haptic_t*);void (*get_tracking_f0)(sih_haptic_t*);};
struct stream_ops {int (*stream_rtp_work_init)(sih_haptic_t*);void (*stream_rtp_work_release)(sih_haptic_t*);};
struct sih_haptic {
 struct input_dev *input_dev;struct device *dev;struct i2c_client *i2c;
 struct mutex lock,fw_lock;atomic_t fw_refs;wait_queue_head_t fw_wait;
 struct hrtimer timer;struct workqueue_struct *work_queue;
 struct {struct work_struct ram_work,gain_work;bool ram_init;int lib_index;} ram;
 struct {struct work_struct rtp_work;struct mutex rtp_lock;atomic_t is_in_rtp_loop,exit_in_rtp_loop,is_in_write_loop;wait_queue_head_t wait_q,stop_wait_q;void *rtp_cont;} rtp;
 struct {struct input_dev vib_dev;} soft_frame;
 struct {int irq_gpio;} chip_attr;
 struct {int effects_count,effect_max;} chip_ipara;
 struct {struct regmap *regmapping;void *config;} regmapp;
 struct {void *reg_addr;} chip_reg;
 struct {struct work_struct stream_work;void *rtp_ptr,*start_buf;} stream_para;
 struct chip_ops *hp_func;struct stream_ops *stream_func;
 bool stopping,input_registered,ff_created,irq_requested,ring_owned,sysfs_created,stream_initialized;
};
static struct {int sih_num;sih_haptic_t *g_haptic[1];} g_haptic_t;
static DEFINE_MUTEX(sih_global_lock);
static int steps,fail_at,ring,queues,irq,inputs,sysfs,wakeup,streams,fw_data,fw_release,CUSTOME_WAVE_ID;
static struct input_dev *registered_input;
static void *managed[4];static int managed_count;
static bool fail(void){return ++steps==fail_at;}
static void *devm_kzalloc(struct device *d,size_t n,int flags){if(fail())return NULL;void *p=calloc(1,n);assert(p);managed[managed_count++]=p;return p;}
static struct input_dev *devm_input_allocate_device(struct device *d){return devm_kzalloc(d,sizeof(struct input_dev),0);}
static void devm_kfree(struct device *d,void *p){(void)d;(void)p;}
static bool i2c_check_functionality(void *a,int f){return !fail();}
static void i2c_set_clientdata(struct i2c_client *i,void *p){i->dev.data=p;}
static sih_haptic_t *i2c_get_clientdata(struct i2c_client *i){return i->dev.data;}
static void device_init_wakeup(struct device *d,bool en){wakeup=en;}
static void pm_relax(struct device *d){}
static int gpio_to_irq(int gpio){return gpio+100;}
static void ready(sih_haptic_t *s){assert(s->lock.initialized&&s->rtp.rtp_lock.initialized&&s->fw_lock.initialized&&s->timer.initialized&&s->ram.ram_work.initialized&&s->ram.gain_work.initialized&&s->rtp.rtp_work.initialized&&s->stream_initialized&&s->work_queue&&ring);}
static void stop(sih_haptic_t *s){}
static void tracking(sih_haptic_t *s){}
static struct chip_ops ops={stop,tracking};
static int stream_init(sih_haptic_t *s){if(fail())return -ENOMEM;INIT_WORK(&s->stream_para.stream_work,NULL);streams++;return 0;}
static void stream_release(sih_haptic_t *s){assert(streams==1);cancel_work_sync(&s->stream_para.stream_work);streams--;}
static struct stream_ops stream_ops={stream_init,stream_release};
static int sih_parse_dts(struct device *d,sih_haptic_t *s,struct device_node *n){return fail()?-EIO:0;}
static int sih_acquire_prepare_res(struct device *d,sih_haptic_t *s){return fail()?-EBUSY:0;}
static int sih_register_func(sih_haptic_t *s){if(fail())return -EIO;s->hp_func=&ops;s->stream_func=&stream_ops;return 0;}
static struct regmap map;
static struct regmap *haptic_regmap_init(struct i2c_client *i,void *c){return fail()?(void *)(intptr_t)-EIO:&map;}
static int create_rb(void){if(fail())return -ENOMEM;assert(!ring);ring++;return 0;}
static void rb_force_exit(void){assert(ring);}
static int release_rb(void){assert(ring==1);ring--;return 0;}
static struct workqueue_struct *create_singlethread_workqueue(const char *n){if(fail())return NULL;queues++;return calloc(1,sizeof(struct workqueue_struct));}
static void destroy_workqueue(struct workqueue_struct *q){assert(queues==1);queues--;free(q);}
static int vibrator_chip_init(sih_haptic_t *s){assert(s->lock.initialized&&ring&&s->work_queue);return fail()?-EIO:0;}
static int sih_acquire_irq_res(struct device *d,sih_haptic_t *s){ready(s);if(fail())return -EIO;irq++;return 0;}
static void devm_free_irq(struct device *d,int n,sih_haptic_t *s){assert(irq==1);irq--;}
static void input_set_drvdata(struct input_dev *d,void *p){d->data=p;}
static void input_set_capability(struct input_dev *d,int type,int flag){}
static int input_ff_create(struct input_dev *d,int n){if(fail())return -ENOMEM;d->ff=calloc(1,sizeof(*d->ff));return 0;}
static void input_ff_destroy(struct input_dev *d){free(d->ff);d->ff=NULL;}
static int sih_upload_effect(struct input_dev *d,void *a,void *b){return 0;}
static int sih_playback(struct input_dev *d,int a,int b){return 0;}
static int sih_erase(struct input_dev *d,int a){return 0;}
static void sih_set_gain(struct input_dev *d,uint16_t g){}
static int input_register_device(struct input_dev *d){ready(d->data);if(fail())return -EIO;inputs++;registered_input=d;return 0;}
static void input_unregister_device(struct input_dev *d){assert(inputs==1&&registered_input==d);inputs--;registered_input=NULL;input_ff_destroy(d);}
static int sih_vibra_attribute_group;
static int sysfs_create_group(int *kobj,void *group){if(fail())return -EIO;sysfs++;return 0;}
static void sysfs_remove_group(int *kobj,void *group){assert(sysfs==1);sysfs--;}
static void sih_vfree_container(sih_haptic_t *s,void *p){assert(!p);}
#define kfree free
static void xm_hap_driver_init(bool en){}
static void *haptic_timer_func;
static const char *awinic_ram_name[]={"native"};
struct firmware {int unused;};
static struct firmware firmware;
static void (*callback)(const struct firmware *,void *);
static void *fw_context;
static int request_firmware_nowait(void *m,int action,const char *n,struct device *d,int flags,void *context,void (*cb)(const struct firmware *,void*)){if(fail())return -EIO;assert(!callback);callback=cb;fw_context=context;return 0;}
static void release_firmware(const struct firmware *f){fw_release++;}
static void sih_ram_load_data(const struct firmware *f,void *context){fw_data++;release_firmware(f);}
static void finish_fw(bool success){assert(callback);void (*cb)(const struct firmware*,void*)=callback;callback=NULL;cb(success?&firmware:NULL,fw_context);}
/* SOURCE */
static void free_managed(void){for(int i=0;i<managed_count;i++)free(managed[i]);managed_count=0;}
static void no_resources(void){assert(!ring&&!queues&&!irq&&!inputs&&!sysfs&&!wakeup&&!streams&&!callback&&!g_haptic_t.g_haptic[0]);}
int main(void){
 struct device_node node={0};struct i2c_client i={.dev={.of_node=&node}};
 assert(!sih_i2c_probe(&i));int count=steps;finish_fw(true);sih_i2c_remove(&i);no_resources();free_managed();
 for(int f=1;f<=count;f++){steps=0;fail_at=f;i.dev.data=NULL;assert(sih_i2c_probe(&i)<0);no_resources();free_managed();}
 fail_at=0;steps=0;assert(!sih_i2c_probe(&i));finish_fw(false);sih_haptic_t *s=i.dev.data;
 assert(!sih_request_ram(s));s->stopping=true;int before=fw_data;finish_fw(true);assert(fw_data==before&&!atomic_read(&s->fw_refs));assert(sih_request_ram(s)==-ENODEV);
 sih_i2c_remove(&i);sih_cleanup(s);no_resources();free_managed();
 printf("{\"probe_fault_points\":%d,\"publication_dependencies_initialized\":true,\"firmware_completion_and_closed_admission\":true,\"teardown_idempotent\":true}\n",count);
}
