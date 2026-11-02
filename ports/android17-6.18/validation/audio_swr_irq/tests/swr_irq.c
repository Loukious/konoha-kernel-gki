#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef uint8_t u8;
struct task_struct { int id; };
static _Thread_local struct task_struct task;
#define current (&task)
#define READ_ONCE(value) __atomic_load_n(&(value), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(value, data) __atomic_store_n(&(value), (data), __ATOMIC_RELEASE)
#define EXPORT_SYMBOL(name)
#define GFP_KERNEL 0
#define IS_ERR(value) ((uintptr_t)(value) >= (uintptr_t)-4095)
#define ERR_PTR(value) ((void *)(intptr_t)(value))

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(name) struct list_head name = { &name, &name }
#define list_for_each_entry(pos, head, member) \
	for (pos = (void *)(head)->next; &pos->member != (head); \
	     pos = (void *)pos->member.next)
static void list_add_tail(struct list_head *node, struct list_head *head)
{
	node->prev = head->prev;
	node->next = head;
	head->prev->next = node;
	head->prev = node;
}
static void list_del(struct list_head *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
}

struct mutex { pthread_mutex_t native; };
#define DEFINE_MUTEX(name) struct mutex name = { PTHREAD_MUTEX_INITIALIZER }
static void before_lock(struct mutex *lock);
static void mutex_init(struct mutex *lock) { assert(!pthread_mutex_init(&lock->native, NULL)); }
static void mutex_lock(struct mutex *lock) { before_lock(lock); assert(!pthread_mutex_lock(&lock->native)); }
static void mutex_unlock(struct mutex *lock) { assert(!pthread_mutex_unlock(&lock->native)); }
static void mutex_destroy(struct mutex *lock) { assert(!pthread_mutex_destroy(&lock->native)); }
#define spin_lock_irqsave(lock, flags) do { (void)(flags); mutex_lock(lock); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); mutex_unlock(lock); } while (0)
typedef atomic_int atomic_t;
static void atomic_inc(atomic_t *value) { atomic_fetch_add(value, 1); }
static bool atomic_dec_and_test(atomic_t *value) { return atomic_fetch_sub(value, 1) == 1; }
#define wake_up_all(wait) ((void)(wait))
typedef atomic_int refcount_t;
static void refcount_set(refcount_t *value, int count) { atomic_store(value, count); }
void refcount_inc(refcount_t *value) { assert(atomic_fetch_add(value, 1) > 0); }
static bool refcount_dec_and_test(refcount_t *value) { int old = atomic_fetch_sub(value, 1); assert(old > 0); return old == 1; }

struct device { atomic_int refs; };
struct device *get_device(struct device *dev) { assert(atomic_fetch_add(&dev->refs, 1) > 0); return dev; }
static void put_device(struct device *dev) { assert(atomic_fetch_sub(&dev->refs, 1) > 1); }
struct swr_master {
	struct mutex state_lock;
	atomic_t active_calls;
	int call_wait;
	bool stopping;
};
struct irq_domain { unsigned int virq; };
struct swr_device {
	struct device dev;
	struct swr_master *master;
	u8 dev_num;
	struct irq_domain *slave_irq;
	bool slave_irq_pending;
};
struct swr_mstr_ctrl { struct swr_master master; bool dev_up; };
static atomic_int fail_alloc;
static atomic_int allocations;
static void *kzalloc(size_t size, int flags)
{
	(void)flags;
	if (atomic_exchange(&fail_alloc, 0))
		return NULL;
	void *result = calloc(1, size);
	assert(result);
	atomic_fetch_add(&allocations, 1);
	return result;
}
static void kfree(void *ptr) { if (ptr) atomic_fetch_sub(&allocations, 1); free(ptr); }
static unsigned int irq_find_mapping(struct irq_domain *domain, unsigned long irq)
{
	assert(irq == 0);
	return domain->virq;
}
static void (*irq_callback)(unsigned int);
static atomic_int callback_count;
static void handle_nested_irq(unsigned int irq)
{
	assert(irq != 0);
	atomic_fetch_add(&callback_count, 1);
	if (irq_callback)
		irq_callback(irq);
}

/* Extracted from the exact staged production sources by the verifier. */
#include "swr_irq_production.inc"

static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static struct mutex *paused_lock;
static bool lock_waiting, lock_release, handler_waiting, handler_release;
static atomic_bool withdraw_done;
static struct swr_device *first, *second;
static struct swr_mstr_ctrl *controller;
static int self_results[4];

