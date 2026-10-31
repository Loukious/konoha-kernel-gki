// SPDX-License-Identifier: GPL-2.0-only
/* Actual staged cooling workers run against these host service adapters. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

struct cpumask { unsigned long bits; };
typedef struct cpumask cpumask_t;
struct list_head { struct list_head *next, *prev; };
struct device { int unused; };
struct device_node { unsigned int refs; };
struct work_struct { _Atomic bool queued; };
struct delayed_work { struct work_struct work; };
struct workqueue_struct { bool destroyed; };
struct thermal_cooling_device { void *devdata; bool removed; };

#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define to_delayed_work(p) container_of(p, struct delayed_work, work)
#define INIT_LIST_HEAD(h) ((h)->next = (h)->prev = (h))
#define list_for_each_entry(p, h, member) \
	for (p = container_of((h)->next, __typeof__(*p), member); &(p)->member != (h); \
	     p = container_of((p)->member.next, __typeof__(*p), member))
#define mutex_lock(p) assert(pthread_mutex_lock(p) == 0)
#define mutex_unlock(p) assert(pthread_mutex_unlock(p) == 0)
#define raw_spin_lock_irqsave(p, flags) do { (flags) = 0; mutex_lock(p); } while (0)
#define raw_spin_unlock_irqrestore(p, flags) do { (void)(flags); mutex_unlock(p); } while (0)
#define cpumask_clear(m) ((m)->bits = 0)
#define cpumask_copy(a, b) ((a)->bits = (b)->bits)
#define cpumask_set_cpu(cpu, m) ((m)->bits |= 1UL << (cpu))
#define cpumask_clear_cpu(cpu, m) ((m)->bits &= ~(1UL << (cpu)))
#define cpumask_empty(m) (!(m)->bits)
#define cpumask_or(a, b, c) ((a)->bits = (b)->bits | (c)->bits)
#define cpumask_andnot(a, b, c) ((a)->bits = (b)->bits & ~(c)->bits)
#define cpumask_subset(a, b) (!((a)->bits & ~(b)->bits))
#define for_each_cpu(cpu, mask) for ((cpu) = 0; (cpu) < 8; (cpu)++) if ((mask)->bits & (1UL << (cpu)))
#define msecs_to_jiffies(ms) (ms)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define PAUSE_THERMAL 2
#define THERMAL_PAUSE_RETRIES 5

static void list_add_tail(struct list_head *entry, struct list_head *head)
{
	entry->next = head;
	entry->prev = head->prev;
	head->prev->next = entry;
	head->prev = entry;
}

/* STAGED_STRUCTS */

static pthread_mutex_t cpus_pause_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cpu_hot_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t walt_drain_pending_lock = PTHREAD_MUTEX_INITIALIZER;
static struct thermal_pause_ctrl *pause_owner;
static struct cpu_hot_ctrl *hot_owner;
static cpumask_t cpus_in_max_cooling_level;
static int thermal_pause_notifier;
static unsigned long hardware_paused, online_cpus, fail_pause, fail_resume, fail_offline, fail_online;
static unsigned long pause_calls, resume_calls, notifications, offline_calls, online_calls;
static void (*during_operation)(void);

static void mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *work, int delay)
{
	assert(!wq->destroyed);
	(void)delay;
	work->work.queued = true;
}
static void queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	assert(!wq->destroyed);
	work->queued = true;
}
static void cancel_delayed_work_sync(struct delayed_work *work) { work->work.queued = false; }
static void cancel_work_sync(struct work_struct *work) { work->queued = false; }
static void destroy_workqueue(struct workqueue_struct *wq) { assert(!wq->destroyed); wq->destroyed = true; }
static void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	assert(!cdev->removed);
	cdev->removed = true;
}
static void of_node_put(struct device_node *np) { assert(np->refs); np->refs--; }
static void cpuhp_remove_state_nocalls(int state) { assert(state >= 0); }
static bool cpu_online(unsigned int cpu) { return online_cpus & (1UL << cpu); }
static int cpu_hot_hp_online(unsigned int cpu);

