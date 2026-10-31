#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#define GFP_KERNEL 0
#define CNSS_UTILS_NOTIFIER_MAX_USER 2
enum cnss_status_type {CNSS_UTILS_FMD_STATUS,CNSS_UTILS_MAX_STATUS_TYPE};
typedef void (*cnss_utils_status_update)(void *,bool);
struct cnss_utils_status_registration {cnss_utils_status_update callback;void *context;};
struct cnss_utils_status_notifier {struct cnss_utils_status_registration *users[2];};
struct cnss_utils_priv {struct cnss_utils_status_notifier notifier_ctx[1];};
static struct cnss_utils_priv object,*cnss_utils_priv=&object;
static pthread_mutex_t utils_notifier_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t utils_notifier_srcu=PTHREAD_RWLOCK_INITIALIZER;
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define rcu_access_pointer(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),v,__ATOMIC_RELEASE)
#define RCU_INIT_POINTER(p,v) rcu_assign_pointer(p,v)
#define rcu_dereference_protected(p,c) rcu_access_pointer(p)
#define srcu_dereference(p,s) rcu_access_pointer(p)
static int fail_alloc;
static atomic_int live,calls;
static void *kzalloc(size_t n,int flags){(void)flags;if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);atomic_fetch_add(&live,1);return p;}
static void kfree(void *p){if(p)atomic_fetch_sub(&live,1);free(p);}
static int srcu_read_lock(pthread_rwlock_t *p){pthread_rwlock_rdlock(p);return 0;}
static void srcu_read_unlock(pthread_rwlock_t *p,int idx){(void)idx;pthread_rwlock_unlock(p);}
static void synchronize_srcu(pthread_rwlock_t *p){pthread_rwlock_wrlock(p);pthread_rwlock_unlock(p);}
/* FUNCTIONS */
static int first,second;
static void callback(void *ctx,bool status){assert(status&&(ctx==&first||ctx==&second));atomic_fetch_add(&calls,1);}
static pthread_mutex_t test_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static bool entered,released,withdraw_started,withdraw_done;
static void blocking(void *ctx,bool status){
 assert(ctx==&first&&status);
 assert(!cnss_utils_register_status_notifier(CNSS_UTILS_FMD_STATUS,callback,&second));
 pthread_mutex_lock(&test_lock);entered=true;pthread_cond_broadcast(&cond);
 while(!released)pthread_cond_wait(&cond,&test_lock);
 pthread_mutex_unlock(&test_lock);atomic_fetch_add(&calls,1);
}
static void *invoke(void *arg){(void)arg;cnss_utils_status_update_user(CNSS_UTILS_FMD_STATUS,true);return NULL;}
static void *withdraw(void *arg){(void)arg;pthread_mutex_lock(&test_lock);withdraw_started=true;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&test_lock);assert(!cnss_utils_unregister_status_notifier(CNSS_UTILS_FMD_STATUS,blocking,&first));pthread_mutex_lock(&test_lock);withdraw_done=true;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&test_lock);return NULL;}
int main(void){
 for(int id=-32;id<=32;id++){
  cnss_utils_status_update_user(id,true);
  assert(cnss_utils_register_status_notifier(id,callback,&first)==(id?-EINVAL:0));
  assert(cnss_utils_unregister_status_notifier(id,callback,&first)==(id?-EINVAL:0));
 }
 assert(cnss_utils_register_status_notifier(0,NULL,&first)==-EINVAL);
 fail_alloc=1;assert(cnss_utils_register_status_notifier(0,callback,&first)==-ENOMEM);fail_alloc=0;
 assert(!cnss_utils_register_status_notifier(0,callback,&first));assert(cnss_utils_register_status_notifier(0,callback,&first)==-EEXIST);
 assert(!cnss_utils_register_status_notifier(0,callback,&second));int third;assert(cnss_utils_register_status_notifier(0,callback,&third)==-ENOSPC);
 cnss_utils_status_update_user(0,true);assert(calls==2);
 assert(cnss_utils_unregister_status_notifier(0,callback,&third)==-ENOENT);assert(!cnss_utils_unregister_status_notifier(0,callback,&first));assert(!cnss_utils_unregister_status_notifier(0,callback,&second));assert(!live);
 for(int round=0;round<200;round++){
  entered=released=withdraw_started=withdraw_done=false;
  assert(!cnss_utils_register_status_notifier(0,blocking,&first));pthread_t reader,remover;assert(!pthread_create(&reader,NULL,invoke,NULL));
  pthread_mutex_lock(&test_lock);while(!entered)pthread_cond_wait(&cond,&test_lock);pthread_mutex_unlock(&test_lock);
  assert(!pthread_create(&remover,NULL,withdraw,NULL));pthread_mutex_lock(&test_lock);while(!withdraw_started)pthread_cond_wait(&cond,&test_lock);assert(!withdraw_done);released=true;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&test_lock);
  pthread_join(reader,NULL);pthread_join(remover,NULL);assert(withdraw_done);assert(!cnss_utils_unregister_status_notifier(0,callback,&second));assert(!live);
 }
 cnss_utils_priv=NULL;cnss_utils_status_update_user(0,true);assert(cnss_utils_register_status_notifier(0,callback,&first)==-EINVAL);assert(cnss_utils_unregister_status_notifier(0,callback,&first)==-EINVAL);
 assert(!live&&calls==402);
 puts("{\"signed_status_bound_cases\":65,\"duplicate_full_and_allocation_errors\":true,\"immutable_callback_context_pair\":true,\"callback_registration_reentry\":true,\"threaded_unregister_drains\":200}");
}