static void before_lock(struct mutex *lock)
{
	if (task.id != 3 || lock != paused_lock)
		return;
	assert(!pthread_mutex_lock(&gate_lock));
	lock_waiting = true;
	assert(!pthread_cond_broadcast(&gate_cond));
	while (!lock_release)
		assert(!pthread_cond_wait(&gate_cond, &gate_lock));
	assert(!pthread_mutex_unlock(&gate_lock));
}

static void wait_flag(bool *flag)
{
	struct timespec deadline;
	assert(!clock_gettime(CLOCK_REALTIME, &deadline));
	deadline.tv_sec += 3;
	assert(!pthread_mutex_lock(&gate_lock));
	while (!*flag)
		assert(!pthread_cond_timedwait(&gate_cond, &gate_lock, &deadline));
	assert(!pthread_mutex_unlock(&gate_lock));
}

static void release_flag(bool *flag)
{
	assert(!pthread_mutex_lock(&gate_lock));
	*flag = true;
	assert(!pthread_cond_broadcast(&gate_cond));
	assert(!pthread_mutex_unlock(&gate_lock));
}

static void callback_block(unsigned int irq)
{
	if (irq != 41)
		return;
	assert(!pthread_mutex_lock(&gate_lock));
	handler_waiting = true;
	assert(!pthread_cond_broadcast(&gate_cond));
	while (!handler_release)
		assert(!pthread_cond_wait(&gate_cond, &gate_lock));
	assert(!pthread_mutex_unlock(&gate_lock));
}

static void callback_self(unsigned int irq)
{
	assert(irq == 41);
	struct irq_domain extra = { 99 };
	self_results[0] = swr_set_slave_irq(first, NULL);
	self_results[1] = swr_set_slave_irq(second, NULL);
	self_results[2] = swr_set_slave_irq(second, &extra);
	self_results[3] = swr_handle_slave_irq(first->master, first->dev_num, swrm_handle_nested_irq);
}

static void callback_pending(unsigned int irq)
{
	assert(irq == 41);
	WRITE_ONCE(first->slave_irq_pending, atomic_load(&callback_count) < 3);
}

static void callback_down(unsigned int irq)
{
	assert(irq == 41);
	WRITE_ONCE(first->slave_irq_pending, atomic_load(&callback_count) < 3);
	WRITE_ONCE(controller->dev_up, false);
}

struct dispatch_args { struct swr_master *master; u8 num; int task_id; bool pending; };
static void *dispatch_thread(void *arg)
{
	struct dispatch_args *a = arg;
	task.id = a->task_id;
	a->pending = swr_handle_slave_irq(a->master, a->num, swrm_handle_nested_irq);
	return NULL;
}
static void *withdraw_thread(void *arg)
{
	task.id = 2;
	assert(swr_set_slave_irq(arg, NULL) == 0);
	atomic_store(&withdraw_done, true);
	return NULL;
}

static void wait_stopping(struct swr_device *dev)
{
	for (int attempt = 0; attempt < 3000; attempt++) {
		bool stopping = false;
		struct swr_slave_irq *irq;
		mutex_lock(&swr_slave_irq_lock);
		list_for_each_entry(irq, &swr_slave_irqs, node)
			if (irq->swr == dev)
				stopping = irq->stopping;
		mutex_unlock(&swr_slave_irq_lock);
		assert(!atomic_load(&withdraw_done));
		if (stopping)
			return;
		usleep(1000);
	}
	assert(!"withdrawal did not reserve its drain");
}

