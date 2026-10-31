#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
struct device { void *of_node; bool wakeup; void *client; };
struct i2c_client { struct device dev; void *data; int irq; };
struct i2c_dev { struct i2c_client *client; bool irq_enabled, irq_wake_up; };
struct platform_gpio { unsigned irq, ven, clkreq, dwl_req; };
struct nfc_dev {
 void *reg, *ipcl;
 bool is_vreg_enabled, removing, nfc_ven_enabled;
 int nfc_state, read_wq;
 struct { struct platform_gpio gpio; struct { u32 vdd_levels[2], max_current; } ldo; } configs;
 struct { bool is_nfc_enabled; } cold_reset;
 struct i2c_dev i2c_dev;
 int (*nfc_disable_intr)(struct nfc_dev *);
 int (*nfc_enable_intr)(struct nfc_dev *);
};
/* TYPES */
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_info(...) ((void)0)
#define NFCLOG_IPC(...) ((void)0)
#define ENOIOCTLCMD 515
static int reg_object, step, fail_step, voltage, load, enabled, handles, wakeups, ven;
static int irq_enable_error, wake_enable_error, wake_disable_error;
static bool supply=true;
static bool failure(void) { return ++step==fail_step; }
static void *of_get_property(void *np,const char *name,void *len) { (void)np;(void)name;(void)len;return supply?&reg_object:NULL; }
static void *regulator_get(struct device *d,const char *name) { (void)d;(void)name;if(failure())return ERR_PTR(-EIO);handles++;return &reg_object; }
static void regulator_put(void *r) { assert(r==&reg_object&&handles==1&&!enabled);handles--; }
static int regulator_set_voltage(void *r,int lo,int hi) { assert(r==&reg_object&&hi==NFC_VDDIO_MAX);if(failure())return -EIO;voltage=lo;return 0; }
static int regulator_set_load(void *r,int value) { assert(r==&reg_object);if(failure())return -EIO;load=value;return 0; }
static int regulator_enable(void *r) { assert(r==&reg_object);if(failure())return -EIO;enabled++;return 0; }
static int regulator_disable(void *r) { assert(r==&reg_object&&enabled==1);if(failure())return -EIO;enabled--;return 0; }
static void set_valid_gpio(int gpio,int value) { (void)gpio;(void)value; }
static void gpio_set_ven(struct nfc_dev *d,int value) { (void)d;ven=value; }
static void wake_up_interruptible(int *wq) { (void)wq;wakeups++; }
static int intr_enable(struct nfc_dev *d) { if(irq_enable_error)return irq_enable_error;d->i2c_dev.irq_enabled=true;return 0; }
static int intr_disable(struct nfc_dev *d) { d->i2c_dev.irq_enabled=false;return 0; }
static struct i2c_client *to_i2c_client(struct device *d) { return d->client; }
static struct nfc_dev *i2c_get_clientdata(struct i2c_client *c) { return c->data; }
static bool device_may_wakeup(struct device *d) { return d->wakeup; }
static int enable_irq_wake(int irq) { assert(irq==17);return wake_enable_error; }
static int disable_irq_wake(int irq) { assert(irq==17);return wake_disable_error; }
/* FUNCTIONS */
int main(void) {
 struct nfc_dev d={.configs.ldo={.vdd_levels={NFC_VDDIO_MIN,NFC_VDDIO_MAX},.max_current=157000}};
 struct device dev={0};unsigned tests=0;
 supply=false;assert(!nfc_ldo_config(&dev,&d)&&!d.reg);supply=true;tests++;
 for(int f=1;f<=4;f++) {
  step=0;fail_step=f;assert(nfc_ldo_config(&dev,&d)==-EIO);assert(!d.reg&&!d.is_vreg_enabled&&!handles&&!enabled&&!voltage&&!load);tests++;
 }
 step=0;fail_step=0;assert(!nfc_ldo_config(&dev,&d));assert(d.is_vreg_enabled&&enabled==1&&voltage==NFC_VDDIO_MIN&&load==157000);
 step=0;fail_step=1;assert(nfc_ldo_unvote(&d)==-EIO);assert(d.is_vreg_enabled&&enabled==1&&voltage&&load);tests++;
 step=0;fail_step=2;assert(nfc_ldo_unvote(&d)==-EIO);assert(!d.is_vreg_enabled&&!enabled&&!load&&voltage);tests++;
 fail_step=0;assert(!regulator_set_voltage(d.reg,0,NFC_VDDIO_MAX));regulator_put(d.reg);d.reg=NULL;
 step=0;assert(!nfc_ldo_config(&dev,&d));step=0;fail_step=3;assert(nfc_ldo_unvote(&d)==-EIO);assert(!enabled&&!voltage&&load);tests++;
 fail_step=0;assert(!regulator_set_load(d.reg,0));regulator_put(d.reg);d.reg=NULL;
 step=0;assert(!nfc_ldo_config(&dev,&d));assert(!nfc_ldo_unvote(&d));regulator_put(d.reg);d.reg=NULL;assert(!handles&&!enabled&&!voltage&&!load);tests++;
 d.nfc_enable_intr=intr_enable;d.nfc_disable_intr=intr_disable;
 assert(!nfc_ioctl_power_states(&d,NFC_POWER_ON)&&ven==1&&d.nfc_ven_enabled);tests++;
 assert(!nfc_ioctl_power_states(&d,NFC_POWER_OFF)&&ven==0&&!d.nfc_ven_enabled&&wakeups==1);tests++;
 irq_enable_error=-ENODEV;assert(nfc_ioctl_power_states(&d,NFC_POWER_ON)==-ENODEV&&ven==0);tests++;
 assert(nfc_ioctl_power_states(&d,NFC_FW_DWL_VEN_TOGGLE)==-ENODEV);assert(nfc_ioctl_power_states(&d,NFC_VEN_FORCED_HARD_RESET)==-ENODEV);tests+=2;irq_enable_error=0;
 assert(!nfc_ioctl_power_states(&d,NFC_ENABLE)&&d.cold_reset.is_nfc_enabled);assert(!nfc_ioctl_power_states(&d,NFC_DISABLE)&&!d.cold_reset.is_nfc_enabled);tests+=2;
 struct i2c_client client={.irq=17,.data=&d,.dev.wakeup=true};client.dev.client=&client;d.i2c_dev.client=&client;d.i2c_dev.irq_enabled=true;
 wake_enable_error=-EIO;assert(nfc_i2c_dev_suspend(&client.dev)==-EIO&&!d.i2c_dev.irq_wake_up);tests++;
 wake_enable_error=0;assert(!nfc_i2c_dev_suspend(&client.dev)&&d.i2c_dev.irq_wake_up);tests++;
 wake_disable_error=-EIO;assert(nfc_i2c_dev_resume(&client.dev)==-EIO&&d.i2c_dev.irq_wake_up);tests++;
 wake_disable_error=0;assert(!nfc_i2c_dev_resume(&client.dev)&&!d.i2c_dev.irq_wake_up);tests++;
 printf("{\"regulator_power_and_wake_fault_cases\":%u,\"power_off_wakes_reader\":true}\n",tests);
}
