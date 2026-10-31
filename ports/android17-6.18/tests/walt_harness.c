// SPDX-License-Identifier: GPL-2.0-only
/* Host adapters for the actual staged helper definitions, inserted below. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef int spinlock_t;
typedef struct { int counter; } atomic_t;
typedef struct { unsigned long bits; } cpumask_t;
struct list_head { struct list_head *next, *prev; };
struct sched_entity { bool sched_delayed; };
struct task_struct { u64 android_vendor_data1[6]; struct sched_entity se; u64 canary; };
struct task_struct init_task;
static u64 vendor_data_pad[64];
static size_t arch_task_struct_size;
struct walt_related_thread_group;

#define __rcu
#define __iomem
#define WALT_NR_CPUS 8
#define NR_CPUS 8
#define MAX_CLUSTERS 4
#define RAVG_HIST_SIZE 5
#define NUM_BUSY_BUCKETS 16
#define ENQUEUE_DELAYED 0x200
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define per_cpu(array, cpu) ((array)[cpu])
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, y) ((x) = (y))
#define smp_load_acquire(p) (*(p))
#define spin_lock_irqsave(lock, flags) ((void)(lock), (flags) = 0)
#define spin_unlock_irqrestore(lock, flags) ((void)(lock), (void)(flags))
#define unlikely(x) (x)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define mult_frac(x, n, d) (((x) / (d)) * (n) + ((x) % (d)) * (n) / (d))
#define trace_update_cpu_capacity(...) ((void)0)
#define pr_debug(...) ((void)0)
#define UCLAMP_MIN 0
#define UCLAMP_MAX 1

struct walt_sched_cluster {
	unsigned long walt_internal_freq_limit, max_freq, max_possible_freq;
	unsigned int id;
};
struct walt_rq { unsigned long cpu_capacity_orig, freq_pressure; };
struct rq { unsigned long pressure, uclamp[2]; };
static struct walt_rq walt_rq[8];
static struct rq runqueues[8];
static struct walt_sched_cluster clusters[8];
static unsigned long capacities[8], references[8];
static bool walt_disabled, clamp_used;
static bool cpufreq_ready, gclk_ready;
#define cpu_rq(cpu) (&runqueues[cpu])
#define cpu_cluster(cpu) (&clusters[cpu])
#define arch_scale_cpu_capacity(cpu) (capacities[cpu])
#define arch_scale_freq_ref(cpu) (references[cpu])
#define hw_load_avg(rq) ((rq)->pressure)
#define uclamp_is_used() (clamp_used)
#define uclamp_rq_get(rq, id) ((rq)->uclamp[id])
#define topology_core_id(cpu) (cpu)
#define topology_cluster_id(cpu) (cpu)
#define for_each_cpu(cpu, mask) for ((cpu) = 0; (cpu) < 8; (cpu)++) if ((mask)->bits & (1UL << (cpu)))
static int cpumask_first(const cpumask_t *mask) { return __builtin_ctzl(mask->bits); }
struct cpufreq_policy { cpumask_t *related_cpus; unsigned long max; };
static u32 readl_relaxed(const void *p) { u32 value; memcpy(&value, p, sizeof(value)); return value; }
static u64 readq_relaxed(const void *p) { u64 value; memcpy(&value, p, sizeof(value)); return value; }

struct ctl_table { int unused; };
struct ctl_table_header { bool live; };
static struct ctl_table_header registration_pool[18];
static unsigned int num_sched_clusters, registration_attempts, fail_at, live_registrations;
static struct ctl_table_header *register_sysctl_sz(const char *path,
		const struct ctl_table *table, size_t count)
{
	unsigned int i = registration_attempts++;
	assert(path && table && count && i < 18);
	if (i == fail_at)
		return NULL;
	assert(!registration_pool[i].live);
	registration_pool[i].live = true;
	live_registrations++;
	return &registration_pool[i];
}
static void unregister_sysctl_table(struct ctl_table_header *hdr)
{
	assert(hdr && hdr->live && live_registrations);
	hdr->live = false;
	live_registrations--;
}

/* STAGED_DEFINITIONS */

