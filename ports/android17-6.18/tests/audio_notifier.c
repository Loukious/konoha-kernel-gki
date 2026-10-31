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
#define __init
#define __exit
#define GFP_KERNEL 0
#define THIS_MODULE 0
#define EPROBE_DEFER 517
#define SERVREG_SERVICE_STATE_DOWN 0x0FFFFFFF
#define SERVREG_SERVICE_STATE_UP 0x1FFFFFFF
#define SERVREG_SERVICE_STATE_EARLY_DOWN 0x2FFFFFFF
#define QCOM_SSR_BEFORE_SHUTDOWN 0
#define QCOM_SSR_AFTER_POWERUP 3
#define NOTIFY_OK 1
#define CONFIG_MSM_QDSP6_PDR 1
#define IS_ERR(p) ((intptr_t)(p)<0&&(intptr_t)(p)>-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_ACQUIRE)
#define smp_load_acquire(p) __atomic_load_n(p,__ATOMIC_ACQUIRE)
#define smp_store_release(p,x) __atomic_store_n(p,x,__ATOMIC_RELEASE)
#define pr_err_ratelimited(...) ((void)0)
#define pr_info(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_info_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define pr_err(...) ((void)0)
enum { AUDIO_NOTIFIER_SERVICE_DOWN, AUDIO_NOTIFIER_SERVICE_UP };
enum { AUDIO_NOTIFIER_SSR_SERVICE, AUDIO_NOTIFIER_PDR_SERVICE, AUDIO_NOTIFIER_MAX_SERVICES };
enum { AUDIO_NOTIFIER_ADSP_DOMAIN, AUDIO_NOTIFIER_MODEM_DOMAIN, AUDIO_NOTIFIER_MAX_DOMAINS };
enum { AUDIO_SSR_DOMAIN_ADSP, AUDIO_SSR_DOMAIN_MODEM };
enum { AUDIO_PDR_DOMAIN_ADSP };
struct audio_notifier_cb_data { int service,domain; };
struct mutex { pthread_mutex_t lock; bool initialized; };
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER,true}
static void mutex_init(struct mutex *m){assert(!m->initialized);assert(!pthread_mutex_init(&m->lock,NULL));m->initialized=true;}
static void mutex_lock(struct mutex *m){assert(m->initialized);assert(!pthread_mutex_lock(&m->lock));}
static void mutex_unlock(struct mutex *m){assert(!pthread_mutex_unlock(&m->lock));}
struct list_head { struct list_head *next,*prev; };
#define LIST_HEAD(n) struct list_head n={&(n),&(n)}
#define INIT_LIST_HEAD(n) ((n)->next=(n)->prev=(n))
#define list_empty(n) ((n)->next==(n))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_entry(p,t,m) container_of(p,t,m)
#define list_for_each_safe(p,n,h) for(p=(h)->next,n=p->next;p!=(h);p=n,n=p->next)
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) for(p=container_of((h)->next,__typeof__(*p),m),n=container_of((p)->m.next,__typeof__(*p),m);&(p)->m!=(h);p=n,n=container_of((n)->m.next,__typeof__(*n),m))
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->next->prev=n->prev;n->prev->next=n->next;}
static atomic_int live, allocations;
static int fail_allocation;
static void *kzalloc(size_t s,int f){(void)f;if(atomic_fetch_add(&allocations,1)+1==fail_allocation)return NULL;void *p=calloc(1,s);assert(p);atomic_fetch_add(&live,1);return p;}
static void *kmalloc(size_t s,int f){return kzalloc(s,f);}
static void kfree(void *p){if(p){free(p);assert(atomic_fetch_sub(&live,1)>0);}}
struct property { const void *value; };
struct device_node { int bytes;uint32_t handle;struct property prop; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev;void *data,*managed; };
typedef uint32_t u32;
typedef uint32_t phandle;
static void *devm_kzalloc(struct device *d,size_t s,int f){struct platform_device *p=container_of(d,struct platform_device,dev);assert(!p->managed);return p->managed=kzalloc(s,f);}
static void reclaim(struct platform_device *p){kfree(p->managed);p->managed=NULL;p->data=NULL;}
static void platform_set_drvdata(struct platform_device *p,void *d){p->data=d;}
static void *platform_get_drvdata(struct platform_device *p){assert(p);return p->data;}
static int of_property_read_u32(struct device_node *n,const char *key,u32 *v){assert(!strcmp(key,"qcom,rproc-handle"));if(!n||n->bytes<4)return -EINVAL;*v=n->handle;return 0;}
static struct property *of_find_property(struct device_node *n,const char *key,int *size){(void)key;if(!n||n->bytes<0)return NULL;*size=n->bytes;n->prop.value=n->bytes>=4?&n->handle:NULL;return &n->prop;}
static uint32_t be32_to_cpup(const void *p){return *(const uint32_t *)p;}
struct rproc { const char *name;atomic_int refs; };
static struct rproc remote={.name="adsp"};
static bool fail_remote;
static struct rproc *rproc_get_by_phandle(phandle handle){assert(handle==42);if(fail_remote)return NULL;atomic_fetch_add(&remote.refs,1);return &remote;}
static void rproc_put(struct rproc *r){assert(r==&remote);assert(atomic_fetch_sub(&r->refs,1)>0);}
static size_t strscpy(char *dst,const char *src,size_t n){size_t l=strnlen(src,n);if(l>=n)l=n-1;memcpy(dst,src,l);dst[l]=0;return l;}
struct notifier_block { int (*notifier_call)(struct notifier_block *,unsigned long,void *);int priority;struct notifier_block *next; };
struct srcu_notifier_head { pthread_rwlock_t lock;struct notifier_block *head;bool initialized; };
static void srcu_init_notifier_head(struct srcu_notifier_head *h){assert(!h->initialized);assert(!pthread_rwlock_init(&h->lock,NULL));h->initialized=true;}
static void srcu_cleanup_notifier_head(struct srcu_notifier_head *h){assert(h->initialized&&!h->head);assert(!pthread_rwlock_destroy(&h->lock));h->initialized=false;}
static int chain_failure;
static int srcu_notifier_chain_register(struct srcu_notifier_head *h,struct notifier_block *n){assert(h->initialized);if(chain_failure)return chain_failure;assert(!pthread_rwlock_wrlock(&h->lock));for(struct notifier_block *p=h->head;p;p=p->next)if(p==n){pthread_rwlock_unlock(&h->lock);return -EEXIST;}n->next=h->head;h->head=n;pthread_rwlock_unlock(&h->lock);return 0;}
static int srcu_notifier_chain_unregister(struct srcu_notifier_head *h,struct notifier_block *n){assert(h->initialized);assert(!pthread_rwlock_wrlock(&h->lock));struct notifier_block **p=&h->head;while(*p&&*p!=n)p=&(*p)->next;int r=*p?0:-ENOENT;if(*p)*p=(*p)->next;pthread_rwlock_unlock(&h->lock);return r;}
static int srcu_notifier_call_chain(struct srcu_notifier_head *h,unsigned long event,void *data){assert(h->initialized);assert(!pthread_rwlock_rdlock(&h->lock));for(struct notifier_block *p=h->head;p;p=p->next)p->notifier_call(p,event,data);pthread_rwlock_unlock(&h->lock);return NOTIFY_OK;}
struct of_device_id { const char *compatible; };
struct platform_driver { struct { const char *name;int owner;const struct of_device_id *of_match_table;bool suppress_bind_attrs; } driver;int (*probe)(struct platform_device *);void (*remove)(struct platform_device *); };
static struct platform_driver *driver;
static int platform_driver_register(struct platform_driver *d){assert(!driver);driver=d;return 0;}
static void platform_driver_unregister(struct platform_driver *d){assert(driver==d);driver=NULL;}
struct provider { bool live;const char *name;struct notifier_block *nb;void (*callback)(int,char *,void *); };
static struct provider pdr,ssr[2];
static bool fail_pdr,fail_ssr,test_release_event;
static void *audio_pdr_service_register(int d,void (*cb)(int,char *,void *)){assert(d==0&&!pdr.live&&cb);if(fail_pdr)return ERR_PTR(-ENOMEM);pdr.live=true;pdr.callback=cb;return &pdr;}
static int audio_pdr_service_deregister(int d){assert(d==0&&pdr.live);if(test_release_event)pdr.callback(SERVREG_SERVICE_STATE_DOWN,NULL,NULL);pdr.live=false;return 0;}
static void *qcom_register_ssr_notifier(const char *name,struct notifier_block *nb){if(fail_ssr)return ERR_PTR(-EIO);int id=!strcmp(name,"modem");assert((id||!strcmp(name,"adsp"))&&!ssr[id].live);ssr[id]=(struct provider){.live=true,.name=name,.nb=nb};return &ssr[id];}
static int qcom_unregister_ssr_notifier(void *handle,struct notifier_block *nb){struct provider *p=handle;assert(p&&!IS_ERR(p)&&p->live&&p->nb==nb);p->live=false;return 0;}
static void *audio_ssr_register(const char *,struct notifier_block *);
static int audio_ssr_deregister(void *,struct notifier_block *);
/* SOURCE */

