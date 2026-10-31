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
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#define __init
#define __exit
#define GFP_KERNEL 0
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define container_of_const(p,t,m) ((const t *)((const char *)(p)-offsetof(t,m)))
typedef uint32_t u32,__u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uint64_t u64;
typedef unsigned long kernel_ulong_t;
typedef int pm_message_t;
typedef struct {atomic_int value;} atomic_t;
typedef atomic_t refcount_t;
static int atomic_read(atomic_t *a){return atomic_load(&a->value);}
static void atomic_inc(atomic_t *a){atomic_fetch_add(&a->value,1);}
static bool atomic_dec_and_test(atomic_t *a){return atomic_fetch_sub(&a->value,1)==1;}
static void atomic_set(atomic_t *a,int n){atomic_store(&a->value,n);}
#define refcount_set atomic_set
#define refcount_dec_and_test atomic_dec_and_test
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_init(struct mutex *m){assert(!pthread_mutex_init(&m->lock,NULL));}
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
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
static void INIT_LIST_HEAD(struct list_head *p){p->next=p->prev=p;}
static bool list_empty(struct list_head *p){return p->next==p;}
static void list_add_tail(struct list_head *p,struct list_head *h){p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
static void list_del_init(struct list_head *p){p->next->prev=p->prev;p->prev->next=p->next;INIT_LIST_HEAD(p);}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
static atomic_int live;
static int fail_step,steps;
static bool fault(void){return ++steps==fail_step;}
static void *kzalloc(size_t n,int f){(void)f;if(fault())return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
static void *kcalloc(size_t n,size_t size,int f){if(n&&size>SIZE_MAX/n)return NULL;return kzalloc(n*size,f);}
static void kfree(void *p){if(p){free(p);assert(live-->0);}}
struct device;
struct device_driver;
struct dev_pm_ops {void *suspend,*resume;};
#define SET_RUNTIME_PM_OPS(a,b,c)
struct bus_type {const char *name;int (*match)(struct device *,const struct device_driver *);const struct dev_pm_ops *pm;};
struct device_type {void *groups,*uevent;void (*release)(struct device *);};
struct device_node {atomic_int refs;const char *full_name;struct device_node *child;};
struct device_driver {const char *name;struct bus_type *bus;int (*probe)(struct device *);int (*remove)(struct device *);void (*shutdown)(struct device *);};
struct device {struct device_node *of_node;struct device_driver *driver;struct bus_type *bus;struct device_type *type;struct device *parent;const char *init_name;char name[96];void *data;atomic_int refs;bool added;void (*release)(struct device *);struct list_head children,sibling;};
static struct device_driver *registered_driver;
static void device_initialize(struct device *d){atomic_store(&d->refs,1);INIT_LIST_HEAD(&d->children);INIT_LIST_HEAD(&d->sibling);}
static struct device *get_device(struct device *d){if(d)assert(atomic_fetch_add(&d->refs,1)>0);return d;}
static void put_device(struct device *d){if(atomic_fetch_sub(&d->refs,1)==1){assert(!d->added);if(d->type&&d->type->release)d->type->release(d);else if(d->release)d->release(d);else abort();}}
static int dev_set_name(struct device *d,const char *fmt,...){if(fault())return -ENOMEM;va_list a;va_start(a,fmt);vsnprintf(d->name,sizeof(d->name),fmt,a);va_end(a);return 0;}
static const char *dev_name(struct device *d){return d->name;}
static int device_add(struct device *d){if(fault())return -EIO;d->added=true;if(d->parent){get_device(d->parent);list_add_tail(&d->sibling,&d->parent->children);}if(registered_driver&&d->bus&&d->bus->match(d,registered_driver)){d->driver=registered_driver;if(d->driver->probe(d))d->driver=NULL;}return 0;}
static int device_register(struct device *d){device_initialize(d);return device_add(d);}
static void device_unregister(struct device *d){assert(d->added);if(d->driver&&d->driver->remove)d->driver->remove(d);d->driver=NULL;d->added=false;if(d->parent){list_del_init(&d->sibling);put_device(d->parent);}put_device(d);}
static int device_for_each_child(struct device *d,void *data,int (*f)(struct device *,void *)){while(!list_empty(&d->children)){struct device *c=container_of(d->children.next,struct device,sibling);int r=f(c,data);if(r)return r;}return 0;}
static void *dev_get_drvdata(const struct device *d){return d->data;}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static int driver_register(struct device_driver *d){assert(!registered_driver);registered_driver=d;return 0;}
static void driver_unregister(struct device_driver *d){assert(registered_driver==d);registered_driver=NULL;}
static int bus_live;
static int bus_register(struct bus_type *b){(void)b;if(fault())return -EIO;assert(!bus_live);bus_live=1;return 0;}
static void bus_unregister(struct bus_type *b){(void)b;assert(bus_live);bus_live=0;}
struct idr {void *slots[16];};
#define DEFINE_IDR(n) struct idr n
static int idr_alloc(struct idr *i,void *p,int begin,int end,int f){(void)f;if(fault())return -ENOMEM;assert(begin>=0&&end==begin+1&&end<=16);if(i->slots[begin])return -ENOSPC;i->slots[begin]=p;return begin;}
static void *idr_find(struct idr *i,int id){assert(id>=0&&id<16);return i->slots[id];}
static void idr_remove(struct idr *i,int id){assert(id>=0&&id<16);i->slots[id]=NULL;}
static int of_alias_get_id(struct device_node *n,const char *a){(void)n;assert(!strcmp(a,"swr"));return -1;}
static struct device_node *of_node_get(struct device_node *n){if(n)atomic_fetch_add(&n->refs,1);return n;}
static void of_node_put(struct device_node *n){if(n)assert(atomic_fetch_sub(&n->refs,1)>0);}
#define for_each_available_child_of_node(parent,node) for(node=of_node_get((parent)->child);node;of_node_put(node),node=NULL)
static int of_alias_from_compatible(struct device_node *n,char *buf,size_t size){(void)n;snprintf(buf,size,"codec");return 0;}
static int of_property_read_u64(struct device_node *n,const char *p,u64 *v){(void)n;assert(!strcmp(p,"reg"));*v=1;return 0;}
static int strscpy(char *dst,const char *src,size_t n){snprintf(dst,n,"%s",src);return strlen(dst);}
struct regmap;struct regmap_config;struct lock_class_key;struct irq_domain;
/* HEADERS */
/* SOURCE */
static struct swr_master master;
static struct swr_device *slave;
static struct device_node node={.refs=1,.full_name="codec"};
static unsigned releases,probes,removes,writes,reads,port_calls,events;
static int callback_mode,probe_fail;
static struct swr_params recorded;
static atomic_bool callback_entered,callback_release,unregister_done;
static void master_release(struct swr_master *m){assert(m==&master);releases++;}
static int port_callback(struct swr_master *m,struct swr_params *t){port_calls++;recorded=*t;if(callback_mode==1)return -EIO;if(callback_mode==2)return 0;swr_port_response(m,t->tid);assert(t->dev_num==recorded.dev_num&&!memcmp(t,&recorded,sizeof(*t)));return callback_mode==3?-EREMOTEIO:0;}
static int read_callback(struct swr_master *m,u8 n,u16 reg,void *p,u32 count){(void)m;(void)n;(void)reg;reads++;memset(p,7,count);return 0;}
static int write_callback(struct swr_master *m,u8 n,u16 reg,const void *p){(void)m;(void)n;(void)reg;assert(p);writes++;if(callback_mode==4){atomic_store(&callback_entered,true);while(!atomic_load(&callback_release))usleep(1000);}return callback_mode==5?-EIO:0;}
static int slave_event(struct swr_device *d){assert(d==slave||!slave);events++;if(callback_mode==6){atomic_store(&callback_entered,true);while(!atomic_load(&callback_release))usleep(1000);}return 0;}
static int slave_probe(struct swr_device *d){probes++;swr_set_dev_data(d,(void *)1);assert(!swr_device_up(d));return probe_fail?-EIO:0;}
static int slave_remove(struct swr_device *d){removes++;assert(!atomic_read(&d->driver_calls));swr_remove_device(d);return 0;}
static struct swr_driver driver={.probe=slave_probe,.remove=slave_remove,.device_up=slave_event,.device_down=slave_event,.reset_device=slave_event,.driver.name="codec"};
static void begin(void){assert(!swr_driver_register(&driver));master.connect_port=master.disconnect_port=port_callback;master.read=read_callback;master.write=write_callback;master.release=master_release;assert(!swr_register_master(&master));swr_master_add_boarddevices(&master);struct swr_boardinfo b={.name="codec",.addr=42,.of_node=&node};slave=swr_new_device(&master,&b);assert(slave&&slave->dev.driver&&node.refs==2);}
static void end(void){swr_unregister_master(&master);slave=NULL;swr_driver_unregister(&driver);assert(!live&&releases==1&&node.refs==1&&list_empty(&swr_master_list)&&!master_idr.slots[0]);}
static int request(bool connect,unsigned count){u8 p[14],mask[14],ch[14],type[14];u32 rate[14];for(unsigned i=0;i<14;i++){p[i]=i+1;mask[i]=3;ch[i]=2;type[i]=i+20;rate[i]=48000+i;}return connect?swr_connect_port(slave,p,count,mask,rate,ch,type):swr_disconnect_port(slave,p,count,mask,type);}
static unsigned lifecycle(void){begin();struct swr_device *held=get_matching_swr_slave_device(&node);assert(held==slave);swr_unregister_master(&master);assert(!releases&&node.refs==2&&live==1);assert(swr_write(held,1,0,"x")==-ENODEV);put_device(&held->dev);assert(releases==1&&node.refs==1&&!live);swr_driver_unregister(&driver);return 5;}
static unsigned ports(void){begin();slave->dev_num=3;for(unsigned n=1;n<=14;n++){assert(!request(true,n));assert(recorded.dev_num==3&&recorded.num_port==n);for(unsigned i=0;i<n;i++)assert(recorded.port_id[i]==i+1&&recorded.ch_en[i]==3&&recorded.num_ch[i]==2&&recorded.ch_rate[i]==48000+i&&recorded.port_type[i]==i+20);assert(!request(false,n));}callback_mode=1;assert(request(true,1)==-EIO);callback_mode=3;assert(request(false,1)==-EREMOTEIO);assert(live==2);swr_port_response(&master,255);swr_port_response(&master,0);assert(swr_connect_port(NULL,NULL,1,NULL,NULL,NULL,NULL)==-EINVAL);assert(request(true,0)==-EINVAL&&request(true,15)==-EINVAL);master.connect_port=NULL;assert(request(true,1)==-EOPNOTSUPP);end();return 35;}
static unsigned exhaustion(void){begin();callback_mode=2;for(unsigned i=0;i<255;i++)assert(!request(true,1));assert(request(true,1)==-ENOMEM);assert(live==257);for(unsigned i=0;i<255;i+=2)swr_port_response(&master,i);assert(!request(false,1));end();return 258;}
static unsigned faults(void){unsigned count=0;for(int i=1;i<=4;i++){memset(&master,0,sizeof(master));master.release=master_release;steps=0;fail_step=i;assert(swr_register_master(&master)<0&&!live&&!master_idr.slots[0]);count++;}fail_step=0;steps=0;memset(&master,0,sizeof(master));master.release=master_release;assert(!swr_register_master(&master));for(int i=1;i<=3;i++){struct swr_boardinfo b={.name="codec",.of_node=&node};steps=0;fail_step=i;assert(!swr_new_device(&master,&b));assert(live==1&&node.refs==1&&master.dev.refs==1&&list_empty(&master.devices));count++;}fail_step=0;swr_unregister_master(&master);assert(!live);return count;}
static unsigned groups(void){begin();u8 b=0;assert(!swr_set_device_group(slave,SWR_GROUP_12));assert(!swr_write(slave,1,2,&b)&&writes==1);assert(!swr_write(slave,2,2,&b)&&writes==1);assert(!swr_set_device_group(slave,SWR_GROUP_NONE));assert(!swr_write(slave,2,2,&b)&&writes==2);assert(swr_set_device_group(slave,9)==-EINVAL);assert(swr_read(NULL,0,0,&b,1)==-EINVAL);assert(swr_read(slave,0,65535,&b,2)==-EINVAL);assert(swr_bulk_write(slave,1,&b,&b,1)==-EOPNOTSUPP);end();return 8;}
static void *write_thread(void *p){(void)p;assert(!swr_write(slave,1,2,"x"));return NULL;}
static void *unregister_thread(void *p){(void)p;swr_unregister_master(&master);atomic_store(&unregister_done,true);return NULL;}
static void *event_thread(void *p){(void)p;assert(!swr_device_up(slave));return NULL;}
static void *remove_thread(void *p){(void)p;swr_drv_remove(&slave->dev);atomic_store(&unregister_done,true);return NULL;}
static unsigned drain(void){begin();callback_mode=4;pthread_t t,u;assert(!pthread_create(&t,NULL,write_thread,NULL));while(!atomic_load(&callback_entered))usleep(1000);assert(!pthread_create(&u,NULL,unregister_thread,NULL));usleep(20000);assert(!atomic_load(&unregister_done));atomic_store(&callback_release,true);assert(!pthread_join(t,NULL)&&!pthread_join(u,NULL)&&!live&&releases==1);swr_driver_unregister(&driver);return 1;}
static unsigned driver_drain(void){begin();callback_mode=6;pthread_t t,u;assert(!pthread_create(&t,NULL,event_thread,NULL));while(!atomic_load(&callback_entered))usleep(1000);assert(!pthread_create(&u,NULL,remove_thread,NULL));usleep(20000);assert(!atomic_load(&unregister_done));atomic_store(&callback_release,true);assert(!pthread_join(t,NULL)&&!pthread_join(u,NULL));unsigned prior=events;assert(!swr_device_up(slave)&&events==prior);slave->dev.driver=NULL;end();return 2;}
static unsigned init_faults(void){for(int i=1;i<=2;i++){steps=0;fail_step=i;assert(soundwire_init()<0&&!bus_live);}steps=0;fail_step=0;assert(!soundwire_init());soundwire_exit();assert(!bus_live);return 3;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;
#define CASE(name) if(!strcmp(argv[1],#name))n=name();else
CASE(lifecycle) CASE(ports) CASE(exhaustion) CASE(faults) CASE(groups) CASE(drain) CASE(driver_drain) CASE(init_faults)
abort();printf("{\"swr_%s_cases\":%u}\n",argv[1],n);}