static int walt_pause_cpus(cpumask_t *mask, int client)
{
	assert(client == PAUSE_THERMAL && __builtin_popcountl(mask->bits) == 1);
	pause_calls++;
	if (during_operation) {
		void (*callback)(void) = during_operation;
		during_operation = NULL;
		callback();
	}
	if (mask->bits & fail_pause)
		return -EAGAIN;
	assert(!(hardware_paused & mask->bits));
	hardware_paused |= mask->bits;
	mask->bits = 0; /* WALT is allowed to mutate the request. */
	return 0;
}
static int walt_resume_cpus(cpumask_t *mask, int client)
{
	assert(client == PAUSE_THERMAL && __builtin_popcountl(mask->bits) == 1);
	assert((hardware_paused & mask->bits) == mask->bits);
	resume_calls++;
	if (mask->bits & fail_resume)
		return -EAGAIN;
	hardware_paused &= ~mask->bits;
	mask->bits = 0;
	return 0;
}
static void blocking_notifier_call_chain(void *head, bool pause, void *data)
{
	unsigned int cpu = (unsigned long)data;
	assert(head && cpu < 8);
	assert(!!(cpus_in_max_cooling_level.bits & (1UL << cpu)) == pause);
	assert(hardware_paused == cpus_in_max_cooling_level.bits);
	/* A real observer may reenter the driver's state lock. */
	mutex_lock(&cpus_pause_lock);
	mutex_unlock(&cpus_pause_lock);
	notifications++;
}
static int remove_cpu(unsigned int cpu)
{
	offline_calls++;
	if (during_operation) {
		void (*callback)(void) = during_operation;
		during_operation = NULL;
		callback();
	}
	if (fail_offline & (1UL << cpu))
		return -EBUSY;
	assert(cpu_online(cpu));
	online_cpus &= ~(1UL << cpu);
	return 0;
}
static int add_cpu(unsigned int cpu)
{
	online_calls++;
	if (fail_online & (1UL << cpu))
		return -EBUSY;
	int ret = cpu_hot_hp_online(cpu);
	if (ret)
		return ret;
	online_cpus |= 1UL << cpu;
	return 0;
}

/* STAGED_FUNCTIONS */

static struct thermal_cooling_device pause_devices[2], hot_device;
static struct thermal_pause_cdev pause_entries[2];
static struct cpu_hot_cdev hot_entry;
static struct device_node pause_nodes[2], hot_node;
static struct workqueue_struct pause_queue, hot_queue;
static struct thermal_pause_ctrl pause_ctrl;
static struct cpu_hot_ctrl hot_ctrl;
static unsigned long overlap_cases, hotplug_cases, drain_cases;

static void init_pause(unsigned long a, unsigned long b)
{
	memset(&pause_ctrl, 0, sizeof(pause_ctrl));
	memset(pause_entries, 0, sizeof(pause_entries));
	memset(pause_devices, 0, sizeof(pause_devices));
	memset(&pause_queue, 0, sizeof(pause_queue));
	INIT_LIST_HEAD(&pause_ctrl.cdevs);
	pause_ctrl.wq = &pause_queue;
	for (unsigned int i = 0; i < 2; i++) {
		pause_entries[i].ctrl = &pause_ctrl;
		pause_entries[i].cpu_mask.bits = i ? b : a;
		pause_entries[i].cdev = &pause_devices[i];
		pause_entries[i].np = &pause_nodes[i];
		pause_nodes[i].refs = 1;
		pause_devices[i].devdata = &pause_entries[i];
		list_add_tail(&pause_entries[i].node, &pause_ctrl.cdevs);
	}
	pause_owner = &pause_ctrl;
	cpus_in_max_cooling_level.bits = hardware_paused = fail_pause = fail_resume = 0;
	pause_calls = resume_calls = notifications = 0;
}

static void pump_pause(void)
{
	unsigned int n = 0;
	while (atomic_exchange(&pause_ctrl.work.work.queued, false)) {
		assert(n++ < 100000);
		thermal_pause_update_work(&pause_ctrl.work.work);
	}
}

static void request_pause(unsigned int dev, bool state)
{
	assert(!thermal_pause_set_cur_state(&pause_devices[dev], state));
	pump_pause();
}

static void cancel_pause_during_apply(void)
{
	assert(!thermal_pause_set_cur_state(&pause_devices[0], 0));
}

static void test_pause(void)
{
	for (unsigned int a = 1; a < 256; a++)
		for (unsigned int b = 1; b < 256; b++) {
			init_pause(a, b);
			request_pause(0, true);
			assert(hardware_paused == a);
			request_pause(1, true);
			assert(hardware_paused == (a | b));
			assert(pause_calls == (unsigned)__builtin_popcount(a | b));
			request_pause(0, false);
			assert(hardware_paused == b);
			assert(pause_entries[0].applied == ((a & b) == a));
			request_pause(1, false);
			assert(hardware_paused == 0);
			assert(pause_calls == resume_calls && notifications == pause_calls + resume_calls);
			thermal_pause_cleanup(&pause_ctrl);
			assert(pause_nodes[0].refs == 0 && pause_nodes[1].refs == 0);
			overlap_cases++;
		}
	init_pause(0x3c, 0xc0);
	fail_pause = 0x04;
	request_pause(0, true);
	assert(hardware_paused == 0x38 && !pause_entries[0].applied);
	assert(pause_calls == 3 + THERMAL_PAUSE_RETRIES + 1);
	fail_pause = 0;
	request_pause(0, true);
	assert(hardware_paused == 0x3c && pause_entries[0].applied);
	fail_resume = 0x3c;
	request_pause(0, false);
	unsigned long state;
	assert(!thermal_pause_get_cur_state(&pause_devices[0], &state));
	assert(state == 1 && hardware_paused == 0x3c);
	assert(resume_calls == 4 * (THERMAL_PAUSE_RETRIES + 1));
	fail_resume = 0;
	request_pause(0, false);
	assert(!thermal_pause_get_cur_state(&pause_devices[0], &state));
	assert(!state && !hardware_paused);
	request_pause(0, true);
	thermal_pause_cleanup(&pause_ctrl);
	assert(!hardware_paused && !pause_owner);
	assert(thermal_pause_set_cur_state(&pause_devices[0], 1) == -ENODEV);
	init_pause(0x3c, 0xc0);
	during_operation = cancel_pause_during_apply;
	request_pause(0, true);
	assert(!hardware_paused && !pause_entries[0].applied);
	assert(thermal_pause_set_cur_state(&pause_devices[0], 2) == -EINVAL);
	thermal_pause_cleanup(&pause_ctrl);
}

