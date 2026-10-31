#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct net_device { bool rmnet; unsigned int dropped; };
struct sk_buff {
	struct sk_buff *next, *prev;
	struct net_device *dev;
	bool freed;
};
struct sk_buff_head {
	struct sk_buff *head, *tail;
	unsigned int qlen;
	int lock;
};
struct softnet_data {
	struct sk_buff_head input_pkt_queue, process_queue;
	unsigned int input_queue_head, drop_counters;
};
struct notifier_block {};
static struct softnet_data sds[4];
static struct net_device devs[2];
static struct sk_buff skbs[32];
static unsigned int configured_threshold, active_mask, work_calls, rtnl_calls;
static unsigned long rmnet_drops;
static int current_cpu, bh_depth, irq_depth, process_depth, cpu_lock;
static bool rtnl_busy, rtnl_owned;
#define DATARMNET1ad396a890 configured_threshold
#define DATARMNET68d84e7b98 (&rmnet_drops)
#define DATARMNETf8de7cb644 0
#define NOTIFY_DONE 0
#define READ_ONCE(x) (x)
#define cpu_online(cpu) (active_mask & (1U << (cpu)))
#define smp_processor_id() current_cpu
#define cpus_read_lock() assert(!"OOM must not acquire blocking CPU-hotplug exclusion")
#define work_on_cpu(...) (assert(!"OOM must not wait for cross-CPU work"), 0L)
#define skb_queue_walk_safe(q, skb, tmp) \
	for ((skb) = (q)->head; (skb) && ((tmp) = (skb)->next, true); (skb) = (tmp))
static void __skb_queue_head_init(struct sk_buff_head *q) { memset(q, 0, sizeof(*q)); }
static unsigned int skb_queue_len(struct sk_buff_head *q) { return q->qlen; }
static void __skb_queue_tail(struct sk_buff_head *q, struct sk_buff *skb)
{
	skb->prev = q->tail; skb->next = NULL;
	if (q->tail) q->tail->next = skb; else q->head = skb;
	q->tail = skb; q->qlen++;
}
static void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *q)
{
	assert(q->qlen);
	if (skb->prev) skb->prev->next = skb->next; else q->head = skb->next;
	if (skb->next) skb->next->prev = skb->prev; else q->tail = skb->prev;
	q->qlen--;
}
static void __skb_queue_purge(struct sk_buff_head *q)
{
	struct sk_buff *skb, *next;
	assert(!bh_depth && !irq_depth && !process_depth);
	skb_queue_walk_safe(q, skb, next) {
		assert(skb->dev->rmnet && !skb->freed);
		__skb_unlink(skb, q); skb->freed = true;
	}
}
static bool rmnet_is_real_dev_registered(struct net_device *dev)
{
	assert(rtnl_owned && bh_depth && irq_depth && process_depth);
	return dev->rmnet;
}
static void rps_input_queue_head_incr(struct softnet_data *sd)
{
	assert(current_cpu >= 0 && sd == &sds[current_cpu]);
	sd->input_queue_head++;
}
static void numa_drop_add(unsigned int *counter, unsigned int n) { *counter += n; }
static void dev_core_stats_rx_dropped_inc(struct net_device *dev) { dev->dropped++; }
static void local_bh_disable(void) { assert(!bh_depth); bh_depth++; }
static void local_bh_enable(void) { assert(bh_depth == 1); bh_depth--; }
static struct softnet_data *cpu_sd(void)
{
	assert(current_cpu >= 0 && bh_depth);
	return &sds[current_cpu];
}
#define this_cpu_ptr(ignored) cpu_sd()
static void spin_lock_irq(int *lock)
{
	assert(bh_depth && !irq_depth && lock == &sds[current_cpu].input_pkt_queue.lock);
	irq_depth++;
}
static void spin_unlock_irq(int *lock)
{
	assert(lock == &sds[current_cpu].input_pkt_queue.lock && irq_depth == 1 && !process_depth);
	irq_depth--;
}
static void process_lock(void) { assert(bh_depth && irq_depth && !process_depth); process_depth++; }
static void process_unlock(void) { assert(process_depth == 1); process_depth--; }
#define local_lock_nested_bh(ignored) process_lock()
#define local_unlock_nested_bh(ignored) process_unlock()
static bool rtnl_trylock(void)
{
	rtnl_calls++;
	if (rtnl_busy) return false;
	assert(!rtnl_owned); rtnl_owned = true; return true;
}
static void rtnl_unlock(void) { assert(rtnl_owned); rtnl_owned = false; }
#include "oom.inc"

