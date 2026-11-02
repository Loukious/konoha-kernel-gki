#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define GFP_KERNEL 0
#define AW_RENAME_ENABLE 1
#define aw_dev_info(...) ((void)0)
#define aw_dev_err(...) ((void)0)
#define DISTINGUISH_CHIP_TYPE
#define IRQF_TRIGGER_RISING 1
struct device {int name;};
struct device_attribute {int kind;};
static struct device_attribute dev_attr_sipa_cmd={1},dev_attr_sipa_device={2};
struct platform_device {struct device dev;};
struct i2c_adapter {int nr;};
struct i2c_client {int addr;struct i2c_adapter *adapter;};
struct snd_soc_dai_driver {const char *name;};
static struct snd_soc_dai_driver aw882xx_dai[]={{"aw882xx-aif"}};
static int soc_codec_dev_aw882xx;
struct workqueue_struct {int live;};
struct delayed_work {unsigned mask;};
struct aw_monitor_desc {bool active;};
struct aw_device {struct workqueue_struct *work_queue;struct aw_monitor_desc monitor_desc;bool profiles;};
struct codec_ops {int (*register_codec)(struct device*,void*,struct snd_soc_dai_driver*,int);void (*unregister_codec)(struct device*);};
struct aw882xx {struct device *dev;struct codec_ops *codec_ops;int rename_flag;struct i2c_client *i2c;bool irq_requested,irq_disabled;int irq_gpio;struct workqueue_struct *work_queue;struct aw_device *aw_pa;struct delayed_work fw_work,interrupt_work,start_work,dc_work;void *codec;};
typedef struct {struct platform_device *pdev;bool command_created,device_created,device_available,interrupt_initialized;int irq,irq_pin,channel_num;struct delayed_work interrupt_work;} sipa_dev_t;
struct snd_soc_component {sipa_dev_t *data;};
static int alloc_fail,suffix_error,codec_error,registry_error,registered,withdrawn,registry_calls,cancelled,irq_disabled,irq_freed;
static int device_live,cmd_live,file_error,request_error,mapping_error,irq_requests;
static struct snd_soc_dai_driver dai_storage;
static void *devm_kzalloc(struct device*d,size_t n,int flags){(void)d;(void)n;(void)flags;return alloc_fail?NULL:&dai_storage;}
static int aw882xx_dai_drv_append_suffix(struct aw882xx*a,struct snd_soc_dai_driver*d,int n){(void)a;(void)d;(void)n;return suffix_error;}
static int fake_register(struct device*d,void*c,struct snd_soc_dai_driver*a,int n){(void)d;(void)c;(void)a;(void)n;if(!codec_error)registered++;return codec_error;}
static void fake_unregister(struct device*d){(void)d;assert(registered);registered--;withdrawn++;}
static int dev_set_name(struct device*d,const char*s,...){(void)d;(void)s;return 0;}
static int register_smartpa_codec(struct device*d,const char*n,const char*t){(void)d;assert(n&&strcmp(t,"aw882xx")==0);registry_calls++;return registry_error;}
static int gpio_is_valid(int pin){return pin>=0;}
static int gpio_to_irq(int pin){return mapping_error?mapping_error:pin+10;}
static void disable_irq(int irq){assert(irq>=0);irq_disabled++;}
static void cancel_delayed_work_sync(struct delayed_work*w){cancelled|=w->mask;}
static void aw882xx_monitor_stop(struct aw_monitor_desc*m){m->active=false;}
static void aw882xx_device_stop(struct aw_device*d){assert(!d->monitor_desc.active);}
static void destroy_workqueue(struct workqueue_struct*w){assert(cancelled==15);w->live=0;}
static void aw882xx_dev_deinit(struct aw_device*d){assert(!d->work_queue);d->profiles=false;}
static int device_create_file(struct device*d,struct device_attribute*a){(void)d;if(file_error==a->kind)return -EIO;if(a->kind==1)cmd_live++;else device_live++;return 0;}
static void device_remove_file(struct device*d,struct device_attribute*a){(void)d;if(a->kind==1){assert(cmd_live);cmd_live--;}else {assert(device_live);device_live--;}}
static sipa_dev_t *snd_soc_component_get_drvdata(struct snd_soc_component*c){return c->data;}
static void free_irq(int irq,void*d){(void)d;assert(irq>=0);irq_freed++;}
static void sia91xx_irq(void){}
static int request_irq(int irq,void(*fn)(void),int flags,const char*name,void*d){(void)irq;(void)fn;(void)flags;(void)name;(void)d;irq_requests++;return request_error;}
/* PRODUCTION_FUNCTIONS */
static unsigned cases;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);abort();}cases++;}while(0)
int main(void){
 struct device dev={0};struct codec_ops ops={fake_register,fake_unregister};struct aw882xx aw={.dev=&dev,.codec_ops=&ops};
 alloc_fail=1;CHECK(aw_componet_codec_register(&aw)==-ENOMEM);CHECK(registered==0&&registry_calls==0);alloc_fail=0;
 suffix_error=-E2BIG;CHECK(aw_componet_codec_register(&aw)==-E2BIG);CHECK(registered==0&&registry_calls==0);suffix_error=0;
 codec_error=-EPERM;CHECK(aw_componet_codec_register(&aw)==-EPERM);CHECK(registry_calls==0);codec_error=0;
 registry_error=-EBUSY;CHECK(aw_componet_codec_register(&aw)==-EBUSY);CHECK(registered==0&&withdrawn==1);registry_error=0;
 CHECK(aw_componet_codec_register(&aw)==0);CHECK(registered==1);fake_unregister(&dev);
 struct workqueue_struct wq={1};struct aw_device pa={.work_queue=&wq,.profiles=true,.monitor_desc={true}};
 aw.work_queue=&wq;aw.aw_pa=&pa;aw.codec=&dev;aw.irq_requested=true;aw.irq_gpio=2;
 aw.fw_work.mask=1;aw.interrupt_work.mask=2;aw.start_work.mask=4;aw.dc_work.mask=8;
 aw882xx_stop_component(&aw);CHECK(!wq.live&&!pa.profiles&&!aw.codec&&!aw.work_queue&&cancelled==15);CHECK(irq_disabled==1&&aw.irq_disabled);
 aw882xx_stop_component(&aw);CHECK(irq_disabled==1);
 struct platform_device pdev={0};sipa_dev_t sia={.pdev=&pdev,.device_available=true,.irq=-1,.irq_pin=-1};
 file_error=2;CHECK(sipa_create_files(&sia)==-EIO);CHECK(!device_live&&!cmd_live);
 file_error=1;CHECK(sipa_create_files(&sia)==-EIO);CHECK(!device_live&&!cmd_live&&!sia.device_created);
 file_error=0;CHECK(sipa_create_files(&sia)==0);CHECK(cmd_live==1&&device_live==1);
 sipa_remove_files(&sia);CHECK(!cmd_live&&!device_live);sipa_remove_files(&sia);CHECK(!cmd_live&&!device_live);
 CHECK(sia91xx_register_interrupt(&sia)==0);CHECK(irq_requests==0&&sia.irq==-1);
 sia.irq_pin=3;mapping_error=-ENXIO;CHECK(sia91xx_register_interrupt(&sia)==-ENXIO);CHECK(irq_requests==0);mapping_error=0;
 request_error=-EBUSY;CHECK(sia91xx_register_interrupt(&sia)==-EBUSY);CHECK(sia.irq==-1);request_error=0;
 CHECK(sia91xx_register_interrupt(&sia)==0);CHECK(sia.irq==13);
 struct snd_soc_component comp={&sia};sia.interrupt_initialized=true;sia.interrupt_work.mask=16;cancelled=0;
 sipa_component_remove(&comp);CHECK(irq_freed==1&&sia.irq==-1&&cancelled==16&&!sia.interrupt_initialized);
 sipa_component_remove(&comp);CHECK(irq_freed==1);
 printf("PASS %u lifecycle cases\n",cases);return 0;
}
