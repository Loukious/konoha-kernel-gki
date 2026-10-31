#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
typedef uint32_t u32;
#define WRITE_ONCE(x,y) ((x)=(y))
#define READ_ONCE(x) (x)
struct work_struct {int unused;};static int cvp_fastrpc_cleanup_work;
static void schedule_work(int *w){}
struct fastrpc_device {unsigned handle;};enum fastrpc_driver_status {DOWN};
#define dprintk(...) ((void)0)
struct mutex {pthread_mutex_t m;};static void mutex_init(struct mutex*m){assert(!pthread_mutex_init(&m->m,0));}static void mutex_destroy(struct mutex*m){assert(!pthread_mutex_destroy(&m->m));}static void mutex_lock(struct mutex*m){assert(!pthread_mutex_lock(&m->m));}static void mutex_unlock(struct mutex*m){assert(!pthread_mutex_unlock(&m->m));}
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *h){h->next=h->prev=h;}static bool list_empty(struct list_head*h){return h->next==h;}
static void list_add_tail(struct list_head *n,struct list_head*h){n->next=h;n->prev=h->prev;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head*n){n->prev->next=n->next;n->next->prev=n->prev;n->next=n->prev=NULL;}
#define list_entry(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define list_first_entry(h,t,m) list_entry((h)->next,t,m)
#define list_for_each_entry(p,h,m) for(p=list_first_entry(h,typeof(*p),m);&(p)->m!=(h);p=list_entry((p)->m.next,typeof(*p),m))
#define list_for_each_safe(p,n,h) for(p=(h)->next,n=p->next;p!=(h);p=n,n=p->next)
struct list {struct list_head list;struct mutex lock;};
typedef atomic_int atomic_t;
#define atomic_read(p) atomic_load(p)
#define atomic_dec(p) assert(atomic_fetch_sub(p,1)>0)
#define atomic_inc(p) atomic_fetch_add(p,1)
struct msm_cvp_inst {struct list_head dsp_list;};
struct cvp_dsp_fastrpc_driver_entry {bool cleanup_pending;struct list_head list;unsigned handle;atomic_t refcount;struct list dsp_sessions;int session_cnt;int cvp_fastrpc_driver;};
struct cvp_dsp_apps {struct list fastrpc_driver_list;struct mutex driver_name_lock,rx_lock;};static struct cvp_dsp_apps gfa_cv;
static int live,names,registrations;static atomic_bool finished;
static void usleep_range(int a,int b){usleep(100);}
static void kfree(void*p){assert(live>0);live--;free(p);}
static void __fastrpc_driver_unregister(int*p){assert(*p==1);*p=0;registrations--;}
static void eva_fastrpc_driver_release_name(struct cvp_dsp_fastrpc_driver_entry*n){names--;}
static int delete_dsp_session(struct msm_cvp_inst *i,struct cvp_dsp_fastrpc_driver_entry*n){assert(!atomic_read(&n->refcount));assert(!n->list.next);kfree(i);return 0;}
#define DEINIT_MSM_CVP_LIST(p) mutex_destroy(&(p)->lock)
#include "registry.inc"
static struct cvp_dsp_fastrpc_driver_entry*create(unsigned handle){struct cvp_dsp_fastrpc_driver_entry*n=calloc(1,sizeof(*n));live++;n->handle=handle;n->cvp_fastrpc_driver=1;names++;registrations++;mutex_init(&n->dsp_sessions.lock);INIT_LIST_HEAD(&n->dsp_sessions.list);for(int i=0;i<2;i++){struct msm_cvp_inst *s=calloc(1,sizeof(*s));live++;list_add_tail(&s->dsp_list,&n->dsp_sessions.list);n->session_cnt++;}list_add_tail(&n->list,&gfa_cv.fastrpc_driver_list.list);return n;}
static void *remove_thread(void*p){eva_fastrpc_driver_unregister(42,true);atomic_store(&finished,true);return NULL;}
int main(void){INIT_LIST_HEAD(&gfa_cv.fastrpc_driver_list.list);mutex_init(&gfa_cv.fastrpc_driver_list.lock);mutex_init(&gfa_cv.driver_name_lock);mutex_init(&gfa_cv.rx_lock);
 for(int i=0;i<50;i++){struct cvp_dsp_fastrpc_driver_entry*n=create(42);assert(cvp_get_fastrpc_node_with_handle(42)==n);eva_fastrpc_driver_unregister(42,false);assert(n->session_cnt==2&&atomic_read(&n->refcount)==1);atomic_store(&finished,false);pthread_t t;pthread_create(&t,NULL,remove_thread,NULL);for(;;){mutex_lock(&gfa_cv.fastrpc_driver_list.lock);bool empty=list_empty(&gfa_cv.fastrpc_driver_list.list);mutex_unlock(&gfa_cv.fastrpc_driver_list.lock);if(empty)break;usleep(100);}usleep(i?1000:60000);assert(!atomic_load(&finished));assert(!cvp_get_fastrpc_node_with_handle(42));cvp_put_fastrpc_node(n);pthread_join(t,NULL);assert(!live&&!names&&!registrations&&atomic_load(&finished));}
 create(44);struct fastrpc_device fd={.handle=44};assert(!cvp_fastrpc_callback(&fd,DOWN));assert(live==3);cvp_fastrpc_cleanup_worker(NULL);assert(!live);
 create(1);create(2);cvp_remove_dsp_sessions();assert(!live&&!names&&!registrations);assert(!pop_frpc_node());mutex_destroy(&gfa_cv.fastrpc_driver_list.lock);mutex_destroy(&gfa_cv.driver_name_lock);mutex_destroy(&gfa_cv.rx_lock);printf("{\"registry_withdrawal_and_reference_races\":50,\"forced_session_cleanup\":106}\n");}