static void reset(void)
{
	memset(sds, 0, sizeof(sds)); memset(skbs, 0, sizeof(skbs));
	memset(devs, 0, sizeof(devs)); devs[1].rmnet = true;
	configured_threshold = 0; active_mask = 1; current_cpu = 0; work_calls = rtnl_calls = 0;
	rmnet_drops = 0; bh_depth = irq_depth = process_depth = cpu_lock = 0;
	rtnl_busy = rtnl_owned = false;
}
static void insert(int cpu, int id, bool process, bool rmnet)
{
	skbs[id].dev = &devs[rmnet];
	__skb_queue_tail(process ? &sds[cpu].process_queue : &sds[cpu].input_pkt_queue, &skbs[id]);
}
static void balanced(void)
{
	assert(!rtnl_owned && !bh_depth && !irq_depth && !process_depth);
	assert(!work_calls);
}
int main(void)
{
	unsigned int cases = 0;
	unsigned long initial = 1UL << 40;
	for (unsigned int limit = 0; limit <= 5; limit++) {
		for (unsigned int mask = 0; mask < 16; mask++) {
			unsigned long progress = initial;
			unsigned int removed = 0, input_left = 2;
			bool expected[4] = { false };
			reset(); configured_threshold = limit;
			for (int i = 0; i < 4; i++) insert(0, i, i >= 2, mask & (1U << i));
			if (limit && limit <= 4) {
				for (int i = 0; i < 2; i++) {
					expected[i] = mask & (1U << i);
					if (expected[i]) { input_left--; removed++; }
				}
				if (input_left >= limit) {
					for (int i = 2; i < 4; i++) {
						expected[i] = mask & (1U << i);
						removed += expected[i];
					}
				}
			}
			assert(DATARMNET2aa4ef6ff9(NULL, 0, &progress) == NOTIFY_DONE);
			assert(progress == initial + !!removed);
			assert(sds[0].input_queue_head == removed && sds[0].drop_counters == removed);
			assert(devs[1].dropped == removed && devs[0].dropped == 0 && rmnet_drops == removed);
			for (int i = 0; i < 4; i++) assert(skbs[i].freed == expected[i]);
			assert(sds[0].input_pkt_queue.qlen + sds[0].process_queue.qlen == 4 - removed);
			assert(!work_calls && rtnl_calls == !!limit && current_cpu == 0); balanced(); cases++;
		}
	}
	reset(); configured_threshold = 1; active_mask = 9; current_cpu = 3;
	insert(0, 0, false, true); insert(3, 1, false, true); insert(1, 2, false, true);
	unsigned long progress = initial;
	DATARMNET2aa4ef6ff9(NULL, 0, &progress);
	assert(progress == initial + 1 && !work_calls && current_cpu == 3);
	assert(!skbs[0].freed && skbs[1].freed && !skbs[2].freed); balanced(); cases++;
	reset(); configured_threshold = 1; rtnl_busy = true; insert(0, 0, false, true);
	progress = initial; DATARMNET2aa4ef6ff9(NULL, 0, &progress);
	assert(progress == initial && !skbs[0].freed && !work_calls); balanced(); cases++;
	reset(); configured_threshold = 1; active_mask = 9; current_cpu = 1;
	insert(1, 0, false, true); progress = initial;
	DATARMNET2aa4ef6ff9(NULL, 0, &progress);
	assert(progress == initial && !skbs[0].freed && !work_calls); balanced(); cases++;
	/* OOM entered under CPU-hotplug write exclusion must not wait on it. */
	reset(); configured_threshold = 1; cpu_lock = 1;
	insert(0, 0, false, true); insert(3, 1, false, true); progress = initial;
	DATARMNET2aa4ef6ff9(NULL, 0, &progress);
	assert(progress == initial + 1 && cpu_lock == 1);
	assert(skbs[0].freed && !skbs[1].freed); balanced(); cases++;
	printf("{\"oom\":\"PASS\",\"cases\":%u}\n", cases);
	return 0;
}
