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

#define U32_MAX UINT32_MAX
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
/* HEADERS */
struct regmap {int exists;};
struct regmap_config {int reg_bits,val_bits,pad_bits;};
struct regmap_bus {int (*write)(void *,const void *,size_t);int (*gather_write)(void *,const void *,size_t,const void *,size_t);int (*read)(void *,const void *,size_t,void *,size_t);int reg_format_endian_default,val_format_endian_default;};
#define REGMAP_ENDIAN_NATIVE 0
static struct regmap map;
static bool map_missing;
static struct regmap *dev_get_regmap(struct device *d,const char *n){assert(d&&!n);return map_missing?NULL:&map;}
static struct regmap *__regmap_init(struct device *d,struct regmap_bus *b,struct device *c,const struct regmap_config *g,struct lock_class_key *k,const char *n){(void)b;(void)g;(void)k;(void)n;assert(d==c);return &map;}
#define __devm_regmap_init __regmap_init
static struct swr_device slave;
static unsigned calls,read_calls,bulk_calls,reset_call,fail_call;
struct transfer {u8 dev;u16 addr;u8 value;};
static struct transfer transfers[10000];
int swr_write(struct swr_device *s,u8 d,u16 a,const void *v){assert(s==&slave&&calls<10000);transfers[calls++]=(struct transfer){d,a,*(const u8 *)v};if(calls==reset_call)atomic_inc(&s->scp_generation);return calls==fail_call?-EREMOTEIO:0;}
int swr_read(struct swr_device *s,u8 d,u16 a,void *v,u32 n){assert(s==&slave&&d==s->dev_num&&n&&n<=0x10000U-a);read_calls++;memset(v,0x5a,n);return read_calls==fail_call?-EIO:0;}
int swr_bulk_write(struct swr_device *s,u8 d,void *r,const void *v,size_t n){assert(s==&slave);bulk_calls++;for(size_t i=0;i<n;i++){int ret=swr_write(s,d,((u16 *)r)[i],(const u8 *)v+i);if(ret)return ret;}return 0;}
/* SOURCE */
static void reset(void){memset(&slave,0,sizeof(slave));slave.dev_num=3;calls=read_calls=bulk_calls=fail_call=reset_call=0;}
static unsigned bounds(void){reset();u8 buf[16]={0};for(unsigned page=0;page<2;page++){slave.paging_support=page;unsigned size=page?4:2;for(unsigned n=0;n<size+1;n++){u8 *short_buf=calloc(n?n:1,1);assert(short_buf);assert(regmap_swr_write(&slave.dev,short_buf,n)==-EINVAL);free(short_buf);}for(unsigned n=size+2;n<2*(size+1);n++)assert(regmap_swr_write(&slave.dev,buf,n)==-EINVAL);}u16 end=65535;assert(regmap_swr_gather_write(&slave.dev,&end,2,buf,2)==-EINVAL);assert(regmap_swr_read(&slave.dev,&end,2,buf,2)==-EINVAL);u32 high=UINT32_MAX;assert(regmap_swr_gather_write(&slave.dev,&high,4,buf,2)==-EINVAL);assert(regmap_swr_read(&slave.dev,&high,4,buf,2)==-EINVAL);assert(!calls&&!read_calls);assert(regmap_swr_write(NULL,buf,3)==-EINVAL);assert(regmap_swr_gather_write(&slave.dev,buf,3,buf,1)==-EINVAL);return 20;}
static unsigned unaligned(void){reset();u8 raw[32]={0};u8 *p=raw+1;u16 a=0x1234,b=0x5678;memcpy(p,&a,2);p[2]=0x42;memcpy(p+3,&b,2);p[5]=0x77;assert(!regmap_swr_write(&slave.dev,p,6)&&bulk_calls==1&&calls==2);assert(transfers[0].addr==a&&transfers[0].value==0x42&&transfers[1].addr==b&&transfers[1].value==0x77);calls=0;assert(!regmap_swr_gather_write(&slave.dev,p,2,p+2,2)&&calls==2&&transfers[1].addr==a+1);return 3;}
static unsigned paging(void){reset();slave.paging_support=true;u8 p[11]={0};u32 a=0x12345678,b=0x23456789;memcpy(p+1,&a,4);p[5]=0x42;memcpy(p+6,&b,4);p[10]=0x77;assert(!regmap_swr_write(&slave.dev,p+1,10)&&calls==6&&!bulk_calls);assert(transfers[0].addr==0x48&&transfers[0].value==((a>>23)&255));assert(transfers[1].addr==0x49&&transfers[1].value==((a>>15)&255));assert(transfers[2].addr==(u16)(a|0x8000)&&transfers[2].value==0x42);assert(transfers[3].addr==0x48&&transfers[3].value==((b>>23)&255));assert(transfers[4].addr==0x49&&transfers[4].value==((b>>15)&255));assert(transfers[5].addr==(u16)(b|0x8000)&&transfers[5].value==0x77);calls=0;assert(!regmap_swr_write(&slave.dev,p+6,5)&&calls==1);atomic_inc(&slave.scp_generation);calls=0;assert(!regmap_swr_write(&slave.dev,p+6,5)&&calls==3);return 5;}
static unsigned page_faults(void){unsigned cases=0;for(unsigned fail=1;fail<=3;fail++){reset();slave.paging_support=true;u32 a=0x12345678;u8 v=1;fail_call=fail;assert(regmap_swr_gather_write(&slave.dev,&a,4,&v,1)==-EREMOTEIO);fail_call=0;unsigned before=calls;assert(!regmap_swr_gather_write(&slave.dev,&a,4,&v,1));assert(calls-before==(fail==1?3:fail==2?2:1));cases++;}reset();slave.paging_support=true;u32 a=0x12345678;u8 v=1;reset_call=1;assert(regmap_swr_gather_write(&slave.dev,&a,4,&v,1)==-EAGAIN&&calls==2);reset_call=0;calls=0;assert(!regmap_swr_gather_write(&slave.dev,&a,4,&v,1)&&calls==3);return cases+2;}
static unsigned page_crossing(void){reset();slave.paging_support=true;u32 a=0x12347fff;u8 v[2]={1,2};assert(!regmap_swr_gather_write(&slave.dev,&a,4,v,2));assert(calls==5&&transfers[2].addr==0xffff&&transfers[4].addr==0x8000);calls=0;assert(!regmap_swr_read(&slave.dev,&a,4,v,2)&&read_calls==2&&v[0]==0x5a&&v[1]==0x5a);return 2;}
static unsigned allocations(void){reset();u8 p[6]={1,2,3,4,5,6};for(unsigned f=1;f<=2;f++){steps=0;fail_step=f;assert(regmap_swr_write(&slave.dev,p,6)==-ENOMEM&&!live&&!calls);}fail_step=0;assert(!regmap_swr_write(&slave.dev,p,6)&&!live);map_missing=true;assert(regmap_swr_write(&slave.dev,p,6)==-EINVAL);return 4;}
static unsigned register_edges(void){reset();u16 a=65535;u8 v;assert(!regmap_swr_read(&slave.dev,&a,2,&v,1)&&v==0x5a);assert(!regmap_swr_gather_write(&slave.dev,&a,2,&v,1));u32 b=0x8000;slave.paging_support=true;calls=0;assert(!regmap_swr_gather_write(&slave.dev,&b,4,&v,1)&&calls==3&&transfers[1].value==1);b=0x10;calls=0;assert(!regmap_swr_gather_write(&slave.dev,&b,4,&v,1)&&calls==1&&transfers[0].addr==0x10);return 4;}
static unsigned config_widths(void){reset();struct regmap_config c={.reg_bits=16,.val_bits=8};assert(__regmap_init_swr(&slave,&c,NULL,NULL)==&map);slave.paging_support=true;assert(__regmap_init_swr(&slave,&c,NULL,NULL)==ERR_PTR(-EINVAL));c.reg_bits=32;assert(__devm_regmap_init_swr(&slave,&c,NULL,NULL)==&map);c.val_bits=16;assert(__devm_regmap_init_swr(&slave,&c,NULL,NULL)==ERR_PTR(-EINVAL));c.val_bits=8;c.pad_bits=8;assert(__regmap_init_swr(&slave,&c,NULL,NULL)==ERR_PTR(-EINVAL));return 5;}
int main(int argc,char **argv){assert(argc==2);unsigned n=0;
#define CASE(name) if(!strcmp(argv[1],#name))n=name();else
CASE(bounds) CASE(unaligned) CASE(paging) CASE(page_faults) CASE(page_crossing) CASE(allocations) CASE(register_edges) CASE(config_widths)
abort();printf("{\"swr_regmap_%s_cases\":%u}\n",argv[1],n);}
