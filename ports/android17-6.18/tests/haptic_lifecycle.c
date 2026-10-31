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
#define ESHUTDOWN 108
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define I2C_FUNC_I2C 1
#define GPIOF_OUT_INIT_LOW 0
#define GPIOF_IN 1
#define AW8697 1
#define AW86927 2
#define AW8697_FLAG_SKIP_INTERRUPTS 1
#define AW86927_FLAG_SKIP_INTERRUPTS 1
#define AW_CHECK_QUAL
#define AW_RAM_UPDATE_DELAY
#define IRQF_TRIGGER_FALLING 1
#define IRQF_ONESHOT 2
#define EV_FF 1
#define FF_CONSTANT 2
#define FF_GAIN 3
#define FF_PERIODIC 4
#define FF_CUSTOM 5
#define FF_EFFECT_COUNT_MAX 32
#define CLOCK_MONOTONIC 1
#define HRTIMER_MODE_REL 1
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define msecs_to_jiffies(t) (t)
#define aw_info(...) ((void)0)
#define aw_err(...) ((void)0)
#define XM_HAP_REGISTER_EXCEPTION(...) ((void)0)
static int steps,fail_at,chip_selected,ring,irq_live,input_live,sysfs_live,misc_live,queue_live,wakeup,queued,i2c_calls,CUSTOME_WAVE_ID;
static int step(void){steps++;return steps==fail_at;}
struct mutex { int initialized; };
static void mutex_init(struct mutex *m){m->initialized=1;}
static void mutex_lock(struct mutex *m){assert(m->initialized);}
static void mutex_unlock(struct mutex *m){assert(m->initialized);}
typedef struct { int value; } atomic_t;
static void atomic_set(atomic_t *a,int v){WRITE_ONCE(a->value,v);}
typedef struct { int initialized; } wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t *w){w->initialized=1;}
static void wake_up_interruptible(wait_queue_head_t *w){assert(w->initialized);}
enum hrtimer_restart { HRTIMER_NORESTART=0 };
struct hrtimer { int initialized;enum hrtimer_restart (*function)(struct hrtimer *); };
static void hrtimer_setup(struct hrtimer *t,enum hrtimer_restart (*f)(struct hrtimer *),int c,int m){(void)c;(void)m;t->initialized=1;t->function=f;}
static void hrtimer_cancel(struct hrtimer *t){assert(t->initialized);}
struct work_struct { int initialized,pending,running;void (*function)(struct work_struct *); };
struct delayed_work { struct work_struct work; };
struct workqueue_struct { int active; };
#define INIT_WORK(w,f) ((w)->initialized=1,(w)->function=(f))
#define INIT_DELAYED_WORK(w,f) INIT_WORK(&(w)->work,f)
static int queue_work(struct workqueue_struct *q,struct work_struct *w){assert(q&&q->active&&w->initialized);w->pending=1;queued++;return 1;}
static int queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay){(void)delay;return queue_work(q,&w->work);}
static int mod_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay){return queue_delayed_work(q,w,delay);}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static void cancel_work_sync(struct work_struct *w){assert(w->initialized);pthread_mutex_lock(&gate);while(w->running)pthread_cond_wait(&condition,&gate);w->pending=0;pthread_mutex_unlock(&gate);}
static void cancel_delayed_work_sync(struct delayed_work *w){cancel_work_sync(&w->work);}
static struct workqueue_struct *create_singlethread_workqueue(const char *n){(void)n;if(step())return NULL;struct workqueue_struct *q=calloc(1,sizeof(*q));assert(q);q->active=1;queue_live++;return q;}
static void destroy_workqueue(struct workqueue_struct *q){assert(q&&q->active);q->active=0;free(q);queue_live--;}
struct device_node { int unused; };
struct device { struct device_node *of_node;void *data;int kobj; };
struct i2c_client { struct device dev;void *adapter; };
struct ff_effect { int unused; };
struct input_dev;
struct ff_device { int (*upload)(struct input_dev *,struct ff_effect *,struct ff_effect *);int (*playback)(struct input_dev *,int,int);int (*erase)(struct input_dev *,int);void (*set_gain)(struct input_dev *,uint16_t); };
struct input_dev { const char *name;struct ff_device *ff;void *data; };
struct audio { struct mutex lock;struct hrtimer timer;struct work_struct work; };
struct mock_aw { struct device *dev;struct i2c_client *i2c;int reset_gpio,irq_gpio,flags,effects_count;struct {int effect_max;}info;struct input_dev *input_dev;struct workqueue_struct *work_queue;struct mutex lock,rtp_lock;struct work_struct vibrator_work,rtp_work,set_gain_work,stop_work;struct delayed_work ram_work;struct hrtimer timer,stop_timer,hap_disable_timer;struct audio haptic_audio;atomic_t is_in_rtp_loop,exit_in_rtp_loop,is_in_write_loop;wait_queue_head_t wait_q,stop_wait_q;bool stopping;int ram_init,rtp_init,nv_flag;struct {int length_us;}play; };
/* AWINIC */
static void *managed[8];static int managed_count;
static void *devm_kzalloc(struct device *d,size_t n,int flags){(void)d;(void)flags;if(step())return NULL;void *p=calloc(1,n);assert(p&&managed_count<8);managed[managed_count++]=p;return p;}
static struct input_dev *devm_input_allocate_device(struct device *d){return devm_kzalloc(d,sizeof(struct input_dev),0);}
static void free_managed(struct awinic *aw){struct input_dev *input=aw?aw->input_dev:NULL;if(input)free(input->ff);for(int n=0;n<managed_count;n++)free(managed[n]);managed_count=0;}
static void i2c_set_clientdata(struct i2c_client *i,void *p){i->dev.data=p;}
static struct awinic *i2c_get_clientdata(struct i2c_client *i){return i->dev.data;}
static int i2c_check_functionality(void *a,int f){(void)a;(void)f;return !step();}
static void device_init_wakeup(struct device *d,bool enable){(void)d;wakeup=enable;}
static int awinic_parse_dt(struct awinic *a,struct device *d,struct device_node *n){(void)d;(void)n;a->reset_gpio=4;a->irq_gpio=5;return step()?-EIO:0;}
static int awinic_parse_chipid(struct awinic *a){a->name=chip_selected;return step()?-EIO:0;}
static int gpio_is_valid(int gpio){return gpio>=0;}
static int gpio_to_irq(int gpio){return gpio+100;}
static int devm_gpio_request_one(struct device *d,int gpio,int flags,const char *name){(void)d;(void)gpio;(void)flags;(void)name;return step()?-EIO:0;}
static void xm_hap_driver_init(bool enable){(void)enable;}
static int parse_backend(struct mock_aw *a){a->effects_count=2;a->info.effect_max=3;return step()?-EIO:0;}
static int aw8697_parse_dt(struct device *d,struct mock_aw *a,struct device_node *n){(void)d;(void)n;return parse_backend(a);}
static int aw86927_parse_dt(struct mock_aw *a,struct device *d,struct device_node *n){(void)d;(void)n;return parse_backend(a);}
static int aw86927_check_qualify(struct mock_aw *a){(void)a;return step();}
static void dependencies_ready(struct mock_aw *a){assert(a&&ring&&a->work_queue&&a->lock.initialized&&a->rtp_lock.initialized&&a->haptic_audio.lock.initialized);struct work_struct *works[]={&a->vibrator_work,&a->rtp_work,&a->set_gain_work,&a->stop_work,&a->haptic_audio.work,&a->ram_work.work};for(int n=0;n<6;n++)assert(works[n]->initialized);assert(a->timer.initialized&&a->stop_timer.initialized&&a->hap_disable_timer.initialized&&a->haptic_audio.timer.initialized);}
static int aw8697_haptic_init(struct mock_aw *a){dependencies_ready(a);return step()?-EIO:0;}
static int aw86927_haptic_init(struct mock_aw *a){return aw8697_haptic_init(a);}
static void aw8697_interrupt_setup(struct mock_aw *a){dependencies_ready(a);}
static void aw86927_interrupt_setup(struct mock_aw *a){dependencies_ready(a);}
static int aw8697_irq(int irq,void *a){(void)irq;(void)a;return 0;}
static int aw86927_irq(int irq,void *a){return aw8697_irq(irq,a);}
static int devm_request_threaded_irq(struct device *d,int irq,void *top,int (*thread)(int,void *),int flags,const char *name,void *a){(void)d;(void)irq;(void)top;(void)thread;(void)flags;(void)name;dependencies_ready(a);if(step())return -EIO;irq_live++;return 0;}
static void synchronize_irq(int irq){(void)irq;assert(irq_live);}
static void devm_free_irq(struct device *d,int irq,void *a){(void)d;(void)irq;(void)a;assert(irq_live==1);irq_live--;}
static void input_set_drvdata(struct input_dev *d,void *a){d->data=a;}
static void input_set_capability(struct input_dev *d,int type,int value){(void)d;(void)type;(void)value;}
static int input_ff_create(struct input_dev *d,int count){assert(count>=32);if(step())return -EIO;d->ff=calloc(1,sizeof(*d->ff));assert(d->ff);return 0;}
#define FF_CALLBACKS(chip) static int chip##_haptics_upload_effect(struct input_dev *d,struct ff_effect *e,struct ff_effect *o){(void)d;(void)e;(void)o;return 0;}static int chip##_haptics_playback(struct input_dev *d,int e,int v){(void)d;(void)e;(void)v;return 0;}static int chip##_haptics_erase(struct input_dev *d,int e){(void)d;(void)e;return 0;}static void chip##_haptics_set_gain(struct input_dev *d,uint16_t g){(void)d;(void)g;}static void chip##_haptics_set_gain_work_routine(struct work_struct *w){(void)w;}static void chip##_vibrator_work_routine(struct work_struct *w){(void)w;}static void chip##_rtp_work_routine(struct work_struct *w){(void)w;}static void chip##_haptic_audio_work_routine(struct work_struct *w){(void)w;}static enum hrtimer_restart chip##_vibrator_timer_func(struct hrtimer *t){(void)t;return 0;}static enum hrtimer_restart chip##_haptic_audio_timer_func(struct hrtimer *t){(void)t;return 0;}
FF_CALLBACKS(aw8697)
FF_CALLBACKS(aw86927)
static int input_register_device(struct input_dev *d){dependencies_ready(d->data);if(step())return -EIO;input_live++;return 0;}
static void input_unregister_device(struct input_dev *d){(void)d;assert(input_live==1);input_live--;}
static int aw8697_vibrator_attribute_group,aw86927_vibrator_attribute_group;
static int sysfs_create_group(void *kobj,void *group){(void)group;struct device *d=container_of(kobj,struct device,kobj);struct awinic *a=d->data;dependencies_ready(a->name==AW8697?a->aw8697:a->aw86927);if(step())return -EIO;sysfs_live++;return 0;}
static void sysfs_remove_group(void *kobj,void *group){(void)kobj;(void)group;assert(sysfs_live==1);sysfs_live--;}
static int aw8697_misc_register(struct mock_aw *a){dependencies_ready(a);if(step())return -EIO;misc_live++;return 0;}
static void aw8697_misc_unregister(void){assert(misc_live==1);misc_live--;}
static int create_rb(void){if(step())return -ENOMEM;assert(!ring);ring=1;return 0;}
static void rb_force_exit(void){assert(ring);}
static int release_rb(void){assert(ring);ring=0;return 0;}
static void aw8697_haptic_stop(struct mock_aw *a){assert(a->stopping);i2c_calls++;}
static void aw86927_haptic_stop(struct mock_aw *a){aw8697_haptic_stop(a);}
static void aw8697_haptic_play_go(struct mock_aw *a,bool enable){(void)a;(void)enable;i2c_calls++;}
#define AW86927_REG_PLAYCFG4 1
#define AW86927_BIT_PLAYCFG4_STOP_ON 1
static int aw86927_i2c_write(struct mock_aw *a,int reg,int value){(void)a;(void)reg;(void)value;i2c_calls++;return 0;}
static void pm_relax(struct device *d){(void)d;}
static void vfree(void *p){assert(!p);}
static void *aw8697_rtp,*aw86927_rtp;
static const char *awinic_ram_name[] = {"normal","backup"};
struct firmware { size_t size;const unsigned char *data; };
static struct firmware firmware;
static int fw_fail,fw_block,fw_started,fw_unblock,fw_loaded,fw_released,remove_done;
static int request_firmware(const struct firmware **out,const char *name,struct device *d){(void)name;(void)d;pthread_mutex_lock(&gate);if(fw_block){fw_started=1;pthread_cond_broadcast(&condition);while(!fw_unblock)pthread_cond_wait(&condition,&gate);}pthread_mutex_unlock(&gate);if(fw_fail)return -ENOENT;*out=&firmware;return 0;}
static void release_firmware(const struct firmware *f){assert(f==&firmware);fw_released++;}
static void aw8697_ram_loaded(const struct firmware *f,void *a){(void)a;fw_loaded++;release_firmware(f);}
static void aw86927_ram_loaded(const struct firmware *f,void *a){aw8697_ram_loaded(f,a);}
/* SOURCE */
static void *firmware_worker(void *arg){struct delayed_work *work=arg;work->work.function(&work->work);pthread_mutex_lock(&gate);work->work.running=0;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);return NULL;}
static void *remove_worker(void *arg){awinic_i2c_remove(arg);WRITE_ONCE(remove_done,1);return NULL;}
static void no_resources(void){assert(!ring&&!irq_live&&!input_live&&!sysfs_live&&!misc_live&&!queue_live&&!wakeup);}
int main(void){
 alarm(25);int faults=0,firmware_cases=0,timer_cases=0;struct device_node node={0};
 for(chip_selected=AW8697;chip_selected<=AW86927;chip_selected++){
  struct i2c_client i={.dev={.of_node=&node}};fail_at=0;steps=0;assert(!awinic_i2c_probe(&i));int count=steps;struct awinic *a=i.dev.data;awinic_i2c_remove(&i);no_resources();free_managed(a);
  for(int f=1;f<=count;f++){i.dev.data=NULL;steps=0;fail_at=f;int ret=awinic_i2c_probe(&i);assert(ret<0);a=managed_count?managed[0]:NULL;no_resources();free_managed(a);faults++;}
  steps=0;fail_at=0;assert(!awinic_i2c_probe(&i));a=i.dev.data;struct mock_aw *backend=chip_selected==AW8697?a->aw8697:a->aw86927;int before=i2c_calls,q=queued;
  backend->stop_timer.function(&backend->stop_timer);backend->hap_disable_timer.function(&backend->hap_disable_timer);assert(i2c_calls==before&&queued==q+2);backend->stop_work.function(&backend->stop_work);assert(i2c_calls==before+1);backend->stopping=true;q=queued;backend->stop_timer.function(&backend->stop_timer);backend->hap_disable_timer.function(&backend->hap_disable_timer);assert(queued==q);backend->stopping=false;timer_cases+=4;
  fw_fail=1;backend->ram_work.work.function(&backend->ram_work.work);assert(!fw_loaded&&!fw_released);fw_fail=0;firmware_cases++;
  fw_block=1;fw_started=fw_unblock=remove_done=0;backend->ram_work.work.running=1;pthread_t loader,remover;assert(!pthread_create(&loader,NULL,firmware_worker,&backend->ram_work));pthread_mutex_lock(&gate);while(!fw_started)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);assert(!pthread_create(&remover,NULL,remove_worker,&i));usleep(20000);assert(!READ_ONCE(remove_done));pthread_mutex_lock(&gate);fw_unblock=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);assert(!pthread_join(loader,NULL)&&!pthread_join(remover,NULL));assert(remove_done&&!fw_loaded&&fw_released==1);fw_released=fw_block=0;firmware_cases++;no_resources();awinic_cleanup(a);no_resources();free_managed(a);
 }
 printf("{\"probe_fault_points\":%d,\"timer_atomic_context_cases\":%d,\"firmware_failure_removal_cases\":%d,\"publication_dependencies_initialized\":true,\"teardown_idempotent\":true}\n",faults,timer_cases,firmware_cases);
}