int main(void)
{
	struct swr_mstr_ctrl ctrl = { .dev_up = true };
	struct swr_device a = { .master = &ctrl.master, .dev_num = 1 };
	struct swr_device b = { .master = &ctrl.master, .dev_num = 2 };
	struct irq_domain da = { 41 }, db = { 42 }, unmapped = { 0 };
	pthread_t dispatch, withdraw;
	struct dispatch_args args = { &ctrl.master, 1, 1, false };
	mutex_init(&ctrl.master.state_lock);
	atomic_init(&ctrl.master.active_calls, 0);
	atomic_init(&a.dev.refs, 1);
	atomic_init(&b.dev.refs, 1);
	first = &a;
	second = &b;
	controller = &ctrl;

	assert(swr_set_slave_irq(NULL, &da) == -EINVAL);
	assert(swr_set_slave_irq(&a, ERR_PTR(-EINVAL)) == -EINVAL);
	assert(swr_set_slave_irq(&a, &unmapped) == 0);
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));
	assert(atomic_load(&callback_count) == 0);
	assert(swr_set_slave_irq(&a, NULL) == 0);
	assert(swr_set_slave_irq(&a, NULL) == 0);
	assert(!swr_handle_slave_irq(NULL, 1, swrm_handle_nested_irq));
	ctrl.master.stopping = true;
	assert(swr_set_slave_irq(&a, &da) == -ENODEV);
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));
	ctrl.master.stopping = false;
	atomic_store(&fail_alloc, 1);
	assert(swr_set_slave_irq(&a, &da) == -ENOMEM);
	assert(atomic_load(&ctrl.master.active_calls) == 0);
	assert(atomic_load(&a.dev.refs) == 1);

	assert(swr_set_slave_irq(&a, &da) == 0);
	assert(atomic_load(&a.dev.refs) == 2 && a.slave_irq == &da);
	assert(swr_set_slave_irq(&a, &db) == -EBUSY && a.slave_irq == &da);
	assert(!swr_handle_slave_irq(&ctrl.master, 9, swrm_handle_nested_irq));
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));
	assert(atomic_load(&callback_count) == 1);
	da.virq = 0;
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));
	assert(atomic_load(&callback_count) == 1);
	da.virq = 41;
	assert(swr_set_slave_irq(&b, &db) == 0);
	irq_callback = callback_self;
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));
	assert(self_results[0] == -EDEADLK && self_results[1] == -EDEADLK);
	assert(self_results[2] == -EDEADLK && self_results[3] == false);
	assert(a.slave_irq == &da && b.slave_irq == &db);
	irq_callback = callback_pending;
	atomic_store(&callback_count, 0);
	swrm_dispatch_slave_irq(&ctrl, 1);
	assert(atomic_load(&callback_count) == 3);
	irq_callback = callback_down;
	atomic_store(&callback_count, 0);
	swrm_dispatch_slave_irq(&ctrl, 1);
	assert(atomic_load(&callback_count) == 1);
	ctrl.dev_up = true;

	irq_callback = callback_block;
	assert(!pthread_create(&dispatch, NULL, dispatch_thread, &args));
	wait_flag(&handler_waiting);
	assert(!pthread_create(&withdraw, NULL, withdraw_thread, &a));
	wait_stopping(&a);
	assert(swr_set_slave_irq(&a, NULL) == -EBUSY);
	assert(swr_set_slave_irq(&a, &da) == -EBUSY);
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));
	assert(!swr_handle_slave_irq(&ctrl.master, 2, swrm_handle_nested_irq));
	assert(!atomic_load(&withdraw_done));
	release_flag(&handler_release);
	assert(!pthread_join(dispatch, NULL));
	assert(!pthread_join(withdraw, NULL));
	assert(atomic_load(&withdraw_done) && a.slave_irq == NULL);
	assert(atomic_load(&a.dev.refs) == 1);
	assert(!swr_handle_slave_irq(&ctrl.master, 1, swrm_handle_nested_irq));

	irq_callback = NULL;
	assert(swr_set_slave_irq(&a, &da) == 0);
	mutex_lock(&swr_slave_irq_lock);
	struct swr_slave_irq *registration;
	list_for_each_entry(registration, &swr_slave_irqs, node)
		if (registration->swr == &a)
			paused_lock = &registration->dispatch_lock;
	mutex_unlock(&swr_slave_irq_lock);
	assert(paused_lock);
	args.task_id = 3;
	assert(!pthread_create(&dispatch, NULL, dispatch_thread, &args));
	wait_flag(&lock_waiting);
	int calls_before = atomic_load(&callback_count);
	assert(swr_set_slave_irq(&a, NULL) == 0);
	assert(a.slave_irq == NULL && atomic_load(&a.dev.refs) == 2);
	release_flag(&lock_release);
	assert(!pthread_join(dispatch, NULL));
	assert(atomic_load(&callback_count) == calls_before);
	assert(atomic_load(&a.dev.refs) == 1);
	paused_lock = NULL;

	assert(swr_set_slave_irq(&a, &da) == 0);
	ctrl.master.stopping = true;
	assert(swr_set_slave_irq(&a, NULL) == 0);
	assert(swr_set_slave_irq(&b, NULL) == 0);
	assert(swr_set_slave_irq(&a, NULL) == 0);
	assert(atomic_load(&ctrl.master.active_calls) == 0);
	assert(atomic_load(&a.dev.refs) == 1 && atomic_load(&b.dev.refs) == 1);
	assert(atomic_load(&allocations) == 0);
	assert(swr_slave_irqs.next == &swr_slave_irqs);
	mutex_destroy(&ctrl.master.state_lock);
	puts("PASS: IRQ registration, pending loop, shutdown, references, self-withdraw, concurrent drain and reserved dispatch");
	return 0;
}
