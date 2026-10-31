#define SLIM_REAL_SPIN
#include "slim_mock.h"
#include <pthread.h>
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{int unused;};struct idr{void*slot[256];unsigned cursor;};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
/* PUBLIC HEADER */
/* PRIVATE HEADER */
#undef spin_lock_irqsave
#undef spin_unlock_irqrestore
#define spin_lock_irqsave(p,f) ((f)=0,assert(!pthread_mutex_lock(p)))
#define spin_unlock_irqrestore(p,f) ((void)(f),assert(!pthread_mutex_unlock(p)))
#undef refcount_inc
#define refcount_inc(p) __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST)
static bool refcount_dec_and_test(refcount_t*r){return __atomic_sub_fetch(r,1,__ATOMIC_SEQ_CST)==0;}
static struct slim_controller ctrl={.txn_lock=PTHREAD_MUTEX_INITIALIZER};
static void pm_runtime_mark_last_busy(struct device*d){}
static void pm_runtime_put_autosuspend(struct device*d){assert(false);}
static void*idr_find(struct idr*i,unsigned id){return i->slot[id];}
static int idr_alloc_cyclic(struct idr*i,void*p,int start,int end,int f){assert(!i->slot[1]);i->slot[1]=p;return 1;}
static void idr_remove(struct idr*i,unsigned id){assert(i->slot[id]);i->slot[id]=NULL;}
#define memcpy_fromio(d,s,n) memcpy(d,s,n)
#undef complete
static void locked_complete(struct completion*c){int rc=pthread_mutex_trylock(&ctrl.txn_lock);assert(rc==EBUSY);c->done=1;}
#define complete(c) locked_complete(c)
/* FUNCTIONS */
static void*response(void*p){u8 reply=42;slim_msg_response(&ctrl,&reply,1,1);return NULL;}
static void*cancel(void*p){slim_free_txn_tid(&ctrl,p);return NULL;}
int main(void){for(int i=0;i<200;i++){struct completion done={0};u8 buffer=0xcc;struct slim_msg_txn*t=kzalloc(sizeof(*t),0);t->async=true;t->reply_len=1;t->msg=&t->async_msg;t->msg->rbuf=&buffer;t->comp=&done;refcount_set(&t->refs,1);assert(!slim_alloc_txn_tid(&ctrl,t));pthread_t a,b;pthread_create(&a,NULL,response,NULL);pthread_create(&b,NULL,cancel,t);pthread_join(a,NULL);pthread_join(b,NULL);assert(!ctrl.tid_idr.slot[1]&&t->refs==1);assert(done.done?(buffer==42):(buffer==0xcc));slim_put_async_txn(t);assert(!live);}
for(int i=0;i<200;i++){struct completion done={0};u8 buffer=0xcc;struct slim_msg_txn*t=kzalloc(sizeof(*t),0);t->async=true;t->reply_len=1;t->msg=&t->async_msg;t->msg->rbuf=&buffer;t->comp=&done;refcount_set(&t->refs,1);assert(!slim_alloc_txn_tid(&ctrl,t));pthread_t thread;pthread_create(&thread,NULL,response,NULL);slim_put_async_txn(t);pthread_join(thread,NULL);assert(done.done&&buffer==42&&!ctrl.tid_idr.slot[1]&&!live);}puts("{\"response_cancel_races\":200,\"async_caller_response_release_races\":200,\"completion_under_transaction_lock\":true}");}
