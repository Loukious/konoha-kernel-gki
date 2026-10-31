#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
typedef uint32_t u32;
struct mutex { pthread_mutex_t value; };
typedef pthread_mutex_t spinlock_t;
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->value,NULL));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->value));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->value));}
static struct mutex *take_guard(struct mutex *m){mutex_lock(m);return m;}
static void drop_guard(struct mutex **m){mutex_unlock(*m);}
#define guard(kind) struct mutex *__attribute__((cleanup(drop_guard))) held_guard=take_guard
#define scoped_guard(kind,m) for(struct mutex *__attribute__((cleanup(drop_guard))) held_guard=take_guard(m),*once=held_guard;once;once=NULL)
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define spin_lock_init(p) assert(!pthread_mutex_init(p,NULL))
#define spin_lock_irqsave(p,f) do{(f)=0;assert(!pthread_mutex_lock(p));}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);assert(!pthread_mutex_unlock(p));}while(0)
#define READ_ONCE(v) __atomic_load_n(&(v),__ATOMIC_ACQUIRE)
#define WRITE_ONCE(v,x) __atomic_store_n(&(v),(x),__ATOMIC_RELEASE)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ARRAY_SIZE(v) (sizeof(v)/sizeof((v)[0]))
#define BIT(v) (1U<<(v))
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(v) ((int)(intptr_t)(v))
#define ERR_CAST(v) ((void *)(v))
#define IS_ERR(v) ((uintptr_t)(v)>=(uintptr_t)-4095)
#define GFP_KERNEL 0
#define WQ_MEM_RECLAIM 0
#define PAGE_SIZE 4096
#define msecs_to_jiffies(v) (v)
#define IS_ENABLED(config) 0
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
static int notifier_from_errno(int ret){return ret?0x8000|(1-ret):1;}
static int notifier_to_errno(int ret){ret&=~0x8000;return ret>1?1-ret:0;}
#define mca_log_info(...) ((void)0)
#define mca_log_err(...) ((void)0)
#define check_mul_overflow(a,b,out) __builtin_mul_overflow(a,b,out)
#define scnprintf snprintf
/* ENUMS */
struct device {void *data;void *of_node;};
struct platform_device {struct device dev;};
struct work_struct {void(*fn)(struct work_struct *);bool pending;};
struct delayed_work {struct work_struct work;};
struct workqueue_struct {int id;};
struct wakeup_source {int id;};
struct notifier_block {int(*notifier_call)(struct notifier_block *,unsigned long,void *);};
struct power_supply;
struct device_attribute;
union power_supply_propval {int intval;};
struct power_supply_desc {const char *name;enum power_supply_type type;const enum power_supply_property *properties;size_t num_properties;int(*get_property)(struct power_supply *,enum power_supply_property,union power_supply_propval *);int(*set_property)(struct power_supply *,enum power_supply_property,const union power_supply_propval *);int(*property_is_writeable)(struct power_supply *,enum power_supply_property);};
struct power_supply {void *data;const struct power_supply_desc *desc;};
struct power_supply_config {void *fwnode;void *drv_data;};
static void *dev_fwnode(struct device *d){return d->of_node;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static void *power_supply_get_drvdata(struct power_supply *p){return p->data;}
static void platform_set_drvdata(struct platform_device *d,void *p){d->dev.data=p;}
static void *platform_get_drvdata(struct platform_device *d){return d->dev.data;}
static int kstrtoint(const char *s,int base,int *out){char *end;errno=0;long value=strtol(s,&end,base);if(errno||s==end||(*end&&strcmp(end,"\n"))||value<INT_MIN||value>INT_MAX)return -EINVAL;*out=value;return 0;}
static char *strim(char *s){while(isspace(*s))s++;char *e=s+strlen(s);while(e>s&&isspace(e[-1]))*--e=0;return s;}
static char *kstrdup(const char *s,int f){(void)f;return strdup(s);}
#define kfree free
/* HEADERS */
/* DECLARATIONS */