static void init_hot(unsigned long online)
{
	memset(&hot_ctrl, 0, sizeof(hot_ctrl));
	memset(&hot_entry, 0, sizeof(hot_entry));
	memset(&hot_device, 0, sizeof(hot_device));
	memset(&hot_queue, 0, sizeof(hot_queue));
	INIT_LIST_HEAD(&hot_ctrl.cdevs);
	hot_ctrl.hp_state = 1;
	hot_ctrl.wq = &hot_queue;
	hot_entry.ctrl = &hot_ctrl;
	hot_entry.cpu_id = 3;
	hot_entry.cdev = &hot_device;
	hot_entry.np = &hot_node;
	hot_node.refs = 1;
	hot_device.devdata = &hot_entry;
	list_add_tail(&hot_entry.node, &hot_ctrl.cdevs);
	hot_owner = &hot_ctrl;
	online_cpus = online;
	fail_online = fail_offline = offline_calls = online_calls = 0;
}

static void pump_hot(void)
{
	unsigned int count = 0;
	while (atomic_exchange(&hot_ctrl.work.queued, false)) {
		assert(count++ < 10);
		cpu_hot_execute_cdev(&hot_ctrl.work);
	}
}
static void request_hot(bool state)
{
	assert(!cpu_hot_set_cur_state(&hot_device, state));
	pump_hot();
}
static void cancel_hot_during_apply(void)
{
	assert(!cpu_hot_set_cur_state(&hot_device, 0));
}

static void test_hotplug(void)
{
	for (unsigned int online = 0; online < 256; online++) {
		init_hot(online);
		request_hot(true);
		assert(!cpu_online(3));
		assert(hot_entry.offlined_by_us == !!(online & 8));
		assert(cpu_hot_hp_online(3) == -EBUSY);
		request_hot(false);
		assert(online_cpus == online);
		assert(offline_calls == online_calls);
		cpu_hot_cleanup(&hot_ctrl);
		assert(online_cpus == online && !hot_owner && !hot_node.refs);
		hotplug_cases++;
	}
	init_hot(0xff);
	fail_offline = 8;
	request_hot(true);
	assert(cpu_online(3) && !hot_entry.offlined_by_us);
	fail_offline = 0;
	request_hot(true);
	assert(!cpu_online(3) && hot_entry.offlined_by_us);
	fail_online = 8;
	request_hot(false);
	assert(!cpu_online(3) && hot_entry.offlined_by_us);
	fail_online = 0;
	cpu_hot_cleanup(&hot_ctrl);
	assert(cpu_online(3));
	init_hot(0xff);
	during_operation = cancel_hot_during_apply;
	request_hot(true);
	assert(cpu_online(3) && !hot_entry.offlined_by_us);
	assert(cpu_hot_set_cur_state(&hot_device, 2) == -EINVAL);
	cpu_hot_cleanup(&hot_ctrl);
}

static void test_drain(void)
{
	for (unsigned int a = 0; a < 256; a++)
		for (unsigned int b = 0; b < 256; b++) {
			cpumask_t pending = {a}, batch;
			assert(walt_drain_pending(&pending) == !!a);
			walt_take_drain_requests(&pending, &batch);
			assert(batch.bits == a && !pending.bits);
			pending.bits |= b;
			/* Finishing batch A must leave even same-CPU requests in B pending. */
			assert(walt_drain_pending(&pending) == !!b);
			walt_take_drain_requests(&pending, &batch);
			assert(batch.bits == b && !pending.bits);
			drain_cases++;
		}
}

int main(void)
{
	test_pause();
	test_hotplug();
	test_drain();
	printf("{\"overlap_pairs\":%lu,\"hotplug_initial_masks\":%lu,\"drain_request_pairs\":%lu,"
	       "\"failure_and_reentrant_request_checks\":true}\n",
	       overlap_cases, hotplug_cases, drain_cases);
}
