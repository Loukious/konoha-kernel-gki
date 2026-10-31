#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#define CONFIG_DEBUG_FS 1
#define CONFIG_SOUNDWIRE_MSTR_CTRL 1
#define IS_ENABLED(x) (x)
#define __iomem
#define GFP_KERNEL 0
#define PM_QOS_DEFAULT_VALUE -1
#define IRQ_HANDLED 1
#define IRQ_NONE 0
#define IRQF_TRIGGER_RISING 1
#define IRQF_ONESHOT 2
#define IRQF_TRIGGER_HIGH 4
#define S_IFREG 0100000
#define EPROBE_DEFER 517
#define BIT(x) (1UL << (x))
static void set_bit(unsigned n,unsigned long *p){*p|=1UL<<n;}
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define module_param(...)
#define MODULE_PARM_DESC(...)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef uint32_t u32,__u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uint64_t u64;
typedef unsigned long kernel_ulong_t;
typedef int pm_message_t,irqreturn_t;
typedef struct {atomic_int value;} atomic_t;
static int atomic_read(atomic_t *a){return atomic_load(&a->value);}
static void atomic_inc(atomic_t *a){atomic_fetch_add(&a->value,1);}
static bool atomic_dec_and_test(atomic_t *a){return atomic_fetch_sub(&a->value,1)==1;}
static void atomic_set(atomic_t *a,int n){atomic_store(&a->value,n);}
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));}
static void mutex_destroy(struct mutex *m){assert(!pthread_mutex_destroy(&m->lock));}
static void mutex_lock(struct mutex *m){assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
typedef struct {pthread_mutex_t lock;} spinlock_t;
#define spin_lock_init(s) assert(!pthread_mutex_init(&(s)->lock,NULL))
#define spin_lock_irqsave(s,f) ((f)=0,assert(!pthread_mutex_lock(&(s)->lock)))
#define spin_unlock_irqrestore(s,f) ((void)(f),assert(!pthread_mutex_unlock(&(s)->lock)))
typedef struct {pthread_mutex_t lock;pthread_cond_t changed;} wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t *w){assert(!pthread_mutex_init(&w->lock,NULL)&&!pthread_cond_init(&w->changed,NULL));}
static void wake_up_all(wait_queue_head_t *w){pthread_mutex_lock(&w->lock);pthread_cond_broadcast(&w->changed);pthread_mutex_unlock(&w->lock);}
#define wait_event(w,c) do {pthread_mutex_lock(&(w).lock);while(!(c))pthread_cond_wait(&(w).changed,&(w).lock);pthread_mutex_unlock(&(w).lock);}while(0)
struct completion {atomic_int done;};
static void init_completion(struct completion *c){atomic_store(&c->done,0);}
static void complete(struct completion *c){atomic_store(&c->done,1);}
static void wait_for_completion(struct completion *c){while(!atomic_load(&c->done))usleep(100);}
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *p){p->next=p->prev=p;}
static bool list_empty(struct list_head *p){return p->next==p;}
static void list_add(struct list_head *p,struct list_head *h){p->prev=h;p->next=h->next;h->next->prev=p;h->next=p;}
static void list_del(struct list_head *p){p->next->prev=p->prev;p->prev->next=p->next;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) for(p=container_of((h)->next,__typeof__(*p),m),n=container_of(p->m.next,__typeof__(*p),m);&p->m!=(h);p=n,n=container_of(n->m.next,__typeof__(*n),m))
static atomic_int live;
static int allocation_steps,allocation_fail;
static void *zalloc(size_t n){if(allocation_fail&&++allocation_steps==allocation_fail)return NULL;void*p=calloc(1,n);if(p)atomic_fetch_add(&live,1);return p;}
#define kzalloc(n,g) zalloc(n)
#define kcalloc(n,s,g) zalloc((n)*(s))
static void kfree(const void *p){if(p){atomic_fetch_sub(&live,1);free((void*)p);}}
struct kref {atomic_int refs;};
static void kref_init(struct kref *r){atomic_store(&r->refs,1);}
static void kref_get(struct kref *r){assert(atomic_fetch_add(&r->refs,1)>0);}
static void kref_put(struct kref *r,void (*f)(struct kref*)){if(atomic_fetch_sub(&r->refs,1)==1)f(r);}
struct device_node {int unused;};
struct device {void *data;void *platform_data;struct device_node *of_node;struct device *parent;};
struct platform_device {struct device dev;};
struct device_driver {int unused;};
struct bus_type;struct regmap;struct regmap_config;struct lock_class_key;struct irq_domain;
struct work_struct {bool initialized;void (*fn)(struct work_struct*);};
struct notifier_block {int (*notifier_call)(struct notifier_block*,unsigned long,void*);};
struct resource;struct clk {int unused;};struct pm_qos_request {bool active;};
struct dentry;struct proc_dir_entry;struct file_operations {int unused;};struct proc_ops {int unused;};
static void INIT_WORK(struct work_struct *w,void (*f)(struct work_struct*)){w->initialized=true;w->fn=f;}
static int cancel_count;
static void cancel_work_sync(struct work_struct *w){assert(w->initialized);cancel_count++;}
static void *platform_get_drvdata(struct platform_device*p){return p->dev.data;}
static void platform_set_drvdata(struct platform_device*p,void *v){p->dev.data=v;}
static void *dev_get_drvdata(const struct device*d){return d->data;}
static void dev_set_drvdata(struct device*d,void*p){d->data=p;}
static void *dev_get_platdata(struct device*d){return d->platform_data;}
static void *managed[1000];static unsigned managed_count;
static void *devm_kcalloc(struct device*d,size_t n,size_t z,int g){(void)d;(void)g;void*p=zalloc(n*z);if(p)managed[managed_count++]=p;return p;}
static void *devm_kzalloc(struct device*d,size_t n,int g){return devm_kcalloc(d,1,n,g);}
static void *devm_kmemdup(struct device*d,const void*p,size_t n,int g){void*q=devm_kzalloc(d,n,g);if(q)memcpy(q,p,n);return q;}
static void devm_kfree(struct device*d,void*p){(void)d;if(!p)return;for(unsigned i=0;i<managed_count;i++)if(managed[i]==p){managed[i]=NULL;kfree(p);return;}assert(false);}
static void devres_release(void){for(unsigned i=0;i<managed_count;i++)kfree(managed[i]);managed_count=0;}
/* HEADERS */
/* CONSTANTS */
static int qos_live,irq_live,wake_live,master_live,pm_live,children;
static int probe_fail,read_error,write_error,pm_error,io_count,responses,last_tid,pm_refs;
static bool fifo_bad,hold_master;
static atomic_bool stop_done,notified,notify_release,unregistered;
static struct swr_master *pending_master;
static u32 last_wire,fifo_sequence;
static int swrm_update_bus_clk(struct swr_mstr_ctrl *s){(void)s;return 0;}
static int pm_runtime_resume_and_get(struct device*d){(void)d;if(pm_error)return pm_error;pm_refs++;return 0;}
static void pm_runtime_put_autosuspend(struct device*d){(void)d;assert(pm_refs>0);pm_refs--;}
static void pm_runtime_mark_last_busy(struct device*d){(void)d;}
static int swrm_runtime_resume(struct device*d){(void)d;return 0;}
static u8 get_cmd_id(struct swr_mstr_ctrl*s){return ++s->cmd_id;}
static int swrm_cmd_fifo_wr_cmd(struct swr_mstr_ctrl*s,u8 val,u8 dev,u8 cmd,u16 reg){(void)s;(void)val;(void)dev;(void)cmd;(void)reg;io_count++;return write_error;}
static void swrm_wait_for_fifo_avail(struct swr_mstr_ctrl*s,int op){(void)s;(void)op;}
static void swr_master_write(struct swr_mstr_ctrl*s,u16 reg,u32 val){(void)s;last_wire=val;(void)reg;}
static u32 swr_master_read(struct swr_mstr_ctrl*s,unsigned int reg){
 if(reg==SWRM_COMP_PARAMS)return (4U<<15)|(4U<<10)|(11U<<20);
 if(reg==SWRM_COMP_HW_VERSION)return SWRM_VERSION_2_0;
 if(reg==SWRM_CMD_FIFO_RD_FIFO(s->ee_val)){
  io_count++;if(fifo_bad||(read_error&&io_count>=read_error))return 0xf00;
  return (s->rcmd_id<<8)|((++fifo_sequence)&0xff);
 }
 return 0;
}
static int swr_master_bulk_write(struct swr_mstr_ctrl*s,u32*regs,u32*vals,size_t n){(void)s;assert(regs&&vals&&n);io_count++;last_wire=vals[n-1];return write_error;}
static void usleep_range(unsigned lo,unsigned hi){(void)lo;(void)hi;}
void swr_port_response(struct swr_master*m,u8 tid){(void)m;responses++;last_tid=tid;}
static void swrm_wakeup_work(struct work_struct*w){(void)w;}
static void swrm_notify_work_fn(struct work_struct*w){(void)w;}
static int swrm_event_notify(struct notifier_block*n,unsigned long a,void*d){(void)n;(void)a;(void)d;return 0;}
static int swrm_slvdev_datapath_control(struct swr_master*m,bool b){(void)m;(void)b;return 0;}
static bool swrm_remove_from_group(struct swr_master*m){(void)m;return false;}
static int swrm_get_logical_dev_num(struct swr_master*m,u64 a,u8*b){(void)m;(void)a;(void)b;return 0;}
static void swrm_device_wakeup_vote(struct swr_master*m){(void)m;}
static void swrm_device_wakeup_unvote(struct swr_master*m){(void)m;}
static irqreturn_t swr_mstr_interrupt(int irq,void*d){(void)irq;(void)d;return IRQ_HANDLED;}
static irqreturn_t swrm_wakeup_interrupt(int irq,void*d){(void)irq;(void)d;return IRQ_HANDLED;}
static int swrm_clk_request(struct swr_mstr_ctrl*s,bool enable){if(enable){if(probe_fail==8)return -EIO;s->clk_ref_count++;}else {assert(s->clk_ref_count>0);s->clk_ref_count--;}return 0;}
static int swrm_request_hw_vote(struct swr_mstr_ctrl*s,int core,bool enable){int*p=core==LPASS_HW_CORE?&s->hw_core_clk_en:&s->aud_core_clk_en;if(enable&&probe_fail==7)return -EIO;*p+=enable?1:-1;assert(*p>=0);return 0;}
static int swrm_master_init(struct swr_mstr_ctrl*s){assert(s->master_registered&&s->irq_registered);return probe_fail==10?-EIO:0;}
static int swrm_do_notify(struct platform_device*p,u32 id,void*data){(void)id;(void)data;assert(platform_get_drvdata(p));atomic_store(&notified,true);while(!atomic_load(&notify_release))usleep(100);return 42;}
static int of_property_read_u32(struct device_node*n,const char*key,u32*v){(void)n;
 if(!strcmp(key,"qcom,swr_master_id")){*v=MASTER_ID_RX;return 0;}
 if(!strcmp(key,"qcom,swr-num-ports")){*v=2;return 0;}
 if(!strcmp(key,"qcom,swr-num-dev")){if(probe_fail==9)return -EINVAL;*v=2;return 0;}
 if(!strcmp(key,"qcom,swr-mstr-irq-wakeup-capable")){*v=1;return 0;}
 return -EINVAL;
}
static void *of_find_property(struct device_node*n,const char*k,int*sz){(void)n;(void)k;if(probe_fail==2)return NULL;*sz=6*sizeof(u32);return (void*)1;}
static int of_property_read_u32_array(struct device_node*n,const char*k,u32*t,size_t count){(void)n;(void)k;assert(count==6);u32 map[]={1,10,3,2,20,1};memcpy(t,map,sizeof(map));return probe_fail==3?-EINVAL:0;}
static void *devm_ioremap(struct device*d,unsigned long base,size_t len){(void)d;(void)base;(void)len;return (void*)1;}
static struct clk clk1,clk2;
static struct clk *devm_clk_get(struct device*d,const char*n){(void)d;if((probe_fail==11&&!strstr(n,"audio"))||(probe_fail==12&&strstr(n,"audio")))return (void*)(intptr_t)-EPROBE_DEFER;return strstr(n,"audio")?&clk2:&clk1;}
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((long)(p))
static int platform_get_irq_byname(struct platform_device*p,const char*n){(void)p;if(!strcmp(n,"swr_master_irq"))return probe_fail==5?-EPROBE_DEFER:31;return 32;}
static int request_threaded_irq(int irq,void*a,irqreturn_t(*fn)(int,void*),unsigned flags,const char*n,void*d){(void)a;(void)fn;(void)flags;(void)n;struct swr_mstr_ctrl*s=d;assert(s->wakeup_work.initialized&&s->dc_presence_work.initialized&&s->master_registered);if(probe_fail==6)return -EBUSY;if(irq==31)irq_live++;else{assert(irq==32);wake_live++;}return 0;}
static void free_irq(int irq,void*d){struct swr_mstr_ctrl*s=d;assert(!atomic_read(&s->notify_calls)&&!atomic_read(&s->master.active_calls));assert(!s->pm_enabled);if(irq==31){assert(irq_live==1);irq_live--;}else{assert(irq==32&&wake_live==1);wake_live--;}}
static int irq_set_irq_wake(int irq,int enabled){assert(irq==31||irq==32);(void)enabled;return 0;}
static int device_init_wakeup(struct device*d,bool enabled){(void)d;(void)enabled;return 0;}
static void cpu_latency_qos_add_request(struct pm_qos_request*q,int value){(void)value;assert(!q->active);q->active=true;qos_live++;}
static void cpu_latency_qos_remove_request(struct pm_qos_request*q){assert(q->active);q->active=false;qos_live--;}
static int of_get_named_gpio(struct device_node*n,const char*k,unsigned i){(void)n;(void)k;(void)i;return -EINVAL;}
#define gpio_is_valid(i) ((i)>=0)
static int gpio_to_irq(int gpio){return gpio;}
static void pm_runtime_set_autosuspend_delay(struct device*d,int delay){(void)d;(void)delay;}
static void pm_runtime_use_autosuspend(struct device*d){(void)d;}
static void pm_runtime_set_active(struct device*d){(void)d;}
static void pm_runtime_enable(struct device*d){(void)d;assert(!pm_live);pm_live=1;}
static void pm_runtime_disable(struct device*d){(void)d;assert(pm_live);pm_live=0;}
static void pm_runtime_set_suspended(struct device*d){(void)d;}
static const char *dev_name(struct device*d){(void)d;return "swr-test";}
static struct dentry *debugfs_create_dir(const char*n,void*p){(void)n;(void)p;return NULL;}
static struct dentry *debugfs_create_file(const char*n,int mode,void*p,void*d,const struct file_operations*f){(void)n;(void)mode;(void)p;(void)d;(void)f;return NULL;}
static void debugfs_remove_recursive(struct dentry*d){(void)d;}
static struct proc_dir_entry *proc_mkdir(const char*n,void*p){(void)n;(void)p;return NULL;}
static struct proc_dir_entry *proc_create_data(const char*n,int mode,void*p,const struct proc_ops*f,void*d){(void)n;(void)mode;(void)p;(void)f;(void)d;return NULL;}
static void proc_remove(struct proc_dir_entry*p){(void)p;}
static const struct file_operations swrm_debug_read_ops,swrm_debug_write_ops,swrm_debug_dump_ops;
static const struct proc_ops swr_mstr_ctrl_proc_ops;
int swr_register_master(struct swr_master*m){spin_lock_init(&m->state_lock);init_waitqueue_head(&m->call_wait);atomic_set(&m->active_calls,0);INIT_LIST_HEAD(&m->devices);m->stopping=false;if(probe_fail==4){m->release(m);return -ENOSPC;}assert(!master_live);master_live=1;return 0;}
void swr_unregister_master(struct swr_master*m){assert(master_live&&m->stopping&&!atomic_read(&m->active_calls));master_live=0;assert(!irq_live&&!wake_live&&!pm_live&&cancel_count==2);atomic_store(&unregistered,true);if(hold_master)pending_master=m;else m->release(m);}
void swr_master_add_boarddevices(struct swr_master*m){struct swr_mstr_ctrl*s=swr_get_ctrl_data(m);assert(s->ready&&s->pm_enabled&&s->irq_registered);children++;}
int of_register_swr_devices(struct swr_master*m){struct swr_mstr_ctrl*s=swr_get_ctrl_data(m);assert(s->ready);children++;return 0;}
/* SOURCE */
static struct device_node node;
static int parent_read(void*h,int r){(void)h;(void)r;return 0;}
static int parent_write(void*h,int r,int v){(void)h;(void)r;(void)v;return 0;}
static int parent_bulk(void*h,u32*r,u32*v,size_t n){(void)h;(void)r;(void)v;(void)n;return 0;}
static int parent_clk(void*h,bool b){(void)h;(void)b;return 0;}
static struct swr_ctrl_platform_data pdata={.handle=(void*)1,.read=parent_read,.write=parent_write,.bulk_write=parent_bulk,.clk=parent_clk};
static struct platform_device pdev={.dev.of_node=&node,.dev.platform_data=&pdata};
static struct swr_mstr_ctrl *setup(void){assert(!swrm_probe(&pdev));struct swr_mstr_ctrl*s=platform_get_drvdata(&pdev);assert(s&&s->ready&&s->refs.refs==2&&master_live&&irq_live&&qos_live&&pm_live&&children==2);return s;}
static void finish(void){swrm_remove(&pdev);assert(!pdev.dev.data&&!master_live&&!irq_live&&!wake_live&&!qos_live&&!pm_live);devres_release();assert(!atomic_load(&live));}
static struct swr_params request={.tid=177,.dev_num=2,.num_port=2,.port_id={1,2},.num_ch={2,1},.ch_rate={48000,96000},.ch_en={3,1},.port_type={10,20}};
static unsigned ports(void){struct swr_mstr_ctrl*s=setup();assert(!swrm_connect_port(&s->master,&request));assert(responses==1&&last_tid==177&&pm_refs==1&&s->master.num_port==2);assert(s->master.port_en_mask==3&&s->mport_cfg[0].req_ch==3);struct swr_port_info*p=swrm_get_port_req(&s->mport_cfg[0],1,2);assert(p&&p->req_ch_rate==48000&&p->num_ch==2);assert(!swrm_disconnect_port(&s->master,&request));assert(responses==2&&s->master.num_port==0);swrm_cleanup_disabled_port_reqs(&s->master);assert(s->master.port_en_mask==0);pm_runtime_put_autosuspend(s->dev);finish();return 8;}
static unsigned port_failures(void){struct swr_mstr_ctrl*s=setup();unsigned n=0;for(int fail=1;fail<=2;fail++){allocation_steps=0;allocation_fail=fail;assert(swrm_connect_port(&s->master,&request)==-ENOMEM);assert(!responses&&!pm_refs&&!s->master.num_port&&!s->master.port_en_mask&&list_empty(&s->mport_cfg[0].port_req_list));n++;}allocation_fail=0;pm_error=-EIO;assert(swrm_connect_port(&s->master,&request)==-EIO&&!responses&&!s->master.num_port);pm_error=0;request.port_type[1]=99;assert(swrm_connect_port(&s->master,&request)==-EINVAL&&!responses&&!s->master.port_en_mask);request.port_type[1]=20;assert(!swrm_connect_port(&s->master,&request));responses=0;request.port_type[1]=99;assert(swrm_disconnect_port(&s->master,&request)==-EINVAL&&!responses&&s->master.num_port==2&&s->mport_cfg[0].req_ch==3);request.port_type[1]=20;request.num_port=15;assert(swrm_connect_port(&s->master,&request)==-EINVAL);request.num_port=2;pm_runtime_put_autosuspend(s->dev);finish();return n+4;}
static unsigned configuration(void){struct swr_mstr_ctrl*s=setup();struct port_params pp[13]={0};pp[0].si=0xabcd;struct swrm_port_config cfg={.uc=0,.size=1,.params=pp};assert(!swrm_set_port_config(s,&cfg));cfg.size=13;assert(!swrm_set_port_config(s,&cfg)&&s->port_param_size[0]==13&&s->port_param[0][0].si==0xabcd);void*old=s->port_param[0];allocation_steps=0;allocation_fail=1;assert(swrm_set_port_config(s,&cfg)==-ENOMEM&&s->port_param[0]==old);allocation_fail=0;cfg.uc=SWR_UC_MAX;assert(swrm_set_port_config(s,&cfg)==-EINVAL);cfg.uc=0;cfg.size=14;assert(swrm_set_port_config(s,&cfg)==-EINVAL);cfg.size=1;cfg.params=NULL;assert(swrm_set_port_config(s,&cfg)==-EINVAL);struct swr_port_params values[SWR_UC_MAX][14]={0};struct swr_dev_frame_config uc[SWR_UC_MAX];for(unsigned i=0;i<SWR_UC_MAX;i++){uc[i].pp=values[i];values[i][13].offset1=123;}assert(!swrm_init_port_params(&s->master,11,14,uc)&&s->pp[0][153].offset1==123);assert(swrm_init_port_params(&s->master,12,14,uc)==-EINVAL);assert(swrm_init_port_params(&s->master,0,14,uc)==-EINVAL);uc[SWR_UC_MAX-1].pp=NULL;assert(swrm_init_port_params(&s->master,1,14,uc)==-EINVAL);assert(!swrm_get_port_config(s));s->mport_cfg[12].port_en=true;s->port_param_size[0]=1;assert(swrm_get_port_config(s)==-EINVAL);finish();return 12;}
static unsigned io(void){struct swr_mstr_ctrl*s=setup();u8 out[4]={0};assert(!swrm_read(&s->master,2,0x1234,out,4));assert(out[0]==1&&out[3]==4&&io_count==4&&!pm_refs);assert(swrm_read(&s->master,2,0xffff,out,2)==-EINVAL);assert(swrm_read(&s->master,2,0,NULL,1)==-EINVAL);pm_error=-EAGAIN;assert(swrm_read(&s->master,2,0,out,1)==-EAGAIN&&io_count==4);pm_error=0;read_error=io_count+1;assert(swrm_read(&s->master,2,0,out,1)==-EIO&&!pm_refs);read_error=0;write_error=-EREMOTEIO;assert(swrm_write(&s->master,2,0,out)==-EREMOTEIO&&!pm_refs);u16 regs[2]={0x1234,0xabcd};u8 vals[2]={0x77,0x88};assert(swrm_bulk_write(&s->master,2,regs,vals,2)==-EREMOTEIO&&!pm_refs);write_error=0;assert(!swrm_bulk_write(&s->master,2,regs,vals,2));assert((last_wire&0xffff)==0xabcd&&(last_wire>>24)==0x88&&((last_wire>>20)&15)==2);s->dev_up=false;assert(swrm_read(&s->master,2,0,out,1)==-ENODEV&&swrm_write(&s->master,2,0,out)==-ENODEV);s->dev_up=true;finish();return 10;}
static unsigned packing(void){unsigned n=0;for(unsigned dev=1;dev<=15;dev++)for(unsigned data=0;data<256;data++){u8 id=13;u32 wire=swrm_get_packed_reg_val(&id,data,dev,0xabcd);assert(id==14&&(wire&0xffff)==0xabcd&&((wire>>16)&15)==14&&((wire>>20)&15)==dev&&(wire>>24)==data);wire=swrm_get_packed_reg_val(&id,data,dev,0xffff);assert(id==0&&(wire&0xffff)==0xffff&&((wire>>16)&15)==0);n++;}return n;}
static unsigned fifo(void){struct swr_mstr_ctrl*s=setup();int data;fifo_bad=true;assert(swrm_cmd_fifo_rd_cmd(s,&data,2,0,0x1234,1)==-EIO&&io_count==4);fifo_bad=false;assert(!swrm_cmd_fifo_rd_cmd(s,&data,2,0,0x1234,1));assert((data&255)==1);finish();return 2;}
static void *remove_thread(void*p){swrm_remove(p);atomic_store(&stop_done,true);return NULL;}
static void *notify_thread(void*p){assert(swrm_wcd_notify(p,1,NULL)==42);return NULL;}
static unsigned lifecycle(void){struct swr_mstr_ctrl*s=setup();assert(!swrm_register_wake_irq(s)&&wake_live==1);assert(!swrm_register_wake_irq(s)&&wake_live==1);hold_master=true;pthread_t th;assert(!pthread_create(&th,NULL,remove_thread,&pdev));while(!atomic_load(&unregistered))usleep(100);usleep(1000);assert(!atomic_load(&stop_done)&&pending_master&&s->refs.refs==2);pending_master->release(pending_master);pthread_join(th,NULL);assert(!pdev.dev.data&&!wake_live&&!irq_live&&!master_live&&!qos_live&&cancel_count==2);devres_release();assert(!atomic_load(&live));return 6;}
static int parent_irq(void*h,irqreturn_t(*fn)(int,void*),void*d,int action){(void)h;(void)fn;struct swr_mstr_ctrl*s=d;assert(s->master_registered);if(action==SWR_IRQ_REGISTER){assert(!irq_live);irq_live++;}else{assert(irq_live==1&&!s->pm_enabled&&!atomic_read(&s->notify_calls));irq_live--;}return 0;}
static unsigned parent_lifecycle(void){pdata.reg_irq=parent_irq;struct swr_mstr_ctrl*s=setup();assert(!s->irq_wake_enabled);assert(!swrm_register_wake_irq(s));assert(wake_live==1);finish();return 3;}
static unsigned probe_failures(void){unsigned n=0;for(int f=1;f<=12;f++){probe_fail=f;children=0;cancel_count=0;allocation_steps=0;if(f==1)allocation_fail=1;else allocation_fail=0;int r=swrm_probe(&pdev);assert(r<0&&!pdev.dev.data&&!qos_live&&!master_live&&!irq_live&&!wake_live&&!pm_live&&!children);devres_release();assert(!atomic_load(&live));n++;}return n;}
static unsigned drain(void){struct swr_mstr_ctrl*s=setup();atomic_inc(&s->master.active_calls);pthread_t th;pthread_create(&th,NULL,remove_thread,&pdev);while(!READ_ONCE(s->master.stopping))usleep(100);usleep(1000);assert(irq_live&&!atomic_load(&stop_done)&&master_live);atomic_dec_and_test(&s->master.active_calls);wake_up_all(&s->master.call_wait);pthread_join(th,NULL);devres_release();assert(!atomic_load(&live));return 3;}
static unsigned notification_drain(void){struct swr_mstr_ctrl*s=setup();pthread_t n,t;pthread_create(&n,NULL,notify_thread,&pdev);while(!atomic_load(&notified))usleep(100);pthread_create(&t,NULL,remove_thread,&pdev);while(!READ_ONCE(s->stopping))usleep(100);usleep(1000);assert(irq_live&&!atomic_load(&stop_done));assert(swrm_wcd_notify(&pdev,1,NULL)==-ESHUTDOWN);atomic_store(&notify_release,true);pthread_join(n,NULL);pthread_join(t,NULL);assert(swrm_wcd_notify(&pdev,1,NULL)==-ESHUTDOWN);devres_release();assert(!atomic_load(&live));return 4;}
int main(int argc,char**argv){assert(argc==2);unsigned n=0;if(!strcmp(argv[1],"ports"))n=ports();else if(!strcmp(argv[1],"port_failures"))n=port_failures();else if(!strcmp(argv[1],"configuration"))n=configuration();else if(!strcmp(argv[1],"io"))n=io();else if(!strcmp(argv[1],"fifo"))n=fifo();else if(!strcmp(argv[1],"packing"))n=packing();else if(!strcmp(argv[1],"lifecycle"))n=lifecycle();else if(!strcmp(argv[1],"parent_lifecycle"))n=parent_lifecycle();else if(!strcmp(argv[1],"probe_failures"))n=probe_failures();else if(!strcmp(argv[1],"drain"))n=drain();else if(!strcmp(argv[1],"notification_drain"))n=notification_drain();else assert(false);printf("{\"%s\":%u}\n",argv[1],n);return 0;}
