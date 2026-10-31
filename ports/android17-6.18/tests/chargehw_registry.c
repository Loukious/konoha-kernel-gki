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
#define FG_IC_MAX 3
#define platform_fg_ops_invalid(data, name) (!(data) || !(data)->ops || !(data)->ops->name)
#define PAGE_SIZE 4096
#define min(a,b) ((a) < (b) ? (a) : (b))
struct fuelguage_ic_ops { int (*fg_ic_get_soc)(void *data); };
struct fuelguage_info { void *data; struct fuelguage_ic_ops *ops; };
static struct fuelguage_info *g_fg_ic_ops[FG_IC_MAX];
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
enum mca_charge_log_id_ele { LOG_FIRST, LOG_SECOND, MCA_CHARGE_LOG_ID_MAX };
struct mca_log_charge_log_ops { int (*dump_log_head)(void *, char *, int); int (*dump_log_context)(void *, char *, int); };
struct mca_charge_log_ops_data { struct mca_log_charge_log_ops *ops; void *data; };
static struct mca_charge_log_ops_data *g_mca_charge_log_data[MCA_CHARGE_LOG_ID_MAX];
static pthread_mutex_t mca_charge_log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t mca_charge_log_srcu = PTHREAD_RWLOCK_INITIALIZER;
/* STAGED_DEFINITIONS */
static atomic_int entered, allow_exit, unregister_done;
static int provider_data = 73;
static int blocking_soc(void *data)
{
	assert(data == &provider_data);
	atomic_store(&entered, 1);
	while (!atomic_load(&allow_exit)) usleep(100);
	assert(*(int *)data == 73);
	return 73;
}
static void *reader_thread(void *unused) { (void)unused; assert(platform_fg_ops_get_soc(0) == 73); return NULL; }
static void *remove_thread(void *unused) { (void)unused; platform_fg_ic_ops_unregister(0, &provider_data); atomic_store(&unregister_done, 1); return NULL; }
static int log_context(void *data, char *buf, int len) { return snprintf(buf, len, "%d", *(int *)data); }
static int log_overflow(void *data, char *buf, int len) { (void)data; memset(buf, 'a', len - 1); buf[len - 1] = 0; return 100000; }
static int log_failure(void *data, char *buf, int len) { (void)data; (void)buf; (void)len; return -EIO; }
int main(void)
{
	struct fuelguage_ic_ops ops = { .fg_ic_get_soc = blocking_soc };
	assert(platform_fg_ic_ops_register(FG_IC_MAX, &provider_data, &ops) == -EINVAL);
	fail_alloc = 1;
	assert(platform_fg_ic_ops_register(0, &provider_data, &ops) == -ENOMEM);
	fail_alloc = 0;
	assert(!platform_fg_ic_ops_register(0, &provider_data, &ops));
	assert(platform_fg_ic_ops_register(0, &provider_data, &ops) == -EEXIST);
	platform_fg_ic_ops_unregister(0, NULL);
	assert(g_fg_ic_ops[0]);
	pthread_t read, remove;
	pthread_create(&read, NULL, reader_thread, NULL);
	while (!atomic_load(&entered)) usleep(100);
	pthread_create(&remove, NULL, remove_thread, NULL);
	while (!atomic_load(&sync_waiting)) usleep(100);
	assert(!atomic_load(&unregister_done));
	assert(!rcu_access_pointer(g_fg_ic_ops[0]));
	atomic_store(&allow_exit, 1);
	pthread_join(read, NULL); pthread_join(remove, NULL);
	assert(platform_fg_ops_get_soc(0) == -EOPNOTSUPP);
	assert(atomic_load(&unregister_done) && !g_fg_ic_ops[0] && !atomic_load(&live_allocs));
	char buf[PAGE_SIZE];
	struct mca_log_charge_log_ops log = { .dump_log_context = log_context };
	mca_log_charge_log_register(LOG_FIRST, &log, &provider_data);
	assert(mca_log_charge_log_dump_log_context(buf) == 3 && !strcmp(buf, "73\n"));
	assert(mca_log_charge_log_dump_log_head(buf) == 1 && !strcmp(buf, "\n"));
	struct mca_log_charge_log_ops huge = { .dump_log_context = log_overflow };
	mca_log_charge_log_register(LOG_FIRST, &huge, &provider_data);
	assert(mca_log_charge_log_dump_log_context(buf) == PAGE_SIZE - 1 && buf[PAGE_SIZE - 1] == 0);
	struct mca_log_charge_log_ops error = { .dump_log_context = log_failure };
	mca_log_charge_log_register(LOG_FIRST, &error, &provider_data);
	assert(mca_log_charge_log_dump_log_context(buf) == -EIO);
	mca_log_charge_log_unregister(LOG_FIRST, NULL);
	assert(g_mca_charge_log_data[0]);
	mca_log_charge_log_unregister(LOG_FIRST, &provider_data);
	assert(!atomic_load(&live_allocs));
	puts("{\"passed\":true,\"provider_unregistration_waits_for_reader\":true,\"new_reads_rejected_after_unpublish\":true,\"registration_failure_and_duplicate\":true,\"log_context_selection_and_bounds\":true}");
	return 0;
}
