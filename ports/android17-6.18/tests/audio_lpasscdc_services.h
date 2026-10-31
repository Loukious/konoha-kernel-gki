#include <assert.h>
#include <stddef.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#define __iomem
#define __user
#define GFP_KERNEL 0
#define U16_MAX UINT16_MAX
#define U8_MAX UINT8_MAX
#define ENOTSYNC 522
#define EPROBE_DEFER 517
#define BIT(n) (1u<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define WARN_ON(x) ({bool _v=(x);assert(!_v);_v;})
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_dbg_ratelimited(...) ((void)0)
#define LPASS_CDC_CHILD_DEVICES_MAX 6
#define PLATFORM_DEVID_NONE -1
#define LPASS_CDC_AUTO_SUSPEND_DELAY 100
#define TX_CORE_CLK 0
#define RX_CORE_CLK 1
#define WSA_CORE_CLK 2
#define VA_CORE_CLK 3
#define WSA2_CORE_CLK 4
#define RX_TX_CORE_CLK 5
#define WSA_TX_CORE_CLK 6
#define WSA2_TX_CORE_CLK 7
#define MAX_CLK 8
#define REG_BYTES 2
#define VAL_BYTES 1
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
struct mutex { pthread_mutex_t lock; };
static void mutex_init(struct mutex *m){pthread_mutex_init(&m->lock,NULL);}
static void mutex_lock(struct mutex*m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex*m){assert(!pthread_mutex_unlock(&m->lock));}
typedef atomic_int atomic_t;
#define atomic_set(p,v) atomic_store(p,v)
#define atomic_read(p) atomic_load(p)
#define atomic_inc(p) atomic_fetch_add(p,1)
#define atomic_dec_and_test(p) (atomic_fetch_sub(p,1)==1)
typedef struct {pthread_mutex_t lock;pthread_cond_t cond;} wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t*w){pthread_mutex_init(&w->lock,NULL);pthread_cond_init(&w->cond,NULL);}
static void wake_up_all(wait_queue_head_t*w){pthread_mutex_lock(&w->lock);pthread_cond_broadcast(&w->cond);pthread_mutex_unlock(&w->lock);}
#define wait_event(w,p) do{pthread_mutex_lock(&(w).lock);while(!(p))pthread_cond_wait(&(w).cond,&(w).lock);pthread_mutex_unlock(&(w).lock);}while(0)
struct device_node {const char *name,*compatible;bool split;int refs;struct device_node *child,*next;};
struct device {struct device *parent;struct device_node *of_node;void *data,*platform_data;};
struct platform_device {struct device dev;bool registered;};
struct notifier_block {int unused;};struct blocking_notifier_head {int unused;};
struct clk {int refs,enables,disables;};
struct regmap {unsigned int values[65536];int reads,writes;bool cache_only;};
struct snd_soc_dai_driver {int id;};
struct snd_info_entry {int unused;};
struct snd_soc_card {struct device *dev;};
struct snd_soc_component {struct device *dev;struct snd_soc_card *card;};
struct snd_soc_component_driver {int unused;};
struct proc_dir_entry {int unused;};
typedef int (*rsc_clk_cb_t)(struct device*,u16);
static int fault,steps,live;
static bool fail(void){return ++steps==fault;}
static void *alloc(size_t size){if(fail())return NULL;void*p=calloc(1,size?size:1);if(p)live++;return p;}
#define kzalloc(n,g) alloc(n)
#define kcalloc(n,s,g) alloc((n)*(s))
static void kfree(void*p){if(p){live--;free(p);}}
static void *dev_get_drvdata(struct device*d){return d?d->data:NULL;}
static void *snd_soc_component_get_drvdata(struct snd_soc_component*c){return c?dev_get_drvdata(c->dev):NULL;}
static bool of_device_is_compatible(struct device_node*n,const char*c){return n&&n->compatible&&!strcmp(n->compatible,c);}
static bool of_property_read_bool(struct device_node*n,const char*p){return n&&n->split&&!strcmp(p,"qcom,split-codec");}
static void *of_find_property(struct device_node*n,const char*p,void*len){return of_property_read_bool(n,p)?n:NULL;}
static struct device_node *of_node_get(struct device_node*n){if(n)n->refs++;return n;}
static void of_node_put(struct device_node*n){if(n)assert(n->refs-->0);}
static struct device_node *next_node(struct device_node*n){struct device_node*r=n->next;of_node_put(n);return of_node_get(r);}
#define for_each_available_child_of_node(p,n) for(n=of_node_get((p)->child);n;n=next_node(n))
static struct clk *disabled[64];static int nr_disabled;
static int clk_prepare_enable(struct clk*c){assert(c);if(fail())return -EIO;c->refs++;c->enables++;return 0;}
static void clk_disable_unprepare(struct clk*c){assert(c&&c->refs>0);c->refs--;c->disables++;if(nr_disabled<64)disabled[nr_disabled++]=c;}
static int digital_cdc_rsc_mgr_hw_vote_enable(struct clk*c,struct device*d){return clk_prepare_enable(c);}
static void digital_cdc_rsc_mgr_hw_vote_disable(struct clk*c,struct device*d){clk_disable_unprepare(c);}
static void pm_runtime_set_autosuspend_delay(struct device*d,int n){assert(n==100);}
static u32 ioread32(void*p){return *(u32*)p;}
static void iowrite32(u32 n,void*p){*(u32*)p=n;}
static u16 read16(const void*p){u16 n;memcpy(&n,p,2);return n;}
#define get_unaligned(p) read16(p)
static int regmap_read(struct regmap*m,unsigned int reg,unsigned int*v){assert(reg<=65535);m->reads++;if(fail())return -EIO;*v=m->values[reg];return 0;}
static int regmap_update_bits(struct regmap*m,unsigned int reg,unsigned int mask,unsigned int val){assert(reg<=65535);m->writes++;if(fail())return -EIO;m->values[reg]=(m->values[reg]&~mask)|(val&mask);return 0;}
static int regmap_write(struct regmap*m,unsigned int reg,unsigned int val){return regmap_update_bits(m,reg,255,val);}
static int snd_soc_component_write(struct snd_soc_component*c,unsigned int r,unsigned int v);
static int snd_soc_component_update_bits(struct snd_soc_component*c,unsigned int r,unsigned int m,unsigned int v);
/* PUBLIC_LAYOUT */
/* PRIVATE_LAYOUT */
static void init_priv(struct lpass_cdc_priv*p,struct device*d){memset(p,0,sizeof(*p));p->dev=d;d->data=p;mutex_init(&p->op_lock);mutex_init(&p->macro_lock);mutex_init(&p->io_lock);mutex_init(&p->clk_lock);mutex_init(&p->vote_lock);mutex_init(&p->dmic_lock);init_waitqueue_head(&p->op_wait);p->dev_up=true;}
static int snd_soc_component_write(struct snd_soc_component*c,unsigned int r,unsigned int v){struct lpass_cdc_priv*p=c->dev->data;return regmap_write(p->regmap,r,v);}
static int snd_soc_component_update_bits(struct snd_soc_component*c,unsigned int r,unsigned int m,unsigned int v){struct lpass_cdc_priv*p=c->dev->data;return regmap_update_bits(p->regmap,r,m,v);}
/* GATES */