static unsigned long capacity_cases, clamp_cases, counter_cases, storage_cases, sysctl_cases;

static void test_storage(void)
{
	assert(sizeof(struct walt_task_struct) <= 512);
	arch_task_struct_size = sizeof(struct task_struct) + 512;
	for (unsigned int i = 0; i < 4096; i++) {
		struct task_struct *p = malloc(arch_task_struct_size);
		memset(p, 0xa5, arch_task_struct_size);
		android_init_dynamic_vendor_data(p);
		assert((void *)task_walt(p) == (void *)(p + 1));
		assert(wts_to_ts(task_walt(p)) == p);
		assert(p->canary == UINT64_C(0xa5a5a5a5a5a5a5a5));
		for (size_t j = 0; j < 512; j++)
			assert(((unsigned char *)(p + 1))[j] == 0);
		task_walt(p)->last_enqueued_ts = i;
		assert(p->android_vendor_data1[0] == UINT64_C(0xa5a5a5a5a5a5a5a5));
		free(p);
		storage_cases++;
	}
	memset(vendor_data_pad, 0xa5, sizeof(vendor_data_pad));
	android_init_dynamic_vendor_data(&init_task);
	assert(task_walt(&init_task) == (void *)vendor_data_pad);
	assert(wts_to_ts(task_walt(&init_task)) == &init_task);
	assert(task_walt(&init_task)->last_enqueued_ts == 0);
	storage_cases++;
	/* EEVDF sleep, forced removal, and wake before delayed removal. */
	struct task_struct p = {0};
	int runnable = 0;
	runnable += walt_task_enqueue_runnable(&p, 0);
	runnable -= walt_task_dequeue_runnable(&p);
	p.se.sched_delayed = true;
	assert(runnable == 0);
	assert(!walt_task_dequeue_runnable(&p));
	assert(!walt_task_enqueue_runnable(&p, 0));
	runnable += walt_task_enqueue_runnable(&p, ENQUEUE_DELAYED);
	p.se.sched_delayed = false;
	assert(runnable == 1);
	runnable -= walt_task_dequeue_runnable(&p);
	assert(runnable == 0);
}

static void test_capacity(void)
{
	cpumask_t mask = { .bits = 0xff };
	struct cpufreq_policy policy = { .related_cpus = &mask };
	for (unsigned long cap = 1; cap <= 1024; cap += 3) {
		for (unsigned long limit = 0; limit <= 4800; limit += 150) {
			for (int cpu = 0; cpu < 8; cpu++) {
				capacities[cpu] = cap;
				references[cpu] = 3600;
				clusters[cpu].max_possible_freq = 3600;
				clusters[cpu].max_freq = limit;
			}
			policy.max = limit;
			walt_update_freq_pressure(&policy);
			unsigned long expected_pressure = limit >= 3600 ? 0 : cap - cap * limit / 3600;
			for (int cpu = 0; cpu < 8; cpu++)
				assert(walt_rq[cpu].freq_pressure == expected_pressure);
			for (unsigned long hw = 0; hw <= cap + 32; hw += 17) {
				runqueues[0].pressure = hw;
				unsigned long actual = cap - min(cap - 1, max(hw, expected_pressure));
				assert(walt_actual_cpu_capacity(0) == actual);
				for (unsigned long internal = 0; internal <= 3600; internal += 400) {
					clusters[0].walt_internal_freq_limit = internal;
					unsigned long restricted = internal == 3600 ? cap : cap * min(internal, limit) / 3600;
					restricted = max(1UL, min(restricted, actual));
					for (unsigned int pressure = 0; pressure < 3; pressure++) {
						unsigned long before = max(1UL, actual / (pressure + 1));
						unsigned long after = before;
						android_rvh_update_cpu_capacity(NULL, 0, &after);
						assert(walt_rq[0].cpu_capacity_orig == restricted);
						assert(after == max(1L, (long)before - (long)(actual - restricted)));
						assert(after && after <= before);
						capacity_cases++;
					}
				}
			}
		}
	}
	for (unsigned long lo = 0; lo <= 1280; lo += 16)
		for (unsigned long hi = 0; hi <= 1280; hi += 16)
			for (unsigned long util = 0; util <= 1280; util += 16) {
				runqueues[0].uclamp[0] = lo;
				runqueues[0].uclamp[1] = hi;
				clamp_used = false;
				assert(walt_uclamp_rq_util(&runqueues[0], util) == util);
				clamp_used = true;
				unsigned long result = walt_uclamp_rq_util(&runqueues[0], util);
				assert(result >= lo);
				assert(result <= max(lo, hi));
				if (util >= lo && util <= hi)
					assert(result == util);
				clamp_cases++;
			}
}

