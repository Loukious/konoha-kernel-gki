#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdarg.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int64_t ktime_t;
#define BIT(x) (1U << (x))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define DIV_ROUND_CLOSEST(x,d) (((x)+(d)/2)/(d))
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define KTIME_MAX INT64_MAX
#define NSEC_PER_MSEC 1000000
#define U16_MAX UINT16_MAX
#define GFP_KERNEL 0
#define LED_DEV_CAP_FLASH 1
#define IRQF_ONESHOT 1
#define GPIOF_OUT_INIT_LOW 0
#define EPROBE_DEFER 517
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define EXPORT_SYMBOL(x)
struct mutex { pthread_mutex_t raw; };
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->raw,NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->raw)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->raw)); }
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
#define INIT_LIST_HEAD(p) (*(p)=(struct list_head){p,p})
static void list_add_tail(struct list_head *p,struct list_head *h) {p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
static void list_del_init(struct list_head *p) {p->next->prev=p->prev;p->prev->next=p->next;INIT_LIST_HEAD(p);}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
struct work_struct {void (*fn)(struct work_struct *);bool pending;};
static int atomic_context,work_runs,timer_cancels,work_cancels;
#define INIT_WORK(w,f) (*(w)=(struct work_struct){.fn=f})
static bool schedule_work(struct work_struct *w) {bool old=w->pending;w->pending=true;return !old;}
static bool flush_work(struct work_struct *w) {if(!w->pending)return false;w->pending=false;work_runs++;w->fn(w);return true;}
static bool cancel_work_sync(struct work_struct *w) {work_cancels++;bool old=w->pending;w->pending=false;return old;}
enum hrtimer_restart {HRTIMER_NORESTART};
struct hrtimer {enum hrtimer_restart (*fn)(struct hrtimer *);bool active;ktime_t delay;};
#define HRTIMER_MODE_REL 0
static void hrtimer_setup(struct hrtimer *t,enum hrtimer_restart (*f)(struct hrtimer *),int clk,int mode) {*t=(struct hrtimer){.fn=f};}
static void hrtimer_start(struct hrtimer *t,ktime_t delay,int mode) {assert(delay>=0);t->active=true;t->delay=delay;}
static void hrtimer_cancel(struct hrtimer *t) {timer_cancels++;t->active=false;}
static ktime_t ms_to_ktime(u64 ms) {assert(ms<=(u64)KTIME_MAX/NSEC_PER_MSEC);return ms*NSEC_PER_MSEC;}
static void fire_timer(struct hrtimer *t) {assert(t->active);t->active=false;atomic_context++;assert(t->fn(t)==HRTIMER_NORESTART);atomic_context--;}
struct device_node;
struct device {struct device *parent;struct device_node *of_node;void *data;};
struct platform_device {struct device dev;};
static void *dev_get_drvdata(struct device *d) {return d->data;}
static void platform_set_drvdata(struct platform_device *p,void *d) {p->dev.data=d;}
static void *platform_get_drvdata(struct platform_device *p) {return p->dev.data;}
struct attribute {int dummy;};
struct device_attribute {struct attribute attr;};
struct attribute_group {struct attribute **attrs;};
#define __ATTR(n,m,s,t) {.attr={0}}
enum led_brightness { LED_OFF=0, LED_FULL=255 };
struct led_trigger {int tag;};
struct led_classdev {const char *name,*default_trigger;u32 brightness,max_brightness,flags;int (*brightness_set_blocking)(struct led_classdev *,enum led_brightness);enum led_brightness (*brightness_get)(struct led_classdev *);struct led_trigger *trigger;struct device *dev;const struct attribute_group **groups;struct work_struct set_brightness_work;};
struct led_classdev_flash;
struct led_flash_ops {int (*flash_brightness_set)(struct led_classdev_flash *,u32);int (*flash_brightness_get)(struct led_classdev_flash *,u32 *);int (*strobe_set)(struct led_classdev_flash *,bool);int (*strobe_get)(struct led_classdev_flash *,bool *);int (*timeout_set)(struct led_classdev_flash *,u32);};
struct led_flash_setting {u32 min,max,step,val;};
struct led_classdev_flash {struct led_classdev led_cdev;struct led_flash_setting brightness,timeout;const struct led_flash_ops *ops;};
struct flash_led_param {u64 on_time_ms,off_time_ms;};
#define QUERY_MAX_AVAIL_CURRENT BIT(2)
struct power_supply {int refs;};
union power_supply_propval {int intval;};
#define POWER_SUPPLY_PROP_VOLTAGE_OCV 1
#define POWER_SUPPLY_PROP_CURRENT_NOW 2
#define BATTERY_RESISTANCE 3
static struct power_supply battery;
static bool has_battery;
static int battery_gets,battery_puts;
static struct power_supply *power_supply_get_by_name(const char *n) {battery_gets++;if(!has_battery)return NULL;battery.refs++;return &battery;}
static void power_supply_put(struct power_supply *p) {assert(p->refs>0);p->refs--;battery_puts++;}
static int qti_battery_charger_get_prop(const char *n,int id,int *value) {*value=100000;return -EINVAL;}
static int power_supply_get_property(struct power_supply *p,int id,union power_supply_propval *v) {assert(!"native battery stub must fail first");return -EINVAL;}
static int kstrtou64(const char *s,int base,u64 *out) {char *end;errno=0;if(*s=='-')return -EINVAL;unsigned long long n=strtoull(s,&end,base);if(errno||end==s||(*end && *end!='\n'))return -EINVAL;*out=n;return 0;}
static int sysfs_emit(char *s,const char *fmt,...) {va_list ap;va_start(ap,fmt);int n=vsnprintf(s,4096,fmt,ap);va_end(ap);return n;}
static void udelay(int n) {assert(!atomic_context);}
struct regmap {u8 data[65536];};
static int io_calls,fail_io,persistent_io,gpio_value[4];
static void (*io_hook)(void);
static int io_check(void) {assert(!atomic_context);if(io_hook)io_hook();io_calls++;return persistent_io || io_calls==fail_io ? -EIO:0;}
static __attribute__((noinline)) int regmap_bulk_read(struct regmap *r,unsigned addr,void *p,size_t n) {int rc=io_check();if(!rc)memcpy(p,&r->data[addr],n);return rc;}
static int regmap_bulk_write(struct regmap *r,unsigned addr,const void *p,size_t n) {int rc=io_check();if(!rc)memcpy(&r->data[addr],p,n);return rc;}
static int regmap_update_bits(struct regmap *r,unsigned addr,unsigned mask,unsigned val) {int rc=io_check();if(!rc)r->data[addr]=(r->data[addr]&~mask)|(val&mask);return rc;}
static bool gpio_is_valid(int n) {return n>=0;}
static void gpio_set_value_cansleep(int n,int v) {assert(!atomic_context);assert(n<4);gpio_value[n]=v;}
typedef int irqreturn_t;
#define IRQ_HANDLED 1
static int (*irq_request_hook)(irqreturn_t (*thread)(int,void *),void *data);
static int devm_request_threaded_irq(struct device *d,int irq,void *top,irqreturn_t (*thread)(int,void *),int flags,const char *name,void *data) {return irq_request_hook?irq_request_hook(thread,data):0;}
/* CONSTANTS AND TYPES */
