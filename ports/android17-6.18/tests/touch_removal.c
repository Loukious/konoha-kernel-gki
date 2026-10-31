#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define WRITE_ONCE(v,x) ((v)=(x))
#define CONFIG_TOUCHSCREEN_NVT_DEBUG_FS 1
#define CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE 1
#define NVT_TOUCH_MP 1
#define NVT_TOUCH_EXT_PROC 1
#define NVT_TOUCH_PROC 1
#define NVT_TOUCH_ESD_PROTECT 0
#define BOOT_UPDATE_FIRMWARE 1
#define WAKEUP_GESTURE 1
#define TOUCH_THP_SUPPORT 1
#define TOUCH_ID 0
struct spi_device {int irq;struct {int unused;} dev;void *data;};
struct nvt_ts_data {bool removing,irq_wake_enabled;int dev_pm_suspend_completion,xbuf_lock,lock,nvt_fwu_work,resume_work,power_supply_work;void *debugfs,*event_wq,*input_dev,*eventbuf_debug,*data_buf,*rbuf,*xbuf;};
static struct nvt_ts_data *ts;static void *nvt_fwu_wq;static unsigned live,step;
static void *alloc(void){void *p=malloc(16);assert(p);live++;return p;}
#define kfree(p) drop(p)
static void drop(void *p){if(p)live--;free(p);}
static struct nvt_ts_data *spi_get_drvdata(struct spi_device *c){return c->data;}
static void complete_all(int *c){assert(ts->removing&&step==0);step=1;}
static void debugfs_remove_recursive(void *d){assert(step==1);step=2;}
static void unregister_touch_panel(int id){assert(ts->removing&&step==2&&id==0);step=3;}
static void nvt_mp_proc_deinit(void){assert(step==3);step=4;}
static void nvt_extra_proc_deinit(void){assert(step==4);step=5;}
static void nvt_flash_proc_deinit(void){assert(step==5);step=6;}
static void cancel_delayed_work_sync(int *work){assert(step==6);step=7;}
static void cancel_work_sync(int *work){assert(step>=7&&step<10);step++;}
static void destroy_workqueue(void *q){assert(step>=7&&step<=9);drop(q);}
static void disable_irq_wake(int irq){assert(irq==55&&step==9);step=10;}
static void nvt_irq_enable(bool enabled){assert(!enabled&&ts->removing&&step==10);step=11;}
static void free_irq(int irq,struct nvt_ts_data *d){assert(d==ts&&step==11);step=12;}
static void device_init_wakeup(void *dev,int flag){assert(!flag&&step==12);step=13;}
static void nvt_gpio_deconfig(struct nvt_ts_data *d){assert(d==ts&&step==13);step=14;}
static void input_unregister_device(void *p){assert(step==14);step=15;drop(p);}
static void spi_set_drvdata(struct spi_device *d,void *p){assert(!p&&step==15);step=16;d->data=p;}
static void mutex_destroy(int *lock){assert(step==16);}
/* FUNCTIONS */
int main(void){struct spi_device dev={.irq=55};ts=calloc(1,sizeof(*ts));assert(ts);live++;dev.data=ts;ts->irq_wake_enabled=true;ts->event_wq=alloc();nvt_fwu_wq=alloc();ts->input_dev=alloc();ts->eventbuf_debug=alloc();ts->data_buf=alloc();ts->rbuf=alloc();ts->xbuf=alloc();nvt_ts_remove(&dev);assert(!live&&!ts&&!dev.data&&step==16);nvt_ts_remove(&dev);printf("{\"callback_and_work_drains_before_free\":true,\"all_buffers_released\":true,\"irq_wake_balanced\":true,\"absent_device_removal_safe\":true}\n");}
