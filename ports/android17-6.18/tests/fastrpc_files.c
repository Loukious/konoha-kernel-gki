#include "fastrpc_services.h"
#include "files_types.inc"
#undef current
struct {int tgid;char comm[16];} task={.tgid=42};
#define current (&task)
struct mutex {bool held,initialized;};static int mutexes;
static void mutex_init(struct mutex *m){assert(!m->initialized);m->initialized=true;mutexes++;}
static void mutex_destroy(struct mutex *m){assert(m->initialized && !m->held);m->initialized=false;mutexes--;}
static void mutex_lock(struct mutex *m){assert(m->initialized && !m->held);m->held=true;}
static void mutex_unlock(struct mutex *m){assert(m->initialized && m->held);m->held=false;}
#define init_waitqueue_head(p) (*(p)=1)
#define wait_event(q,cond) do{(void)(q);assert(cond);}while(0)
#define atomic_add(n,p) atomic_fetch_add(p,n)
#define atomic_sub(n,p) atomic_fetch_sub(p,n)
#define smp_load_acquire(p) (*(p))
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#define pr_err(...) ((void)0)
#define BUG_ON(v) assert(!(v))
#define list_del list_del_init
#define list_for_each_entry_safe(p,tmp,h,m) for(p=list_first_entry(h,__typeof__(*p),m),tmp=container_of(p->m.next,__typeof__(*p),m);&p->m!=(h);p=tmp,tmp=container_of(tmp->m.next,__typeof__(*tmp),m))
struct device {int unused;};struct inode {int unused;};struct file {void *private_data;};
struct fastrpc_device {struct fastrpc_user *fl;bool dev_close;int refs;};
struct dev_pm_qos_request {int active;};
struct fastrpc_notif_rsp {struct list_head notifn;};
struct fastrpc_buf {struct list_head node;struct fastrpc_user *fl;};
struct fastrpc_channel_ctx {spinlock_t lock;struct device *dev;atomic_t teardown;bool ready,secure;struct completion ssr_complete;unsigned invoke_cnt;int ssr_wait_queue,domain_id,lowest_capacity_core_count,refs,tgid_frpc_ida;struct list_head users,gmaps;};
struct fastrpc_device_node {struct fastrpc_channel_ctx *cctx;bool secure;};
struct fastrpc_user {struct fastrpc_channel_ctx *cctx;struct fastrpc_device *device;atomic_t state,dma_invokes;int dma_wait,tgid,tgid_frpc,sessionid,pd_type;bool is_secure_dev,multi_session_support,set_session_info;struct {int user_fd;}config;struct {atomic_t is_attached;}*spd;struct {spinlock_t nqlock;atomic_t notif_queue_count;int notif_wait_queue;}proc_state_notif;struct list_head user,notif_queue,pending,interrupted,maps,mmaps,cached_bufs,fastrpc_drivers;spinlock_t lock,dspsignals_lock;struct mutex remote_map_mutex,map_mutex,signal_create_mutex,pm_qos_mutex;struct dev_pm_qos_request *dev_pm_qos_req;void *sctx,*secsctx;bool qos_request;void *signal_groups[FASTRPC_DSPSIGNAL_NUM_SIGNALS/FASTRPC_DSPSIGNAL_GROUP_SIZE];};
struct {spinlock_t glock;}g_frpc;
static int id_fail,ids,free_calls,allocation_step,allocation_failure;
static void *file_alloc(size_t n,int flags){if(++allocation_step==allocation_failure)return NULL;return kzalloc(n,flags);}
#define kzalloc file_alloc
#define miscdev_to_fdevice(p) ((struct fastrpc_device_node *)(p))
static void fastrpc_channel_ctx_get(struct fastrpc_channel_ctx *c){c->refs++;}
static void fastrpc_channel_ctx_put(struct fastrpc_channel_ctx *c){assert(c->refs>1);c->refs--;}
static void wake_up_all(int *q){(void)q;}
static void wake_up_interruptible(int *q){(void)q;}
static int get_unique_hlos_process_id(struct fastrpc_channel_ctx *c){(void)c;if(id_fail)return -1;ids++;return 17;}
static void ida_free(int *ida,int id){(void)ida;assert(id==17 && ids==1);ids--;}
static void fastrpc_clients_release(struct fastrpc_user *fl){assert(!atomic_read(&fl->dma_invokes));}
static void fastrpc_device_put_locked(struct fastrpc_device *d){assert(!d);}
static int fastrpc_release_current_dsp_process(struct fastrpc_user *fl){return atomic_read(&fl->cctx->teardown)?-EPIPE:0;}
static void fastrpc_free_user(struct fastrpc_user *fl){(void)fl;free_calls++;}
static bool dev_pm_qos_request_active(struct dev_pm_qos_request *q){return q->active;}
static void dev_pm_qos_remove_request(struct dev_pm_qos_request *q){q->active=0;}
static void fastrpc_pm_relax(struct fastrpc_user *fl,bool secure){(void)fl;(void)secure;}
static void fastrpc_session_free(struct fastrpc_channel_ctx *c,void *s){(void)c;(void)s;}
#include "files.inc"
int main(void){spin_lock_init(&g_frpc.glock);struct fastrpc_channel_ctx c={.refs=1,.lowest_capacity_core_count=2,.ready=true};spin_lock_init(&c.lock);init_completion(&c.ssr_complete);INIT_LIST_HEAD(&c.users);INIT_LIST_HEAD(&c.gmaps);struct fastrpc_device_node n={.cctx=&c};int cases=0;
 for(int mode=0;mode<6;mode++){struct file f={.private_data=&n};allocation_step=0;allocation_failure=mode==1?1:mode==2?2:0;id_fail=mode==3;c.ready=mode!=4;atomic_set(&c.teardown,mode==5);int err=fastrpc_device_open(NULL,&f);if(mode==0){assert(!err && !list_empty(&c.users));assert(!fastrpc_device_release(NULL,&f));}else assert(err<0);assert(c.refs==1 && !c.invoke_cnt && list_empty(&c.users) && !ids && !allocations && !mutexes);cases++;}
 for(int ssr=0;ssr<2;ssr++){struct file f={.private_data=&n};allocation_failure=allocation_step=id_fail=0;c.ready=true;atomic_set(&c.teardown,0);assert(!fastrpc_device_open(NULL,&f));atomic_set(&c.teardown,ssr);if(ssr)complete_all(&c.ssr_complete);assert(!fastrpc_device_release(NULL,&f));assert(!f.private_data && c.refs==1 && !c.invoke_cnt && !ids && !allocations && !mutexes);cases++;}
 printf("{\"file_open_failure_release_scenarios\":%d,\"post_ssr_release_count\":true}\n",cases);
}
