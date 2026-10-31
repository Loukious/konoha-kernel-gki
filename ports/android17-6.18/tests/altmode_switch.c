#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define GFP_KERNEL 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define NOTIFY_BAD 0x8002
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define EPROBE_DEFER 517
#define spin_lock_irqsave(p,f) ((void)(p),(f)=0)
#define spin_unlock_irqrestore(p,f) ((void)(p),(void)(f))
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
struct mutex {bool held;};
static void mutex_lock(struct mutex *m) {assert(!m->held);m->held=true;}
static void mutex_unlock(struct mutex *m) {assert(m->held);m->held=false;}
struct atomic_t {int value;};
static void atomic_set(struct atomic_t *p,int v) {p->value=v;}
static int atomic_read(struct atomic_t *p) {return p->value;}
enum typec_accessory {TYPEC_ACCESSORY_NONE,TYPEC_ACCESSORY_AUDIO,TYPEC_ACCESSORY_DEBUG};
enum fsa_function {FSA_MIC_GND_SWAP,FSA_USBC_ORIENTATION_CC1,FSA_USBC_ORIENTATION_CC2,FSA_USBC_DISPLAYPORT_DISCONNECTED,FSA_USBC_SBU_LPD_SENSE,FSA_USBC_SBU_LPD_ISOLATE,FSA_EVENT_MAX};
struct ucsi_glink_constat_info {enum typec_accessory acc;};
struct notifier_block {int (*notifier_call)(struct notifier_block*,unsigned long,void*);};
struct blocking_notifier_head {int unused;};
struct work_struct {bool pending;void (*fn)(struct work_struct *);};
struct list_head {bool linked;};
static void INIT_LIST_HEAD(struct list_head *h) {h->linked=false;}
static void list_add_tail(struct list_head *n,struct list_head *h) {(void)h;n->linked=true;}
static void list_del_init(struct list_head *n) {assert(n->linked);n->linked=false;}
struct device_node {int unused;};
struct device {struct device_node *of_node;bool awake,wakeup_enabled;};
struct regmap {u32 regs[0x52];unsigned calls,fail,writes,delays;u32 write_reg[64],write_val[64];};
struct fsa4480_priv {struct list_head registry_node;struct regmap *regmap;struct device *dev;struct notifier_block ucsi_nb;struct atomic_t usbc_mode;struct work_struct usbc_analog_work;struct blocking_notifier_head fsa4480_notifier;struct mutex notification_lock;int work_lock;bool work_active,removing;int last_error;};
struct fsa4480_reg_val {u16 reg;u8 val;};
static struct fsa4480_priv *provider;
static struct regmap *current;
static int srcu_count,fsa4480_ops_srcu,notified,reentrant,injection;
static void *system_freezable_wq;
static int fsa4480_usbc_event_changed(struct notifier_block*,unsigned long,void*);
int fsa4480_switch_event(struct device_node*,enum fsa_function);
static int regmap_read(struct regmap *r,unsigned reg,u32 *out) {assert(reg<ARRAY_SIZE(r->regs));if(++r->calls==r->fail)return -EIO;*out=r->regs[reg];return 0;}
static int regmap_write(struct regmap *r,unsigned reg,unsigned val) {assert(reg<ARRAY_SIZE(r->regs));if(++r->calls==r->fail)return -EIO;assert(r->writes<64);r->write_reg[r->writes]=reg;r->write_val[r->writes++]=val;r->regs[reg]=val;if(reg==4&&val==0xf8)r->regs[7]=r->regs[5]==0x18?0x23:0x1c;if(injection){injection=0;struct ucsi_glink_constat_info info={TYPEC_ACCESSORY_NONE};fsa4480_usbc_event_changed(&provider->ucsi_nb,0,&info);}return 0;}
static void usleep_range(int lo,int hi) {assert(lo==50&&hi==55);current->delays++;}
static int srcu_read_lock(int *s) {(void)s;srcu_count++;return 1;}
static void srcu_read_unlock(int *s,int idx) {(void)s;assert(idx==1&&srcu_count>0);srcu_count--;}
static struct fsa4480_priv *fsa4480_find(struct device_node *n) {assert(srcu_count>0);return provider&&!provider->removing&&provider->dev->of_node==n?provider:NULL;}
static bool queue_work(void *q,struct work_struct *w) {(void)q;bool old=w->pending;w->pending=true;return !old;}
static void pm_stay_awake(struct device *d) {assert(!d->awake);d->awake=true;}
static void pm_relax(struct device *d) {d->awake=false;}
static int blocking_notifier_call_chain(struct blocking_notifier_head *h,int mode,void *data) {(void)h;(void)data;assert(!provider->notification_lock.held);notified++;if(reentrant){reentrant=0;assert(!fsa4480_switch_event(provider->dev->of_node,mode==TYPEC_ACCESSORY_AUDIO?FSA_MIC_GND_SWAP:FSA_USBC_DISPLAYPORT_DISCONNECTED));}return 0;}
static struct mutex fsa4480_registry_lock;
static struct list_head fsa4480_devices;
static struct {int unused;} fsa4480_regmap_config;
static unsigned probe_step,probe_fault;
static struct fsa4480_priv *probe_allocation;
static bool ucsi_registered;
struct i2c_client {struct device dev;struct fsa4480_priv *priv;};
#define mutex_init(m) ((m)->held=false)
#define spin_lock_init(p) (*(p)=0)
#define INIT_WORK(w,f) ((w)->fn=(f))
#define BLOCKING_INIT_NOTIFIER_HEAD(h) ((void)(h))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
static bool probe_failure(void) {return ++probe_step==probe_fault;}
static void *devm_kzalloc(struct device *d,size_t n,int gfp) {(void)d;(void)gfp;if(probe_failure())return NULL;probe_allocation=calloc(1,n);provider=probe_allocation;return probe_allocation;}
static struct regmap *devm_regmap_init_i2c(struct i2c_client *c,const void *cfg) {(void)c;(void)cfg;if(probe_failure())return ERR_PTR(-ENOMEM);return current;}
static int devm_regmap_qti_debugfs_register(struct device *d,struct regmap *r) {(void)d;(void)r;return probe_failure()?-ENOMEM:0;}
static int device_init_wakeup(struct device *d,bool enabled) {if(enabled&&probe_failure())return -ENOMEM;d->wakeup_enabled=enabled;return 0;}
static int register_ucsi_glink_notifier(struct notifier_block *nb) {struct ucsi_glink_constat_info info={TYPEC_ACCESSORY_AUDIO};fsa4480_usbc_event_changed(nb,0,&info);if(probe_failure())return -EIO;ucsi_registered=true;return 0;}
static void unregister_ucsi_glink_notifier(struct notifier_block *nb) {(void)nb;assert(ucsi_registered);ucsi_registered=false;}
static void cancel_work_sync(struct work_struct *w) {w->pending=false;}
static void synchronize_srcu(int *s) {(void)s;assert(!srcu_count);}
static void i2c_set_clientdata(struct i2c_client *c,struct fsa4480_priv *p) {c->priv=p;}
static struct fsa4480_priv *i2c_get_clientdata(struct i2c_client *c) {return c->priv;}
/* TABLES */
/* FUNCTIONS */
static void init(struct fsa4480_priv *p,struct regmap *r,struct device *d) {memset(p,0,sizeof(*p));memset(r,0,sizeof(*r));memset(d,0,sizeof(*d));static struct device_node n;d->of_node=&n;p->dev=d;p->regmap=r;provider=p;current=r;r->regs[5]=0x18;r->regs[4]=0x98;}
int main(void) {
 struct fsa4480_priv p;struct regmap r;struct device d;int cases=0;
 for(int chip=0;chip<2;chip++)for(unsigned fail=0;fail<=11;fail++){init(&p,&r,&d);r.regs[0]=chip?0xf6:0;r.fail=fail;int rc=fsa4480_update_reg_defaults(&r);assert(rc==(fail?-EIO:0));if(!fail){const struct fsa4480_reg_val *table=chip?dio_reg_i2c_defaults:fsa_reg_i2c_defaults;assert(r.writes==10);for(int i=0;i<10;i++){assert(r.write_reg[i]==table[i].reg&&r.write_val[i]==table[i].val);}}cases++;}
 const unsigned targets[][2]={{0,0x9f},{7,0x9f},{0x18,0xf8},{0x78,0xf8},{0x19,0xd9},{0x78,0xd9}};
 for(size_t target=0;target<ARRAY_SIZE(targets);target++)for(unsigned fail=0;fail<=5;fail++){init(&p,&r,&d);r.fail=fail;int rc=fsa4480_usbc_update_settings(&p,targets[target][0],targets[target][1]);assert(rc==(fail?-EIO:0));assert(r.writes==(fail>2?fail-3:fail?0:3));if(!fail){assert(r.regs[5]==targets[target][0]&&r.regs[4]==targets[target][1]&&r.delays==1);}cases++;}
 for(int event=0;event<FSA_EVENT_MAX;event++)for(unsigned fail=0;fail<=7;fail++){init(&p,&r,&d);r.fail=fail;int rc=fsa4480_switch_event(d.of_node,event);assert(!srcu_count&&!p.notification_lock.held);assert(rc==0||rc==-EIO);if(!fail)assert(!rc);cases++;}
 init(&p,&r,&d);assert(fsa4480_switch_event(d.of_node,FSA_EVENT_MAX)==-EINVAL);p.removing=true;assert(fsa4480_switch_event(d.of_node,FSA_MIC_GND_SWAP)==-ENODEV&&!srcu_count);p.removing=false;
 struct ucsi_glink_constat_info info={TYPEC_ACCESSORY_AUDIO};assert(fsa4480_usbc_event_changed(&p.ucsi_nb,0,NULL)==NOTIFY_BAD);assert(fsa4480_usbc_event_changed(&p.ucsi_nb,0,&info)==NOTIFY_OK&&p.work_active&&d.awake);p.usbc_analog_work.pending=false;injection=1;reentrant=1;fsa4480_usbc_analog_work_fn(&p.usbc_analog_work);assert(!p.work_active&&!d.awake&&!p.last_error&&r.regs[4]==0x98&&atomic_read(&p.usbc_mode)==TYPEC_ACCESSORY_NONE);
 info.acc=TYPEC_ACCESSORY_AUDIO;assert(fsa4480_usbc_event_changed(&p.ucsi_nb,0,&info)==NOTIFY_OK);r.calls=0;r.fail=1;p.usbc_analog_work.pending=false;fsa4480_usbc_analog_work_fn(&p.usbc_analog_work);assert(p.last_error==-EIO&&!p.work_active&&!d.awake);r.fail=0;r.calls=0;assert(fsa4480_usbc_event_changed(&p.ucsi_nb,0,&info)==NOTIFY_OK&&p.work_active);p.usbc_analog_work.pending=false;fsa4480_usbc_analog_work_fn(&p.usbc_analog_work);assert(!p.last_error&&!p.work_active&&!d.awake&&r.regs[4]==0x9f);
 for(unsigned fault=0;fault<=5;fault++) {struct i2c_client client={0};static struct device_node node;client.dev.of_node=&node;memset(&r,0,sizeof(r));current=&r;provider=probe_allocation=NULL;probe_step=0;probe_fault=fault;int rc=fsa4480_probe(&client);assert(rc==(fault?fault==5?-EIO:-ENOMEM:0));if(!fault){assert(ucsi_registered&&client.priv->registry_node.linked&&client.dev.awake);fsa4480_remove(&client);assert(!client.priv&&!ucsi_registered&&!client.dev.awake&&!client.dev.wakeup_enabled);}else{assert(!ucsi_registered&&!client.dev.awake&&!client.dev.wakeup_enabled&&!client.priv);if(probe_allocation)assert(!probe_allocation->usbc_analog_work.pending&&!probe_allocation->registry_node.linked);}free(probe_allocation);provider=probe_allocation=NULL;}
 printf("{\"regmap_table_sequence_and_fault_cases\":%d,\"probe_allocation_and_unwind_cases\":6,\"reentrant_notifier_and_mode_change\":true,\"wake_and_error_retry_balance\":true}\n",cases);
}
