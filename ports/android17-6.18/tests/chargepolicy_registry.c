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
#define ADAPTER_PROTOCOL_MAX 3
#define adapter_protocol_invalid_ops(data, name) (!(data) || !(data)->ops || !(data)->ops->name)
#define PAGE_SIZE 4096
#define min(a,b) ((a) < (b) ? (a) : (b))
struct adapter_protocol_class_ops { int (*get_adapter_max_power)(void *data, unsigned int *value); };
struct adapter_protocol_class_data { void *data; struct adapter_protocol_class_ops *ops; };
static struct adapter_protocol_class_data *g_protocol_class_data[ADAPTER_PROTOCOL_MAX];
static pthread_mutex_t mca_ops_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t mca_ops_srcu = PTHREAD_RWLOCK_INITIALIZER;
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
static int blocking_power(void *data, unsigned int *value)
{
	assert(data == &provider_data);
	atomic_store(&entered, 1);
	while (!atomic_load(&allow_exit)) usleep(100);
	assert(*(int *)data == 73);
	*value = 73;
	return 0;
}
static void *reader_thread(void *unused) { (void)unused; unsigned int value; assert(protocol_class_get_adapter_max_power(0, &value) == 0 && value == 73); return NULL; }
static void *remove_thread(void *unused) { (void)unused; protocol_class_unregister_ops(0, &provider_data); atomic_store(&unregister_done, 1); return NULL; }
int main(void)
{
 struct adapter_protocol_class_ops ops = { .get_adapter_max_power = blocking_power };
 assert(protocol_class_register_ops(ADAPTER_PROTOCOL_MAX, &ops, &provider_data) == -EINVAL);
 assert(protocol_class_register_ops(0, NULL, &provider_data) == -EINVAL);
 assert(protocol_class_register_ops(0, &ops, NULL) == -EINVAL);
 fail_alloc = 1;
 assert(protocol_class_register_ops(0, &ops, &provider_data) == -ENOMEM);
 fail_alloc = 0;
 assert(!protocol_class_register_ops(0, &ops, &provider_data));
 assert(protocol_class_register_ops(0, &ops, &provider_data) == -EEXIST);
 protocol_class_unregister_ops(0, NULL);
 assert(g_protocol_class_data[0]);
 pthread_t read, remove;
 pthread_create(&read, NULL, reader_thread, NULL);
 while (!atomic_load(&entered)) usleep(100);
 pthread_create(&remove, NULL, remove_thread, NULL);
 while (!atomic_load(&sync_waiting)) usleep(100);
 assert(!atomic_load(&unregister_done));
 assert(!rcu_access_pointer(g_protocol_class_data[0]));
 atomic_store(&allow_exit, 1);
 pthread_join(read, NULL); pthread_join(remove, NULL);
 unsigned int value = 123;
 assert(protocol_class_get_adapter_max_power(0, &value) == -EOPNOTSUPP && value == 123);
 assert(protocol_class_get_adapter_max_power(0, NULL) == -EINVAL);
 assert(atomic_load(&unregister_done) && !g_protocol_class_data[0] && !atomic_load(&live_allocs));
 puts("{\"passed\":true,\"unregister_waits_for_reader\":true,\"allocation_and_duplicate_failures\":true}");
 return 0;
}
