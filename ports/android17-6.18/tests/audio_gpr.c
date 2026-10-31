#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define GFP_KERNEL 0
#define GFP_ATOMIC 1
#define EPROBE_DEFER 517
#define __init
#define __exit
#define __packed __attribute__((packed))
#define DECLARE_FLEX_ARRAY(t,n) t n[]
#define THIS_MODULE NULL
#define NOTIFY_OK 1
#define IS_ERR(p) ((intptr_t)(p)<0&&(intptr_t)(p)>-4096)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_info_ratelimited(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define container_of_const(p,t,m) ((const t *)((const char *)(p)-offsetof(t,m)))
typedef uint32_t u32;
typedef uint32_t __u32;
typedef unsigned long kernel_ulong_t;
typedef atomic_int atomic_t;
#define atomic_read(p) atomic_load(p)
#define atomic_set(p,v) atomic_store(p,v)
typedef struct { pthread_mutex_t lock; } spinlock_t;
#define DEFINE_SPINLOCK(n) spinlock_t n={PTHREAD_MUTEX_INITIALIZER}
#define spin_lock_irqsave(p,f) ((f)=0,assert(!pthread_mutex_lock(&(p)->lock)))
#define spin_unlock_irqrestore(p,f) ((void)(f),assert(!pthread_mutex_unlock(&(p)->lock)))
static void spin_lock_init(spinlock_t *p){assert(!pthread_mutex_init(&p->lock,NULL));}
static void spin_lock(spinlock_t *p){assert(!pthread_mutex_lock(&p->lock));}
static void spin_unlock(spinlock_t *p){assert(!pthread_mutex_unlock(&p->lock));}
struct mutex { pthread_mutex_t lock; };
static void mutex_init(struct mutex *p){assert(!pthread_mutex_init(&p->lock,NULL));}
struct list_head { struct list_head *next,*prev; };
struct kref { atomic_int refs; };
static void kref_init(struct kref *p){atomic_store(&p->refs,1);}
static void kref_get(struct kref *p){assert(atomic_fetch_add(&p->refs,1)>0);}
static void kref_put(struct kref *p,void (*release)(struct kref *)){if(atomic_fetch_sub(&p->refs,1)==1)release(p);}
static atomic_int live,steps;
static int fail_step;
static bool failure(void){return atomic_fetch_add(&steps,1)+1==fail_step;}
static void *kzalloc(size_t s,int f){(void)f;if(failure())return NULL;void *p=calloc(1,s);assert(p);atomic_fetch_add(&live,1);return p;}
static void kfree(void *p){if(p){free(p);assert(atomic_fetch_sub(&live,1)>0);}}
struct device_node { const char *name;bool available,present;u32 reg;atomic_int refs;struct device_node *child,*next; };
static struct device_node *of_node_get(struct device_node *p){if(p)atomic_fetch_add(&p->refs,1);return p;}
static void of_node_put(struct device_node *p){if(p)assert(atomic_fetch_sub(&p->refs,1)>0);}
static struct device_node *next_child(struct device_node *root,struct device_node *previous){struct device_node *p=previous?previous->next:root->child;of_node_put(previous);while(p&&!p->available)p=p->next;return of_node_get(p);}
#define for_each_available_child_of_node(root,node) for(node=next_child(root,NULL);node;node=next_child(root,node))
#define for_each_child_of_node(root,node) for_each_available_child_of_node(root,node)
static int of_property_read_u32(struct device_node *n,const char *name,void *out){assert(!strcmp(name,"reg"));if(!n||!n->present)return -EINVAL;memcpy(out,&n->reg,4);return 0;}
struct device;
struct device_driver;
struct kobj_uevent_env { int dummy; };
struct module { int dummy; };
struct bus_type { const char *name;int (*match)(struct device *,const struct device_driver *);int (*probe)(struct device *);void (*remove)(struct device *);int (*uevent)(const struct device *,struct kobj_uevent_env *); };
struct device_driver { const struct bus_type *bus;struct module *owner; };
struct device { struct device *parent,*children,*next;struct device_node *of_node;const struct bus_type *bus;struct device_driver *driver;void (*release)(struct device *);void *data,*managed;atomic_int refs;pthread_mutex_t lock,children_lock;bool initialized,registered;int id;char name[96]; };
static void device_initialize(struct device *d){assert(!d->initialized);atomic_store(&d->refs,1);assert(!pthread_mutex_init(&d->lock,NULL));assert(!pthread_mutex_init(&d->children_lock,NULL));d->initialized=true;}
static struct device *get_device(struct device *d){assert(d->initialized&&atomic_fetch_add(&d->refs,1)>0);return d;}
static void put_device(struct device *d){assert(d->initialized);if(atomic_fetch_sub(&d->refs,1)==1){assert(!d->registered);assert(!pthread_mutex_destroy(&d->lock));assert(!pthread_mutex_destroy(&d->children_lock));if(d->release)d->release(d);}}
static void device_lock(struct device *d){assert(!pthread_mutex_lock(&d->lock));}
static void device_unlock(struct device *d){assert(!pthread_mutex_unlock(&d->lock));}
static void *dev_get_drvdata(const struct device *d){return __atomic_load_n(&d->data,__ATOMIC_ACQUIRE);}
static void dev_set_drvdata(struct device *d,void *p){__atomic_store_n(&d->data,p,__ATOMIC_RELEASE);}
static void *devm_kzalloc(struct device *d,size_t s,int f){assert(!d->managed);return d->managed=kzalloc(s,f);}
static struct device_driver *active_driver;
static int dev_set_name(struct device *d,const char *fmt,const char *name,u32 domain,u32 id){(void)fmt;if(failure())return -ENOMEM;snprintf(d->name,sizeof(d->name),"gprsvc:%s:%x:%x",name,domain,id);return 0;}
static const char *dev_name(struct device *d){return d->name;}
static int device_add(struct device *d){if(failure())return -ENOMEM;d->registered=true;assert(d->parent);pthread_mutex_lock(&d->parent->children_lock);d->next=d->parent->children;d->parent->children=d;pthread_mutex_unlock(&d->parent->children_lock);d->driver=active_driver;if(d->driver&&d->bus->probe)assert(!d->bus->probe(d));return 0;}
static int device_register(struct device *d){device_initialize(d);return device_add(d);}
static pthread_mutex_t gate_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition=PTHREAD_COND_INITIALIZER;
static bool gate_callback,callback_entered,release_callback,removal_entered;
static void device_unregister(struct device *d){pthread_mutex_lock(&gate_lock);if(gate_callback){removal_entered=true;pthread_cond_broadcast(&gate_condition);}pthread_mutex_unlock(&gate_lock);device_lock(d);if(d->driver&&d->bus->remove)d->bus->remove(d);d->driver=NULL;device_unlock(d);pthread_mutex_lock(&d->parent->children_lock);struct device **p=&d->parent->children;while(*p&&*p!=d)p=&(*p)->next;assert(*p);*p=d->next;pthread_mutex_unlock(&d->parent->children_lock);d->registered=false;put_device(d);}
static int device_for_each_child(struct device *parent,void *data,int (*fn)(struct device *,void *)){for(;;){pthread_mutex_lock(&parent->children_lock);struct device *d=parent->children;if(d)get_device(d);pthread_mutex_unlock(&parent->children_lock);if(!d)return 0;int ret=fn(d,data);put_device(d);if(ret)return ret;}}
static bool of_driver_match_device(struct device *d,const struct device_driver *v){(void)d;(void)v;return false;}
static int of_device_uevent_modalias(const struct device *d,struct kobj_uevent_env *v){(void)d;(void)v;return -ENODEV;}
static int add_uevent_var(struct kobj_uevent_env *v,const char *fmt,const char *name){(void)v;(void)fmt;(void)name;return 0;}
static int driver_register(struct device_driver *d){(void)d;return 0;}
static void driver_unregister(struct device_driver *d){(void)d;}
struct idr { struct {u32 id;void *value;} slots[8]; };
static void idr_init(struct idr *i){memset(i,0,sizeof(*i));}
static void *idr_find(struct idr *i,unsigned long id){for(int s=0;s<8;s++)if(i->slots[s].value&&i->slots[s].id==id)return i->slots[s].value;return NULL;}
static int idr_alloc_u32(struct idr *i,void *v,u32 *id,u32 max,int flags){(void)flags;assert(*id==max);if(failure())return -ENOMEM;if(idr_find(i,*id))return -ENOSPC;for(int s=0;s<8;s++)if(!i->slots[s].value){i->slots[s].id=*id;i->slots[s].value=v;return 0;}return -ENOSPC;}
static int idr_alloc(struct idr *i,void *v,int start,int end,int flags){if(start<0||end<=start)return -EINVAL;u32 id=start;int r=idr_alloc_u32(i,v,&id,end-1,flags);return r?r:start;}
static void *idr_remove(struct idr *i,unsigned long id){for(int s=0;s<8;s++)if(i->slots[s].value&&i->slots[s].id==id){void *v=i->slots[s].value;i->slots[s].value=NULL;return v;}return NULL;}
static void idr_destroy(struct idr *i){for(int s=0;s<8;s++)assert(!i->slots[s].value);}
struct rpmsg_endpoint { int sends,id; };
struct rpmsg_device { struct device dev;struct rpmsg_endpoint *ept; };
static int rpmsg_trysend(struct rpmsg_endpoint *e,void *ptr,int len){assert(e&&ptr&&len>=24);volatile unsigned char last=((unsigned char *)ptr)[len-1];(void)last;e->sends++;return 0;}
struct wakeup_source { struct device *dev;bool live; };
static atomic_int wakes,relaxes;
static struct wakeup_source *wakeup_source_register(struct device *d,const char *name){(void)name;struct wakeup_source *p=kzalloc(sizeof(*p),0);if(p){p->dev=d;p->live=true;}return p;}
static void wakeup_source_unregister(struct wakeup_source *p){assert(p&&p->live);p->live=false;kfree(p);}
static void pm_wakeup_ws_event(struct wakeup_source *p,int timeout,bool hard){(void)timeout;(void)hard;assert(p&&p->live);wakes++;}
static void __pm_relax(struct wakeup_source *p){assert(p&&p->live);relaxes++;}
struct notifier_block { int (*notifier_call)(struct notifier_block *,unsigned long,void *);int priority; };
enum { AUDIO_NOTIFIER_SERVICE_DOWN,AUDIO_NOTIFIER_SERVICE_UP };
enum { AUDIO_NOTIFIER_ADSP_DOMAIN,AUDIO_NOTIFIER_MODEM_DOMAIN };
struct audio_notifier_cb_data { int service,domain; };
static bool notifier_ready=true;
static bool audio_notifier_probe_status(void){return notifier_ready;}
static struct {bool registered;char name[24];struct notifier_block *nb;int domain;} subscriptions[2];
static int audio_notifier_register(char *name,int domain,struct notifier_block *nb){if(failure())return -ENOMEM;assert(domain>=0&&domain<2&&!subscriptions[domain].registered);subscriptions[domain].registered=true;strcpy(subscriptions[domain].name,name);subscriptions[domain].nb=nb;subscriptions[domain].domain=domain;struct audio_notifier_cb_data data={.domain=domain};nb->notifier_call(nb,AUDIO_NOTIFIER_SERVICE_DOWN,&data);return 0;}
static int audio_notifier_deregister(char *name){for(int i=0;i<2;i++)if(subscriptions[i].registered&&!strcmp(subscriptions[i].name,name))subscriptions[i].registered=false;return 0;}
static void service_event(int domain,int state){assert(subscriptions[domain].registered);struct audio_notifier_cb_data d={.domain=domain};subscriptions[domain].nb->notifier_call(subscriptions[domain].nb,state,&d);}
enum { SND_EVENT_DOWN,SND_EVENT_UP };
struct snd_event_ops { int (*enable)(struct device *,void *);void (*disable)(struct device *,void *); };
static bool snd_registered[2];
static atomic_int snd_state[2];
static int snd_event_client_register(struct device *d,const struct snd_event_ops *ops,void *data){(void)ops;(void)data;if(failure())return -ENOMEM;assert(!snd_registered[d->id]);snd_registered[d->id]=true;return 0;}
static int snd_event_client_deregister(struct device *d){assert(snd_registered[d->id]);snd_registered[d->id]=false;snd_state[d->id]=0;return 0;}
static int snd_event_notify(struct device *d,unsigned state){assert(snd_registered[d->id]);snd_state[d->id]=state;return 0;}
struct work_struct {void (*fn)(struct work_struct *);};
#define INIT_WORK(w,cb) ((w)->fn=(cb))
static bool schedule_work(struct work_struct *w){w->fn(w);return true;}
struct of_device_id {const char *compatible;};
struct rpmsg_driver {int (*probe)(struct rpmsg_device *);void (*remove)(struct rpmsg_device *);int (*callback)(struct rpmsg_device *,void *,int,void *,u32);struct {const char *name;const struct of_device_id *of_match_table;} drv;};
static bool bus_registered,rpmsg_registered,fail_bus,fail_rpmsg;
static int bus_register(const struct bus_type *b){(void)b;if(fail_bus)return -ENOMEM;assert(!bus_registered);bus_registered=true;return 0;}
static void bus_unregister(const struct bus_type *b){(void)b;assert(bus_registered);bus_registered=false;}
static int register_rpmsg_driver(struct rpmsg_driver *d){(void)d;if(fail_rpmsg)return -ENOMEM;assert(bus_registered&&!rpmsg_registered);rpmsg_registered=true;return 0;}
static void unregister_rpmsg_driver(struct rpmsg_driver *d){(void)d;assert(rpmsg_registered&&bus_registered);rpmsg_registered=false;}
static size_t strscpy(char *d,const char *s,size_t n){size_t l=strnlen(s,n);if(l>=n)l=n-1;memcpy(d,s,l);d[l]=0;return l;}
/* HEADERS */
/* SOURCE */
static atomic_int deliveries;
static u32 delivered_id;
static int client_probe(struct gpr_device *d){(void)d;return 0;}
static int client_callback(struct gpr_device *d,void *buf){(void)buf;pthread_mutex_lock(&gate_lock);if(gate_callback){callback_entered=true;pthread_cond_broadcast(&gate_condition);while(!release_callback)pthread_cond_wait(&gate_condition,&gate_lock);}pthread_mutex_unlock(&gate_lock);delivered_id=d->svc_id;deliveries++;return 0;}
static struct gpr_driver client={.probe=client_probe,.callback=client_callback};
static struct device_node children[2]={{.name="core",.available=true,.present=true,.reg=3,.refs=1},{.name="large",.available=true,.present=true,.reg=0x10003,.refs=1}};
static struct device_node roots[2]={{.name="adsp",.available=true,.present=true,.reg=2,.refs=1},{.name="modem",.available=true,.present=true,.reg=1,.refs=1}};
static struct rpmsg_endpoint endpoints[2]={{.id=0},{.id=1}};
static struct rpmsg_device parents[2];
static void initialize(void){active_driver=&client.driver;for(int i=0;i<2;i++){parents[i].dev.id=i;parents[i].dev.of_node=&roots[i];parents[i].ept=&endpoints[i];device_initialize(&parents[i].dev);}roots[0].child=&children[0];children[0].next=&children[1];}
static void cleanup_parent(int i){kfree(parents[i].dev.managed);parents[i].dev.managed=NULL;dev_set_drvdata(&parents[i].dev,NULL);assert(!parents[i].dev.children&&!snd_registered[i]&&!subscriptions[i].registered);}
static void bind(int i){assert(!gpr_probe(&parents[i]));}
static void unbind(int i){gpr_remove(&parents[i]);cleanup_parent(i);}
static void clean(void){assert(!live);for(int i=0;i<2;i++)assert(children[i].refs==1&&!parents[i].dev.children);}
static u32 wire(unsigned version,unsigned words,unsigned bytes){return version|(words<<4)|(bytes<<8);}
static unsigned packet_bounds(void){bind(0);unsigned char message[64]={0};struct gpr_hdr *h=(void *)message;h->dst_port=3;unsigned cases=0;unsigned sizes[]={0,23,24,40,60,65535,65536,0xffffff};
 for(unsigned v=0;v<16;v++)for(unsigned w=0;w<16;w++)for(unsigned p=0;p<8;p++)for(int len=-8;len<=64;len++){
  h->header=wire(v,w,sizes[p]);int before=deliveries;int ret=gpr_callback(&parents[0],h,len,NULL,0);bool valid=v==0&&w*4>=24&&w*4<=sizes[p]&&len>=24&&(unsigned)len==sizes[p];assert((ret==0)==valid);assert(deliveries-before==valid);cases++;
 }
 unsigned char *short_packet=malloc(1);assert(gpr_callback(&parents[0],short_packet,-1,NULL,0)==-EINVAL);free(short_packet);cases++;
 assert(gpr_callback(&parents[0],NULL,24,NULL,0)==-EINVAL);cases++;unbind(0);clean();return cases;
}
static unsigned routing(void){bind(0);struct gpr_hdr h={.header=wire(0,6,24),.dst_port=0x10003};assert(!gpr_callback(&parents[0],&h,24,NULL,0)&&delivered_id==0x10003);struct gpr *g=dev_get_drvdata(&parents[0].dev);struct gpr_device *svc=idr_find(&g->svcs_idr,h.dst_port);assert(svc&&svc->svc_id==h.dst_port&&svc->dev.of_node->refs==2);
 device_lock(&svc->dev);gpr_device_remove(&svc->dev);svc->dev.driver=NULL;device_unlock(&svc->dev);assert(idr_find(&g->svcs_idr,h.dst_port)==svc);assert(gpr_callback(&parents[0],&h,24,NULL,0)<0);svc->dev.driver=&client.driver;assert(!gpr_callback(&parents[0],&h,24,NULL,0));
 struct gpr_device_id id={.svc_id=GPR_SVC_MAX,.domain_id=2};assert(!gpr_add_device(&parents[0].dev,NULL,&id));h.dst_port=UINT32_MAX;assert(!gpr_callback(&parents[0],&h,24,NULL,0)&&delivered_id==GPR_SVC_MAX);unbind(0);clean();return 5;
}
static unsigned transmit(void){bind(0);service_event(0,AUDIO_NOTIFIER_SERVICE_UP);struct gpr *g=dev_get_drvdata(&parents[0].dev);struct gpr_device *svc=idr_find(&g->svcs_idr,3);unsigned sizes[]={24,28,60,65536,0xffffff};unsigned cases=0;
 for(unsigned i=0;i<5;i++){struct gpr_pkt *p=calloc(1,sizes[i]);assert(p);p->hdr.header=wire(0,6,sizes[i]);assert(gpr_send_pkt(svc,p)==(int)sizes[i]);assert(p->hdr.dst_domain_id==2);free(p);cases++;}
 assert(gpr_send_pkt(svc,NULL)==-EINVAL);struct gpr_pkt p={.hdr.header=wire(1,6,24)};assert(gpr_send_pkt(svc,&p)==-EINVAL);cases+=2;unbind(0);clean();return cases;
}
static unsigned domains(void){bind(0);bind(1);service_event(0,AUDIO_NOTIFIER_SERVICE_UP);service_event(1,AUDIO_NOTIFIER_SERVICE_UP);assert(gpr_get_q6_state()==GPR_SUBSYS_LOADED&&gpr_get_modem_state()==GPR_SUBSYS_UP);assert(snd_state[0]==SND_EVENT_UP&&snd_state[1]==SND_EVENT_UP);service_event(0,AUDIO_NOTIFIER_SERVICE_DOWN);assert(gpr_get_q6_state()==GPR_SUBSYS_DOWN&&gpr_get_modem_state()==GPR_SUBSYS_UP);struct gpr_device modem={.dev.parent=&parents[1].dev,.domain_id=1};struct gpr_pkt pkt={.hdr.header=wire(0,6,24)};assert(gpr_send_pkt(&modem,&pkt)==24);unbind(1);assert(gpr_get_modem_state()==GPR_SUBSYS_DOWN);unbind(0);clean();return 5;}
static unsigned probe_faults(void){unsigned cases=0;notifier_ready=false;assert(gpr_probe(&parents[0])==-EPROBE_DEFER);notifier_ready=true;cases++;
 roots[0].present=false;assert(gpr_probe(&parents[0])==-EINVAL);roots[0].present=true;cases++;
 for(unsigned d=0;d<8;d++)if(d!=1&&d!=2){roots[0].reg=d;assert(gpr_probe(&parents[0])==-EINVAL);cases++;}roots[0].reg=2;
 for(int f=1;f<=12;f++){steps=0;fail_step=f;assert(gpr_probe(&parents[0])<0);fail_step=0;cleanup_parent(0);clean();cases++;}
 bind(0);struct rpmsg_device duplicate={.dev.of_node=&roots[0],.ept=&endpoints[0]};device_initialize(&duplicate.dev);assert(gpr_probe(&duplicate)==-EBUSY);put_device(&duplicate.dev);assert(subscriptions[0].registered&&snd_registered[0]);unbind(0);clean();cases++;return cases;
}
static struct gpr_hdr event_packet;
static int receive_result;
static void *receive(void *unused){(void)unused;receive_result=gpr_callback(&parents[0],&event_packet,24,NULL,0);return NULL;}
static void *remove_parent(void *unused){(void)unused;gpr_remove(&parents[0]);return NULL;}
static void await(bool *flag){struct timespec until;timespec_get(&until,TIME_UTC);until.tv_sec+=5;pthread_mutex_lock(&gate_lock);while(!*flag)assert(!pthread_cond_timedwait(&gate_condition,&gate_lock,&until));pthread_mutex_unlock(&gate_lock);}
static unsigned lifetime(void){bind(0);struct gpr *g=dev_get_drvdata(&parents[0].dev);struct gpr_device *svc=idr_find(&g->svcs_idr,3);event_packet=(struct gpr_hdr){.header=wire(0,6,24),.dst_port=3};gate_callback=true;pthread_t rx,rm;assert(!pthread_create(&rx,NULL,receive,NULL));await(&callback_entered);assert(svc->dev.refs>=2);assert(!pthread_create(&rm,NULL,remove_parent,NULL));await(&removal_entered);assert(gpr_send_pkt(svc,(struct gpr_pkt *)&event_packet)==-ENETRESET);struct rpmsg_device duplicate={.dev.of_node=&roots[0],.ept=&endpoints[0]};device_initialize(&duplicate.dev);assert(gpr_probe(&duplicate)==-EBUSY);put_device(&duplicate.dev);pthread_mutex_lock(&gate_lock);release_callback=true;pthread_cond_broadcast(&gate_condition);pthread_mutex_unlock(&gate_lock);assert(!pthread_join(rx,NULL));assert(!pthread_join(rm,NULL));assert(!receive_result);gate_callback=false;cleanup_parent(0);clean();return 1;}
static unsigned init_faults(void){fail_bus=true;assert(gpr_init()<0&&!bus_registered&&!rpmsg_registered);fail_bus=false;fail_rpmsg=true;assert(gpr_init()<0&&!bus_registered&&!rpmsg_registered);fail_rpmsg=false;assert(!gpr_init());gpr_exit();assert(!bus_registered&&!rpmsg_registered);return 3;}
int main(int argc,char **argv){assert(argc==2);initialize();unsigned n=0;if(!strcmp(argv[1],"init"))n=init_faults();else{assert(!gpr_init());if(!strcmp(argv[1],"bounds"))n=packet_bounds();else if(!strcmp(argv[1],"routing"))n=routing();else if(!strcmp(argv[1],"transmit"))n=transmit();else if(!strcmp(argv[1],"domains"))n=domains();else if(!strcmp(argv[1],"probe"))n=probe_faults();else if(!strcmp(argv[1],"lifetime"))n=lifetime();else abort();gpr_exit();}for(int i=0;i<2;i++)put_device(&parents[i].dev);printf("{\"gpr_%s_cases\":%u}\n",argv[1],n);}
