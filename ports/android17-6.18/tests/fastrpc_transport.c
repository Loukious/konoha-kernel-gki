#include "fastrpc_services.h"
struct mutex {pthread_mutex_t lock;};
static void mutex_init(struct mutex *m){pthread_mutex_init(&m->lock,NULL);}
static void mutex_lock(struct mutex *m){pthread_mutex_lock(&m->lock);}
static void mutex_unlock(struct mutex *m){pthread_mutex_unlock(&m->lock);}
#define init_waitqueue_head(q) (*(q)=1)
#define wait_event(q,cond) do {(void)(q);while(!(cond))usleep(1000);}while(0)
#define smp_store_release(p,n) (*(p)=(n))
#define WRITE_ONCE(p,n) ((p)=(n))
#define kref_init(p) (*(p)=1)
#define idr_init(p) (*(p)=1)
#define ida_init(p) (*(p)=1)
#define cpumask_first(x) 0
#define DMA_BIT_MASK(n) ((1ULL<<(n))-1)
#define dev_info(...) ((void)0)
#define dev_err(...) ((void)0)
struct node {char *label;int vmcount;u32 vmid;};
struct device {struct node *of_node;void *data;u64 *dma_mask,coherent_dma_mask;int refs;};
struct rpmsg_device {struct device dev;void *ept;};
struct miscdevice {struct device *this_device;};
struct wakeup_source {int unused;};
struct fastrpc_device_node {struct miscdevice miscdev;};
struct fastrpc_user {struct list_head user;int sessionid;bool cleaned;};
#include "transport_types.inc"
struct fastrpc_channel_ctx {int domain_id,vmcount;u64 perms;struct {u32 vmid,perm;}vmperms[FASTRPC_MAX_VMIDS];struct gid_list gidlist;atomic_t teardown,exported_bufs;int export_wait,ssr_wait_queue;bool ready,secure,unsigned_support,staticpd_status;u32 qos_latency,lowest_capacity_core_count;int refcount;struct list_head users,gmaps;struct {struct list_head list;}rootheap_bufs;struct mutex wake_mutex,session_mutex;spinlock_t lock;struct {spinlock_t tx_lock,rx_lock;}gmsg_log;int ctx_idr,tgid_frpc_ida;struct completion ssr_complete;int max_sess_per_proc;struct rpmsg_device *rpdev;struct device *dev;u64 cpuinfo_todsp;struct fastrpc_device_node *fdevice,*secure_fdevice;struct {void *pdrhandle;}spd[FASTRPC_MAX_SPD];struct wakeup_source *wake_source,*wake_source_secure;u32 invoke_cnt;};
static struct fastrpc_channel_ctx *published;
static int stage,fail_stage,children,reserved,nodes,handles,wakes,removed,device_puts;static bool ever_ready,scm=true;
static bool fail(void){return ++stage==fail_stage;}
static int of_property_read_string(struct node *n,const char *p,const char **s){assert(!strcmp(p,"label"));*s=n->label;return n->label?0:-EINVAL;}
static int of_property_read_variable_u32_array(struct node *n,const char *p,u32 *out,int min,int max){(void)p;(void)min;(void)max;if(n->vmcount>0)out[0]=n->vmid;return n->vmcount;}
static bool qcom_scm_is_available(void){return scm;}
static bool of_property_read_bool(struct node *n,const char *p){(void)n;(void)p;return false;}
static int of_property_read_u32(struct node *n,const char *p,u32 *out){(void)n;(void)p;*out=10;return 0;}
static int fastrpc_init_privileged_gids(struct device *d,char *p,struct gid_list *l){(void)d;(void)p;l->gids=kzalloc(sizeof(u32),0);l->gidcount=1;return 0;}
static void fastrpc_lowest_capacity_corecount(struct device *d,struct fastrpc_channel_ctx *c){(void)d;c->lowest_capacity_core_count=2;}
static void dev_set_drvdata(struct device *d,void *p){d->data=p;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static struct device *get_device(struct device *d){d->refs++;return d;}
static int dma_set_mask_and_coherent(struct device *d,u64 mask){assert(d->dma_mask==&d->coherent_dma_mask && mask==0xffffffff);return fail()?-EIO:0;}
static int of_reserved_mem_device_init_by_idx(struct device *d,struct node *n,int i){(void)d;(void)n;(void)i;reserved=1;return 0;}
static void of_reserved_mem_device_release(struct device *d){(void)d;reserved=0;}
static int of_platform_populate(struct node *n,void *a,void *b,struct device *d){(void)n;(void)a;(void)b;(void)d;children=3;return fail()?-EIO:0;}
static void of_platform_depopulate(struct device *d){struct fastrpc_channel_ctx *c=d->data;if(ever_ready){assert(c->ssr_complete.done && !atomic_read(&c->exported_bufs));}children=0;}
static int fastrpc_device_register(struct device *d,struct fastrpc_channel_ctx *c,bool secure,const char *domain){(void)domain;assert(!c->ready);if(fail())return -EIO;struct fastrpc_device_node *n=kzalloc(sizeof(*n),0);n->miscdev.this_device=d;if(secure)c->secure_fdevice=n;else c->fdevice=n;nodes++;return 0;}
static void misc_deregister(struct miscdevice *m){assert(nodes>0);nodes--;kfree(container_of(m,struct fastrpc_device_node,miscdev));}
static int fastrpc_setup_service_locator(struct fastrpc_channel_ctx *c,char *a,char *b,char *d,int i){(void)a;(void)b;(void)d;c->spd[i].pdrhandle=kzalloc(1,0);handles++;return fail()?-EIO:0;}
static void pdr_handle_release(void *h){assert(handles>0);handles--;kfree(h);}
static void fastrpc_register_wakeup_source(struct device *d,const char *name,struct wakeup_source **out){(void)d;(void)name;*out=kzalloc(sizeof(**out),0);wakes++;}
static void wakeup_source_unregister(struct wakeup_source *s){assert(wakes>0);wakes--;kfree(s);}
static void fastrpc_update_gctx(struct fastrpc_channel_ctx *c,int add){if(add){assert(c->ready);published=c;ever_ready=true;}else if(published==c)published=NULL;}
static void fastrpc_channel_ctx_put(struct fastrpc_channel_ctx *c){assert(c->refcount>0);if(!--c->refcount){assert(!children && !nodes && !handles && !wakes && !reserved && !published);c->dev->refs--;device_puts++;kfree(c->gidlist.gids);kfree(c);}}
static void fastrpc_queue_pd_status(struct fastrpc_user *u,int d,int s,int id){(void)u;(void)d;(void)s;(void)id;}
static void fastrpc_notify_users(struct fastrpc_user *u){(void)u;}
static void fastrpc_ssr_dspsignal_cancel_wait(struct fastrpc_user *u){(void)u;}
static void fastrpc_free_user(struct fastrpc_user *u){assert(published==NULL);u->cleaned=true;}
static int fastrpc_mmap_remove_ssr(struct fastrpc_channel_ctx *c){assert(atomic_read(&c->teardown));return 0;}
#include "transport.inc"
static void reset(void){assert(!allocations && !children && !nodes && !handles && !wakes && !reserved && !published);stage=fail_stage=removed=device_puts=0;ever_ready=false;}
static void *remove_thread(void *p){fastrpc_rpmsg_remove(p);removed=1;return NULL;}
int main(void){int cases=0;for(int domain_id=0;domain_id<4;domain_id++){int count=domain_id==0?6:(domain_id==2?4:domain_id==3?4:3);for(int f=1;f<=count;f++){reset();struct node n={.label=(char *)domains[domain_id],.vmcount=-1};struct rpmsg_device p={.dev.of_node=&n};fail_stage=f;assert(fastrpc_rpmsg_probe(&p)<0 && !p.dev.data && p.dev.refs==0);reset();cases++;}}
 for(int i=0;i<4;i++){reset();struct node n={.label=(char *)domains[i],.vmcount=1,.vmid=3};struct rpmsg_device p={.dev.of_node=&n};assert(!fastrpc_rpmsg_probe(&p) && published);fastrpc_rpmsg_remove(&p);assert(!p.dev.data && !p.dev.refs);reset();cases++;}
 reset();struct node n={.label="cdsp",.vmcount=1,.vmid=64};struct rpmsg_device p={.dev.of_node=&n};assert(fastrpc_rpmsg_probe(&p)==-EINVAL && !allocations);n.vmid=3;scm=false;assert(fastrpc_rpmsg_probe(&p)==-EPROBE_DEFER);scm=true;cases+=2;
 for(int i=0;i<100;i++){reset();n.vmcount=-1;p=(struct rpmsg_device){.dev.of_node=&n};assert(!fastrpc_rpmsg_probe(&p));struct fastrpc_channel_ctx *c=published;c->refcount++;atomic_set(&c->exported_bufs,1);struct fastrpc_user u={0};INIT_LIST_HEAD(&u.user);list_add_tail(&u.user,&c->users);pthread_t t;pthread_create(&t,NULL,remove_thread,&p);wait_for_completion(&c->ssr_complete);assert(u.cleaned && !removed && children && reserved);atomic_set(&c->exported_bufs,0);pthread_join(t,NULL);assert(removed && !children);fastrpc_channel_ctx_put(c);reset();}
 printf("{\"transport_probe_failure_and_remove_scenarios\":%d,\"session_close_before_export_drain_races\":100}\n",cases);
}
