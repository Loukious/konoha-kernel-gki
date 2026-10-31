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
#define dev_err(...) ((void)errors++)
#define dev_err_ratelimited(...) ((void)errors++)
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
struct device {int pm_refs;struct regmap *regmap;struct device *parent;struct device_node *of_node;void *data,*platform_data;};
struct platform_device {struct device dev;bool registered;};
struct notifier_block {int unused;};struct blocking_notifier_head {int unused;};
struct clk {int refs,enables,disables;};
struct regmap {unsigned int values[65536];int reads,writes;bool cache_only;};
struct snd_soc_dai_driver {int id;};
struct snd_info_entry {int unused;};
struct snd_soc_card {struct device *dev;};
struct snd_soc_dapm_context {struct snd_soc_component *component;struct snd_soc_card *card;};
struct snd_soc_dapm_widget {struct snd_soc_dapm_context *dapm;unsigned int shift;const char *name;};
struct snd_kcontrol {unsigned long private_value;struct snd_soc_dapm_widget *widget;};
struct soc_mixer_control {unsigned int shift;};
struct snd_soc_dapm_update {int unused;};
struct snd_ctl_elem_value {struct {struct {long value[4];} integer;} value;};
#define test_bit(i,p) ((*(p))&(1UL<<(i)))
#define set_bit(i,p) ((*(p))|=(1UL<<(i)))
#define clear_bit(i,p) ((*(p))&=~(1UL<<(i)))
struct snd_soc_dai {struct snd_soc_component *component;int id;const char *name;};
struct snd_pcm_substream {int unused;};
struct snd_pcm_hw_params {int rate,channels;};
struct snd_soc_component {struct device *dev;struct snd_soc_card *card;};
struct snd_soc_component_driver {int unused;};
struct proc_dir_entry {int unused;};
typedef int (*rsc_clk_cb_t)(struct device*,u16);
static int fault,fault2,steps,live,errors;
static bool fail(void){++steps;return steps==fault||steps==fault2;}
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



#define SND_SOC_DAPM_PRE_PMU 1
#define SND_SOC_DAPM_POST_PMU 2
#define SND_SOC_DAPM_PRE_PMD 4
#define SND_SOC_DAPM_POST_PMD 8
#define max(a,b) ((a)>(b)?(a):(b))
#define msecs_to_jiffies(n) (n)
#define system_freezable_wq NULL
#define to_delayed_work(p) container_of(p,struct delayed_work,work)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef int irqreturn_t;
struct work_struct {int unused;};
struct delayed_work {struct work_struct work;bool pending;int delay;};
static int awake;
static bool queue_delayed_work(void*q,struct delayed_work*w,int d){if(w->pending)return false;w->pending=true;w->delay=d;return true;}
static bool cancel_delayed_work_sync(struct delayed_work*w){bool had=w->pending;w->pending=false;return had;}
static void pm_stay_awake(struct device*d){awake++;}
static void pm_relax(struct device*d){assert(awake-->0);}
static struct regmap *dev_get_regmap(struct device*d,void*n){return d->regmap;}
static int pm_runtime_resume_and_get(struct device*d){if(fail())return -EIO;d->pm_refs++;return 0;}
static void pm_runtime_put_autosuspend(struct device*d){assert(d->pm_refs-->0);}
static void pm_runtime_mark_last_busy(struct device*d){}
static bool votes_ready=true;
static bool lpass_cdc_check_core_votes(struct device*d){return votes_ready;}
static int clock_refs[MAX_CLK],fs_refs;
static int lpass_cdc_clk_rsc_request_clock(struct device*d,int def,int id,bool en){assert(def>=0&&def<8&&id>=0&&id<8);if(fail())return -EIO;if(en)clock_refs[id]++;else assert(clock_refs[id]-->0);return 0;}
static void lpass_cdc_clk_rsc_fs_gen_request(struct device*d,bool en){if(en)fs_refs++;else assert(fs_refs-->0);}
static void regcache_mark_dirty(struct regmap*m){}
static int regcache_sync_region(struct regmap*m,int a,int b){return fail()?-EIO:0;}
static int snd_soc_component_update_bits(struct snd_soc_component*c,unsigned int r,unsigned int m,unsigned int v){return regmap_update_bits(c->dev->regmap,r,m,v);}
static struct snd_soc_component *snd_soc_dapm_to_component(struct snd_soc_dapm_context*d){return d->component;}
static int pin_active,wake_capable;
static int msm_cdc_pinctrl_select_active_state(struct device_node*n){if(fail())return -EIO;pin_active=1;return 0;}
static int msm_cdc_pinctrl_select_sleep_state(struct device_node*n){if(fail())return -EIO;pin_active=0;return 0;}
static int msm_cdc_pinctrl_set_wakeup_capable(struct device_node*n,bool en){if(fail())return -EIO;wake_capable=en;return 0;}
static int params_rate(struct snd_pcm_hw_params*p){return p->rate;}
static int params_channels(struct snd_pcm_hw_params*p){return p->channels;}
#define for_each_set_bit(i,p,n) for(i=0;i<(n);i++)if((*(p))&(1UL<<i))
struct regulator {int voltage,load,enabled;};
static int regulator_set_voltage(struct regulator*r,int lo,int hi){if(fail())return -EIO;r->voltage=lo;return 0;}
static int regulator_set_load(struct regulator*r,int load){if(fail())return -EIO;r->load=load;return 0;}
static int regulator_enable(struct regulator*r){if(fail())return -EIO;r->enabled++;return 0;}
static int regulator_disable(struct regulator*r){if(fail())return -EIO;assert(r->enabled-->0);return 0;}
/* PUBLIC_LAYOUT */
/* VA_LAYOUT */
static void init_va(struct lpass_cdc_va_macro_priv*p,struct device*d){memset(p,0,sizeof(*p));p->dev=d;d->data=p;p->pre_dev_up=true;p->default_clk_id=0;p->clk_id=3;p->reset_swr=true;mutex_init(&p->wlock);mutex_init(&p->mclk_lock);mutex_init(&p->swr_clk_lock);mutex_init(&p->child_lock);mutex_init(&p->core_vote_lock);init_waitqueue_head(&p->child_wait);}
static struct device *macro_dev;
static struct device *lpass_cdc_get_device_ptr(struct device*d,int id){assert(id==VA_MACRO);return macro_dev;}

static int dmic_refs[8];
static int lpass_cdc_dmic_clk_enable(struct snd_soc_component*c,int id,int type,bool enable){assert(c&&id>=0&&id<8);if(fail())return -EIO;if(enable)dmic_refs[id]++;else assert(dmic_refs[id]-->0);return 0;}
static unsigned int snd_soc_component_read(struct snd_soc_component*c,unsigned int reg){assert(c&&c->dev->regmap);return c->dev->regmap->values[reg];}
static int snd_soc_component_write(struct snd_soc_component*c,unsigned int reg,unsigned int value){return regmap_write(c->dev->regmap,reg,value);}
static void usleep_range(unsigned int min,unsigned int max){assert(min<=max);}
static void lpass_cdc_clear_amic_tx_hold(struct device*d,int id){}