static atomic_int calls[2], events[2], callback_active, max_callback_active;
static int client_cb(struct notifier_block *nb,unsigned long event,void *data){(void)nb;struct audio_notifier_cb_data *d=data;assert(d&&d->domain>=0&&d->domain<2&&event<=1);int active=atomic_fetch_add(&callback_active,1)+1;if(active>atomic_load(&max_callback_active))atomic_store(&max_callback_active,active);atomic_fetch_add(&calls[d->domain],1);atomic_store(&events[d->domain],event);atomic_fetch_sub(&callback_active,1);return NOTIFY_OK;}
static struct device_node node={.bytes=4,.handle=42};
static struct platform_device device={.dev.of_node=&node};
static struct notifier_block a={.notifier_call=client_cb},b={.notifier_call=client_cb};
static void bind(void){assert(!audio_notify_probe(&device));assert(audio_notifier_probe_status());}
static void unbind(void){audio_notify_remove(&device);reclaim(&device);assert(!audio_notifier_probe_status()&&!adsp_private&&!remote.refs&&!pdr.live&&!ssr[0].live&&!ssr[1].live&&list_empty(&client_list)&&!live);}
static unsigned bounds(void){bind();unsigned cases=0;
 for(int d=-10;d<=10;d++)if(d!=0&&d!=1){assert(audio_notifier_register("test",d,&a)==-EINVAL);cases++;}
 assert(audio_notifier_register(NULL,0,&a)==-EINVAL);assert(audio_notifier_register("",0,&a)==-EINVAL);assert(audio_notifier_register("test",0,NULL)==-EINVAL);
 struct notifier_block no_callback={0};assert(audio_notifier_register("test",0,&no_callback)==-EINVAL);cases+=4;
 char long_name[64];memset(long_name,'x',63);long_name[63]=0;assert(audio_notifier_register(long_name,0,&a)==-ENAMETOOLONG);cases++;
 assert(!audio_notifier_register("same",0,&a));assert(audio_notifier_register("other",1,&a)==-EALREADY);assert(!audio_notifier_register("same",0,&b));assert(service_data[1][0].num_of_clients==2);assert(!audio_notifier_deregister("same"));assert(list_empty(&client_list)&&!pdr.live);cases+=3;unbind();return cases;
}
static unsigned probe_faults(void){unsigned cases=0;
 assert(audio_notifier_register("test",0,&a)==-EPROBE_DEFER);assert(!live);cases++;
 for(int bytes=-1;bytes<4;bytes++){node.bytes=bytes;assert(audio_notify_probe(&device)<0);reclaim(&device);assert(!audio_notifier_probe_status()&&!remote.refs);cases++;}
 node.bytes=4;allocations=0;fail_allocation=1;assert(audio_notify_probe(&device)==-ENOMEM);reclaim(&device);fail_allocation=0;cases++;
 fail_remote=true;assert(audio_notify_probe(&device)==-EPROBE_DEFER);reclaim(&device);fail_remote=false;assert(!remote.refs);cases++;
 bind();struct platform_device other={.dev.of_node=&node};assert(audio_notify_probe(&other)==-EBUSY);reclaim(&other);assert(remote.refs==1&&audio_notifier_probe_status());cases++;unbind();return cases;
}
static unsigned failure_paths(void){bind();unsigned cases=0;
 allocations=0;fail_allocation=1;assert(audio_notifier_register("test",0,&a)==-ENOMEM);fail_allocation=0;cases++;
 fail_pdr=fail_ssr=true;assert(audio_notifier_register("test",0,&a)<0);assert(list_empty(&client_list)&&!pdr.live&&!ssr[0].live);fail_pdr=fail_ssr=false;cases++;
 chain_failure=-ENOMEM;assert(audio_notifier_register("test",0,&a)==-ENOMEM);assert(!pdr.live&&!ssr[0].live&&list_empty(&client_list));chain_failure=0;cases++;
 fail_pdr=true;assert(!audio_notifier_register("test",0,&a));assert(ssr[0].live&&!pdr.live);assert(!audio_notifier_deregister("test"));fail_pdr=false;cases++;
 assert(!audio_notifier_register("modem",1,&a));assert(ssr[1].live&&!ssr[0].live);assert(!audio_notifier_deregister("modem"));cases++;unbind();return cases;
}
static unsigned domains(void){bind();assert(!audio_notifier_register("adsp",0,&a));assert(!audio_notifier_register("modem",1,&b));calls[0]=calls[1]=0;
 audio_notifier_service_cb(SERVREG_SERVICE_STATE_EARLY_DOWN,1,0);assert(calls[0]==1);
 audio_notifier_service_cb(SERVREG_SERVICE_STATE_DOWN,0,1);assert(calls[1]==1);
 audio_notifier_service_cb(SERVREG_SERVICE_STATE_DOWN,1,0);assert(calls[0]==1);
 audio_notifier_service_cb(SERVREG_SERVICE_STATE_EARLY_DOWN,1,0);assert(calls[0]==2);
 audio_notifier_service_cb(SERVREG_SERVICE_STATE_UP,1,0);assert(calls[0]==3&&events[0]==1);
 audio_notifier_service_cb(SERVREG_SERVICE_STATE_DOWN,1,0);assert(calls[0]==4&&events[0]==0);
 assert(!audio_notifier_deregister("adsp"));assert(!audio_notifier_deregister("modem"));unbind();return 6;
}
static unsigned remove_active(void){bind();assert(!audio_notifier_register("adsp",0,&a));assert(!audio_notifier_register("modem",1,&b));audio_notifier_service_cb(SERVREG_SERVICE_STATE_UP,1,0);audio_notifier_service_cb(QCOM_SSR_AFTER_POWERUP,0,1);calls[0]=calls[1]=0;unbind();assert(calls[0]==1&&calls[1]==1&&events[0]==0&&events[1]==0);bind();assert(!audio_notifier_register("again",0,&a));assert(!audio_notifier_deregister("again"));unbind();return 2;}
static unsigned drain(void){bind();assert(!audio_notifier_register("adsp",0,&a));calls[0]=0;test_release_event=true;assert(!audio_notifier_deregister("adsp"));test_release_event=false;assert(calls[0]==0);unbind();return 1;}
static void *event_thread(void *unused){(void)unused;for(int i=0;i<1000;i++)audio_notifier_service_cb(i&1?SERVREG_SERVICE_STATE_UP:SERVREG_SERVICE_STATE_DOWN,1,0);return NULL;}
static void *registry_thread(void *opaque){struct notifier_block n={.notifier_call=client_cb};char name[16];snprintf(name,sizeof(name),"thread%ld",(long)opaque);for(int i=0;i<200;i++){assert(!audio_notifier_register(name,0,&n));assert(!audio_notifier_deregister(name));}return NULL;}
static unsigned concurrent(void){bind();assert(!audio_notifier_register("anchor",0,&a));pthread_t t[4];for(long i=0;i<2;i++)assert(!pthread_create(&t[i],NULL,event_thread,NULL));for(long i=2;i<4;i++)assert(!pthread_create(&t[i],NULL,registry_thread,(void *)i));for(int i=0;i<4;i++)assert(!pthread_join(t[i],NULL));assert(max_callback_active==1);assert(!audio_notifier_deregister("anchor"));unbind();return 2400;}
int main(int argc,char **argv){assert(argc==2);assert(!audio_notifier_init());unsigned cases=0;
 if(!strcmp(argv[1],"bounds"))cases=bounds();else if(!strcmp(argv[1],"probe"))cases=probe_faults();else if(!strcmp(argv[1],"failures"))cases=failure_paths();else if(!strcmp(argv[1],"domains"))cases=domains();else if(!strcmp(argv[1],"remove"))cases=remove_active();else if(!strcmp(argv[1],"drain"))cases=drain();else if(!strcmp(argv[1],"concurrent"))cases=concurrent();else abort();
 audio_notifier_exit();assert(!driver);printf("{\"audio_notifier_%s_cases\":%u}\n",argv[1],cases);
}
