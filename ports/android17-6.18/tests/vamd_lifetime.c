#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <elf.h>
#define MAX_OWNER_STRING 32
#define MAX_NAME_LENGTH 12
#define GFP_KERNEL 0
#define KOBJ_ADD 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define smp_load_acquire(p) (*(p))
#define smp_store_release(p,v) (*(p)=(v))
#define DMA_BIT_MASK(n) UINT64_MAX
#define pr_err(...) ((void)0)
#define dev_err(...) ((void)0)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define to_va_md_s_data(p) container_of(p,struct va_md_s_data,s_kobj)
typedef uint64_t phys_addr_t;
typedef uint64_t dma_addr_t;
typedef uint32_t u32;
typedef uint64_t u64;
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *n) {n->next=n->prev=n;}
static bool list_empty(const struct list_head *n) {return n->next==n;}
static void list_add_tail(struct list_head *n,struct list_head *h) {n->next=h;n->prev=h->prev;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n) {n->next->prev=n->prev;n->prev->next=n->next;}
#define list_add_tail_rcu list_add_tail
#define list_del_rcu list_del
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of(p->m.next,__typeof__(*t),m);&p->m!=(h);p=t,t=container_of(t->m.next,__typeof__(*t),m))
struct notifier_block {int (*notifier_call)(struct notifier_block*,unsigned long,void*);int priority;struct notifier_block *next;};
struct atomic_notifier_head {struct notifier_block *head;};
struct kobject;
struct kobj_type {void (*release)(struct kobject*);};
struct kset;
struct kobject {int refs;bool registered,sysfs;char *name;struct kset *kset;const struct kobj_type *type;};
struct kset {struct kobject kobj;};
/* STRUCTURES */
struct mutex {bool held;};
static struct mutex va_md_lock;
static void mutex_lock(struct mutex *m) {assert(!m->held);m->held=true;}
static void mutex_unlock(struct mutex *m) {assert(m->held);m->held=false;}
static unsigned steps,fail_step,live,sync_calls;
static bool dma_owned,reserved,base_enabled=true;
static unsigned panic_count,subsystem_count;
static struct kobject *objects[32],kernel_obj;
static struct kobject *kernel_kobj=&kernel_obj;
static struct atomic_notifier_head panic_notifier_list;
static struct notifier_block qcom_va_md_panic_blk,qcom_va_md_elf_panic_blk;
static struct {int unused;} va_md_s_attr_group;
static void va_md_kobj_release(struct kobject *kobj);
static const struct kobj_type va_md_kobj_type={va_md_kobj_release};
static bool failure(void) {return ++steps==fail_step;}
static void *kzalloc(size_t n,int flags) {(void)flags;if(failure())return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void kfree(void *p) {if(p){assert(live);live--;free(p);}}
static bool msm_minidump_enabled(void) {return base_enabled;}
static void synchronize_rcu(void) {sync_calls++;}
static void ATOMIC_INIT_NOTIFIER_HEAD(struct atomic_notifier_head *h) {h->head=NULL;}
static int atomic_notifier_chain_register(struct atomic_notifier_head *h,struct notifier_block *nb) {
 if(failure())return -EIO;
 assert(!nb->next);nb->next=h->head;h->head=nb;
 if(h==&panic_notifier_list){assert(!va_md_data.va_md_init);panic_count++;}else subsystem_count++;
 return 0;
}
static int atomic_notifier_chain_unregister(struct atomic_notifier_head *h,struct notifier_block *nb) {
 struct notifier_block **p=&h->head;while(*p&&*p!=nb)p=&(*p)->next;assert(*p==nb);*p=nb->next;nb->next=NULL;synchronize_rcu();
 if(h==&panic_notifier_list){assert(!va_md_data.va_md_init);assert(panic_count);panic_count--;}else{assert(subsystem_count);subsystem_count--;}
 return 0;
}
static int kobject_init_and_add(struct kobject *o,const struct kobj_type *type,struct kobject *parent,const char *fmt,...) {
 (void)parent;o->refs=1;o->type=type;assert(!strcmp(fmt,"%s"));
 if(failure())return -ENOMEM;
 va_list ap;va_start(ap,fmt);const char *name=va_arg(ap,const char*);o->name=strdup(name);va_end(ap);assert(o->name);
 for(int i=0;i<32;i++)if(!objects[i]){objects[i]=o;o->registered=true;return 0;}
 abort();
}
static void kobject_del(struct kobject *o) {o->registered=false;}
static void kobject_put(struct kobject *o) {
 assert(o->refs>0);if(--o->refs)return;
 for(int i=0;i<32;i++)if(objects[i]==o)objects[i]=NULL;
 free(o->name);o->name=NULL;o->type->release(o);
}
static struct kobject *kset_find_obj(struct kset *ks,const char *name) {
 for(int i=0;i<32;i++)if(objects[i]&&objects[i]->registered&&objects[i]->kset==ks&&!strcmp(objects[i]->name,name)){objects[i]->refs++;return objects[i];}
 return NULL;
}
static int sysfs_create_group(struct kobject *o,const void *g) {(void)g;if(failure())return -ENOMEM;assert(o->registered);o->sysfs=true;return 0;}
static void sysfs_remove_group(struct kobject *o,const void *g) {(void)g;assert(o->sysfs);o->sysfs=false;}
static int kobject_uevent(struct kobject *o,int evt) {(void)evt;assert(o->sysfs&&!list_empty(&va_md_data.va_md_list));return 0;}
static struct kset *kset_create_and_add(const char *n,void *ops,struct kobject *parent) {(void)n;(void)ops;(void)parent;return kzalloc(sizeof(struct kset),0);}
static void kset_unregister(struct kset *ks) {assert(!panic_count);assert(list_empty(&va_md_data.va_md_list));kfree(ks);}
struct device_node {int refs;};
struct device {struct device_node *of_node;};
struct platform_device {struct device dev;void *data;};
struct reserved_mem {uint64_t size;};
static struct reserved_mem rmem={4096};
static struct device_node memory_node={1};
static struct device_node *of_parse_phandle(struct device_node *np,const char *prop,int idx) {(void)np;(void)prop;(void)idx;if(failure())return NULL;memory_node.refs++;return &memory_node;}
static struct reserved_mem *of_reserved_mem_lookup(struct device_node *np) {assert(np==&memory_node);return failure()?NULL:&rmem;}
static void of_node_put(struct device_node *np) {assert(np==&memory_node&&np->refs==2);np->refs--;}
static int of_reserved_mem_device_init_by_idx(struct device *dev,struct device_node *np,int idx) {(void)dev;(void)np;(void)idx;if(failure())return -EIO;assert(!reserved);reserved=true;return 0;}
static void of_reserved_mem_device_release(struct device *dev) {(void)dev;assert(reserved&&!dma_owned);reserved=false;}
static int dma_set_mask_and_coherent(struct device *dev,uint64_t mask) {(void)dev;(void)mask;return failure()?-EIO:0;}
static void *dma_alloc_coherent(struct device *dev,size_t n,dma_addr_t *handle,int flags) {(void)dev;void *p=kzalloc(n,flags);if(p){assert(!dma_owned);dma_owned=true;*handle=0x10200000;}return p;}
static void dma_free_coherent(struct device *dev,size_t n,void *p,dma_addr_t handle) {(void)dev;assert(n==rmem.size&&p&&dma_owned&&handle==0x10200000);assert(!panic_count&&!subsystem_count);dma_owned=false;kfree(p);}
static phys_addr_t dma_to_phys(struct device *dev,dma_addr_t handle) {(void)dev;return handle;}
static void platform_set_drvdata(struct platform_device *pdev,void *data) {pdev->data=data;}
static bool remove_failure;
static int msm_minidump_remove_region(const struct md_region *r) {(void)r;assert(!panic_count&&!subsystem_count);return remove_failure?-EIO:0;}
/* FUNCTIONS */
static int cb_a(struct notifier_block *nb,unsigned long event,void *ptr) {(void)nb;(void)event;(void)ptr;return 0;}
static int cb_b(struct notifier_block *nb,unsigned long event,void *ptr) {(void)nb;(void)event;(void)ptr;return 0;}
static struct platform_device pdev;
static void probe(void) {steps=fail_step=0;assert(!qcom_va_md_driver_probe(&pdev));assert(steps==8&&va_md_data.va_md_init&&dma_owned&&reserved&&panic_count==2&&live==2&&memory_node.refs==1);}
static void remove_provider(void) {qcom_va_md_driver_remove(&pdev);assert(!va_md_data.va_md_init&&!dma_owned&&!reserved&&!panic_count&&!subsystem_count&&!live&&!pdev.data);}
int main(void) {
 struct notifier_block a={.notifier_call=cb_a,.priority=20,.next=(void*)0x123},b={.notifier_call=cb_b,.priority=3};
 for(unsigned f=1;f<=8;f++){steps=0;fail_step=f;int ret=qcom_va_md_driver_probe(&pdev);assert(ret<0);assert(!live&&!dma_owned&&!reserved&&!panic_count&&!va_md_data.va_md_init&&!va_md_data.elf_mem&&memory_node.refs==1);}
 probe();assert(qcom_va_md_driver_probe(&pdev)==-EBUSY);unsigned baseline=live;
 for(unsigned f=1;f<=5;f++){steps=0;fail_step=f;int ret=qcom_va_md_register("display",&a);assert(ret<0);assert(live==baseline&&!subsystem_count&&list_empty(&va_md_data.va_md_list));for(int i=0;i<32;i++)assert(!objects[i]);}
 fail_step=steps=0;assert(qcom_va_md_register(NULL,&a)==-EINVAL);assert(qcom_va_md_register("display",NULL)==-EINVAL);assert(qcom_va_md_register("",&a)==-EINVAL);assert(qcom_va_md_register("a/b",&a)==-EINVAL);
 assert(!qcom_va_md_register("display%literal",&a));assert(qcom_va_md_register("display%literal",&a)==-EEXIST);assert(subsystem_count==1&&live==4);
 struct kobject *open=kset_find_obj(va_md_data.va_md_kset,"display%literal");assert(open&&open->refs==2);assert(!qcom_va_md_register("display%literal",&b)&&live==5&&subsystem_count==2);
 assert(qcom_va_md_unregister("display%literal",&(struct notifier_block){.notifier_call=(void*)0x100})==-EINVAL&&live==5);
 assert(!qcom_va_md_unregister("display%literal",&a)&&subsystem_count==1&&live==4);assert(open->refs==2);
 base_enabled=false;assert(!qcom_va_md_unregister("display%literal",&b));assert(!subsystem_count&&live==3&&!open->registered&&!open->sysfs&&open->refs==1&&list_empty(&va_md_data.va_md_list));assert(qcom_va_md_register("display",&a)==-ENODEV);base_enabled=true;kobject_put(open);assert(live==2);
 assert(!qcom_va_md_register("display%literal",&a));assert(!qcom_va_md_unregister("display%literal",&a));assert(live==2);
 for(int i=0;i<16;i++){char name[24];snprintf(name,sizeof(name),"client%d",i);assert(!qcom_va_md_register(name,&a));}
 assert(subsystem_count==16&&live==34);remove_provider();assert(qcom_va_md_unregister("client0",&a)==-ENODEV);
 probe();va_md_data.va_md_minidump_reg=true;remove_provider();
 probe();va_md_data.va_md_minidump_reg=true;remove_failure=true;qcom_va_md_driver_remove(&pdev);assert(!va_md_data.va_md_init&&live==1&&dma_owned&&reserved&&!panic_count&&!subsystem_count);assert(qcom_va_md_driver_probe(&pdev)==-EBUSY);
 dma_free_coherent(&pdev.dev,va_md_data.total_mem_size,(void*)va_md_data.elf_mem,va_md_data.dma_handle);of_reserved_mem_device_release(&pdev.dev);memset(&va_md_data,0,sizeof(va_md_data));assert(!live);
 printf("{\"probe_failure_stages\":8,\"registration_failure_stages\":5,\"held_sysfs_reference\":true,\"disabled_base_unregister\":true,\"provider_remove_clients\":16,\"advertised_buffer_retained_on_error\":true,\"rcu_drain_calls\":%u}\n",sync_calls);
}
