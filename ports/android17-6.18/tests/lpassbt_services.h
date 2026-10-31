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
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define __init
#define __exit
#define __iomem
#define IS_ENABLED(x) 1
#define BIT(n) (1UL<<(n))
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef uint32_t u32,__be32;
typedef unsigned long irq_hw_number_t;
typedef int(*irq_handler_t)(int,void*);
struct property {const char*name;int count;u32 data[20];};
struct device_node {struct property props[12];int n,refs;const char*full_name;const char*name;struct device_node *child,*next;};
struct resource {void*ptr;void(*release)(void*);};
struct device {struct device_node*of_node;void*data;int refs;struct resource resources[64];int nr,group;struct device*parent;bool runtime_enabled,runtime_active;int pm_refs;};
struct platform_device {struct device dev;void*pdata;bool registered;};
static int fail_step,step,live;
static bool fail(void){return fail_step&&++step==fail_step;}
static void *alloc(size_t n){if(fail())return NULL;void*p=calloc(1,n);if(p)live++;return p;}
static void kfree(void*p){if(p){live--;free(p);}}
#define kzalloc(n,g) alloc(n)
static void add_resource(struct device*d,void*p,void(*release)(void*)){assert(d->nr<64);d->resources[d->nr++]=(struct resource){p,release};}
static void cleanup_from(struct device*d,int from){while(d->nr>from){struct resource*r=&d->resources[--d->nr];r->release(r->ptr);}}
static void *devm_kcalloc(struct device*d,size_t n,size_t s,int g){(void)g;void*p=alloc(n*s);if(p)add_resource(d,p,kfree);return p;}
#define devm_kzalloc(d,n,g) devm_kcalloc(d,1,n,g)
static void devm_kfree(struct device*d,void*p){for(int i=0;i<d->nr;i++)if(d->resources[i].ptr==p){kfree(p);memmove(&d->resources[i],&d->resources[i+1],(--d->nr-i)*sizeof(d->resources[0]));return;}assert(!p);}
static int devm_add_action_or_reset(struct device*d,void(*fn)(void*),void*p){if(fail()){fn(p);return -ENOMEM;}add_resource(d,p,fn);return 0;}
static void devm_remove_action(struct device*d,void(*fn)(void*),void*p){for(int i=0;i<d->nr;i++)if(d->resources[i].ptr==p&&d->resources[i].release==fn){memmove(&d->resources[i],&d->resources[i+1],(--d->nr-i)*sizeof(d->resources[0]));return;}abort();}
static void *devres_open_group(struct device*d,void*p,int g){(void)p;(void)g;if(fail())return NULL;d->group=d->nr;return d;}
static void devres_remove_group(struct device*d,void*p){assert(p==d);d->group=-1;}
static int devres_release_group(struct device*d,void*p){assert(p==d);cleanup_from(d,d->group);d->group=-1;return 0;}
static void *dev_get_drvdata(struct device*d){return d->data;}
static void dev_set_drvdata(struct device*d,void*p){d->data=p;}
static void put_device(struct device*d){assert(d->refs>0);d->refs--;}
static struct platform_device *found;
static struct platform_device *of_find_device_by_node(struct device_node*np){if(!found||np!=found->dev.of_node)return NULL;found->dev.refs++;return found;}
struct mutex {pthread_mutex_t lock;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
static void mutex_lock(struct mutex*m){pthread_mutex_lock(&m->lock);}
static void mutex_unlock(struct mutex*m){pthread_mutex_unlock(&m->lock);}
static struct property *property(struct device_node*np,const char*name){for(int i=0;i<np->n;i++)if(!strcmp(np->props[i].name,name))return &np->props[i];return NULL;}
static bool of_property_present(struct device_node*np,const char*n){return property(np,n)!=NULL;}
static bool of_property_read_bool(struct device_node*np,const char*n){return of_property_present(np,n);}
static int of_property_count_u32_elems(struct device_node*np,const char*n){struct property*p=property(np,n);return p?p->count:-EINVAL;}
static int of_property_read_u32_array(struct device_node*np,const char*n,u32*out,int count){struct property*p=property(np,n);if(!p||p->count<count)return -EINVAL;assert(count<=16);memcpy(out,p->data,count*sizeof(u32));return 0;}
static int of_property_read_u32(struct device_node*np,const char*n,u32*out){return of_property_read_u32_array(np,n,out,1);}
static struct device_node *of_parse_phandle(struct device_node*np,const char*n,int index){(void)index;if(!of_property_present(np,n))return NULL;np->refs++;return np;}
static void of_node_put(struct device_node*np){if(np){assert(np->refs>0);np->refs--;}}
static const __be32 *of_get_property(struct device_node*np,const char*n,int*len){struct property*p=property(np,n);if(!p)return NULL;*len=p->count*sizeof(u32);return p->data;}
static u32 be32_to_cpup(const u32*p){return *p;}
static int of_property_count_strings(struct device_node*np,const char*n){(void)np;(void)n;return -EINVAL;}
static int of_property_read_string_index(struct device_node*np,const char*n,int index,const char**out){(void)np;(void)n;(void)index;(void)out;return -EINVAL;}
static void prop(struct device_node*np,const char*n,int count,u32 value){struct property*p=&np->props[np->n++];*p=(struct property){.name=n,.count=count};for(int i=0;i<20;i++)p->data[i]=value;}
static void clean(struct device*d){cleanup_from(d,0);assert(!live&&!d->refs);}
