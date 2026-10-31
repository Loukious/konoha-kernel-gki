#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MCA_SMARTCHG_IF_CHG_TYPE_END 6
#define GFP_KERNEL 0
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELEASE)
struct smart_charge_info {int delta_fv;};struct mca_votable{};
struct mca_smartchg_if_ops {int type;void *data;int (*set_delta_fv)(void *,int);};
static struct mca_smartchg_if_ops *g_mca_smartchg_if_ops[6];
static pthread_mutex_t smart_ops_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t smart_ops_srcu=PTHREAD_RWLOCK_INITIALIZER;
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define lockdep_is_held(p) 1
#define srcu_dereference(p,l) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_access_pointer(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_dereference_protected(p,c) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
struct srcu_guard {pthread_rwlock_t *lock;};
static struct srcu_guard srcu_guard_init(pthread_rwlock_t *l){pthread_rwlock_rdlock(l);return (struct srcu_guard){l};}
static void srcu_guard_cleanup(struct srcu_guard *g){pthread_rwlock_unlock(g->lock);}
#define guard(t) struct srcu_guard g __attribute__((cleanup(srcu_guard_cleanup)))=srcu_guard_init
static atomic_int sync_waiting,live_allocs,entered,allow_exit,removed;
static int fail_alloc,replays;
static void synchronize_srcu(pthread_rwlock_t *l){atomic_store(&sync_waiting,1);pthread_rwlock_wrlock(l);pthread_rwlock_unlock(l);}
static void *kmemdup(void *src,size_t len,int flags){(void)flags;if(fail_alloc)return NULL;void *p=malloc(len);if(p){memcpy(p,src,len);atomic_fetch_add(&live_allocs,1);}return p;}
static void kfree(void *p){if(p)atomic_fetch_sub(&live_allocs,1);free(p);}
static void smart_charge_replay(void){replays++;}
/* STAGED_DEFINITIONS */
static int provider_data;
static int callback(void *p,int v){assert(p==&provider_data&&v==42);atomic_store(&entered,1);while(!atomic_load(&allow_exit))usleep(100);return -EIO;}
static void *reader(void *p){assert(smart_charge_fv_control(NULL,p,42,NULL)==-EIO);return NULL;}
static void *unregister_worker(void *p){mca_smartchg_if_ops_unregister(0,p);atomic_store(&removed,1);return NULL;}
int main(void){
 struct mca_smartchg_if_ops ops={.type=0,.data=&provider_data,.set_delta_fv=callback};struct smart_charge_info info={0};
 assert(mca_smartchg_if_ops_register(NULL)==-EINVAL);ops.type=-1;assert(mca_smartchg_if_ops_register(&ops)==-EINVAL);ops.type=6;assert(mca_smartchg_if_ops_register(&ops)==-EINVAL);ops.type=0;fail_alloc=1;assert(mca_smartchg_if_ops_register(&ops)==-ENOMEM);fail_alloc=0;
 assert(!mca_smartchg_if_ops_register(&ops)&&replays==1);assert(mca_smartchg_if_ops_register(&ops)==-EBUSY);ops.set_delta_fv=NULL;
 mca_smartchg_if_ops_unregister(0,NULL);assert(g_mca_smartchg_if_ops[0]);pthread_t r,u;pthread_create(&r,NULL,reader,&info);while(!atomic_load(&entered))usleep(100);pthread_create(&u,NULL,unregister_worker,&provider_data);while(!atomic_load(&sync_waiting))usleep(100);assert(!atomic_load(&removed)&&!rcu_access_pointer(g_mca_smartchg_if_ops[0]));atomic_store(&allow_exit,1);pthread_join(r,NULL);pthread_join(u,NULL);assert(!atomic_load(&live_allocs)&&info.delta_fv==42);
 puts("{\"passed\":true,\"immutable_provider_copy\":true,\"unregister_drains_callback\":true,\"provider_error_preserved\":true,\"late_provider_replay_requested\":true}");
}
