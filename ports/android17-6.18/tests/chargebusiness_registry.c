/* Test actual callback publication/unregister with a pthread SRCU model. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define __rcu
#define GFP_KERNEL 0
struct strategy_fg_class_ops { int (*strategy_fg_get_soc)(void *data); bool (*strategy_fg_get_charging_done)(void *data); int (*strategy_fg_get_model_name)(void *,const char **); };
struct strategy_fg_class_info { void *data; struct strategy_fg_class_ops *ops; };
static struct strategy_fg_class_info *g_stg_fg_class_info;
#define is_invalid_ops(name) (!entry || !entry->ops || !entry->ops->name)
#define strategy_fg_class_ops_no_para(name) (entry->ops->name(entry->data))
static int strscpy(char *out,const char *in,size_t len) { size_t n=strlen(in);if(n>=len)return -E2BIG;memcpy(out,in,n+1);return n; }
static pthread_mutex_t fg_ops_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t fg_ops_srcu = PTHREAD_RWLOCK_INITIALIZER;
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define lockdep_is_held(p) 1
#define srcu_dereference(p,l) __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define rcu_access_pointer(p) __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define rcu_dereference_protected(p,c) __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p), (v), __ATOMIC_RELEASE)
#define rcu_replace_pointer(p,v,c) __atomic_exchange_n(&(p), (v), __ATOMIC_ACQ_REL)
struct srcu_guard { pthread_rwlock_t *lock; };
static struct srcu_guard srcu_guard_init(pthread_rwlock_t *lock) { pthread_rwlock_rdlock(lock); return (struct srcu_guard){lock}; }
static void srcu_guard_cleanup(struct srcu_guard *guard) { pthread_rwlock_unlock(guard->lock); }
#define guard(type) struct srcu_guard current_guard __attribute__((cleanup(srcu_guard_cleanup))) = srcu_guard_init
static atomic_int sync_waiting;
static void synchronize_srcu(pthread_rwlock_t *lock) { atomic_store(&sync_waiting, 1); pthread_rwlock_wrlock(lock); pthread_rwlock_unlock(lock); }
static int fail_alloc;
static atomic_int live_allocs;
static void *kmalloc(size_t size, int flags) { (void)flags; if (fail_alloc) return NULL; void *p = malloc(size); if (p) atomic_fetch_add(&live_allocs, 1); return p; }
static void kfree(void *p) { if (p) atomic_fetch_sub(&live_allocs, 1); free(p); }
#define EXPORT_SYMBOL(name)
/* STAGED_DEFINITIONS */
static atomic_int entered, allow_exit, unregister_done;
static int provider_data = 73;
static int blocking_soc(void *data)
{
 assert(data==&provider_data);atomic_store(&entered,1);
 while(!atomic_load(&allow_exit)) usleep(100);
 return *(int *)data;
}
static void *reader_thread(void *unused) {(void)unused;assert(strategy_class_fg_ops_get_soc()==73);return NULL;}
static void *remove_thread(void *unused) {(void)unused;strategy_class_fg_ops_unregister(&provider_data);atomic_store(&unregister_done,1);return NULL;}
static char model[]="BP4U";
static int get_model(void *data,const char **name) {(void)data;*name=model;return 0;}
int main(void) {
 struct strategy_fg_class_ops ops={.strategy_fg_get_soc=blocking_soc,.strategy_fg_get_model_name=get_model};
 assert(strategy_class_fg_ops_get_charging_done()==false);
 assert(strategy_class_fg_ops_register(NULL,&ops)==-EINVAL);
 fail_alloc=1;assert(strategy_class_fg_ops_register(&provider_data,&ops)==-ENOMEM);fail_alloc=0;
 assert(!strategy_class_fg_ops_register(&provider_data,&ops));
 assert(strategy_class_fg_ops_register(&provider_data,&ops)==-EEXIST);
 char copied[16];assert(!strategy_class_fg_copy_model_name(copied,sizeof(copied)));assert(!strcmp(copied,"BP4U"));model[0]='X';assert(!strcmp(copied,"BP4U"));
 assert(strategy_class_fg_copy_model_name(copied,1)==-E2BIG);
 strategy_class_fg_ops_unregister(NULL);assert(g_stg_fg_class_info);
 pthread_t read,remove;pthread_create(&read,NULL,reader_thread,NULL);while(!atomic_load(&entered))usleep(100);
 pthread_create(&remove,NULL,remove_thread,NULL);while(!atomic_load(&sync_waiting))usleep(100);
 assert(!atomic_load(&unregister_done)&&!rcu_access_pointer(g_stg_fg_class_info));
 atomic_store(&allow_exit,1);pthread_join(read,NULL);pthread_join(remove,NULL);
 assert(strategy_class_fg_ops_get_soc()==-ENODEV&&!atomic_load(&live_allocs));
 puts("{\"passed\":true,\"unregister_waits_for_callback\":true,\"missing_done_is_false\":true,\"model_copy_owned\":true}");
}