static void test_counters(void)
{
	static const struct walt_cpufreq_soc_data data = {
		.reg_cycle_cntr = 32, .accumulative_counter = true,
	};
	unsigned char *mapping = calloc(1, 4096);
	assert(mapping);
	for (unsigned int cpu = 0; cpu < 8; cpu++) {
		clusters[cpu].id = 0;
		cpufreq_data[0].base = mapping;
		cpufreq_data[0].soc_data = &data;
	}
	cpufreq_ready = true;
	u64 expected[8] = {0};
	u32 hardware[8] = {0};
	u32 random = 0x8f012;
	for (unsigned int i = 0; i < 1000000; i++) {
		unsigned int cpu = i % 8;
		random = random * 1664525U + 1013904223U;
		u32 step = i < 8 ? UINT32_MAX : random;
		hardware[cpu] += step;
		expected[cpu] += step;
		memcpy(mapping + 32 + (cpu + 1) * 4, &hardware[cpu], 4);
		assert(walt_cpufreq_get_cpu_cycle_counter(cpu, 0) == expected[cpu]);
		counter_cases++;
	}
	cpufreq_ready = false;
	free(mapping);
	for (unsigned int cpu = 0; cpu < 8; cpu++)
		assert(walt_cpufreq_get_cpu_cycle_counter(cpu, 0) == expected[cpu]);
	gclk_ready = true;
	u64 *wide = malloc(sizeof(*wide));
	assert(wide);
	ncc_data[0].base = wide;
	const u64 samples[] = {0, UINT64_MAX - 1, UINT64_MAX, 0, 1, 0x8000000000000000ULL};
	for (unsigned int i = 0; i < ARRAY_SIZE(samples); i++) {
		*wide = samples[i];
		assert(walt_get_ncc_gclk_cycle_counter(0, 0) == samples[i]);
		counter_cases++;
	}
	gclk_ready = false;
	free(wide);
	assert(walt_get_ncc_gclk_cycle_counter(0, 0) == samples[ARRAY_SIZE(samples) - 1]);
}

static void test_sysctl(void)
{
	const unsigned int expected[] = {2, 3, 6, 11, 18};
	for (num_sched_clusters = 1; num_sched_clusters <= 4; num_sched_clusters++) {
		for (fail_at = 0; fail_at <= expected[num_sched_clusters]; fail_at++) {
			registration_attempts = live_registrations = 0;
			memset(registration_pool, 0, sizeof(registration_pool));
			memset(walt_sysctl_headers, 0, sizeof(walt_sysctl_headers));
			int ret = walt_register_sysctl();
			if (fail_at < expected[num_sched_clusters]) {
				assert(ret == -ENOMEM);
				assert(live_registrations == 0);
			} else {
				assert(ret == 0);
				assert(live_registrations == expected[num_sched_clusters]);
			}
			sysctl_cases++;
		}
	}
}

int main(void)
{
	test_storage();
	test_capacity();
	test_counters();
	test_sysctl();
	printf("{\"task_bytes\":%zu,\"storage_cases\":%lu,\"capacity_cases\":%lu,"
	       "\"uclamp_cases\":%lu,\"counter_cases\":%lu,\"sysctl_cases\":%lu}\n",
	       sizeof(struct walt_task_struct), storage_cases, capacity_cases,
	       clamp_cases, counter_cases, sysctl_cases);
}
