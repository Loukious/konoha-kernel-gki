/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef uint16_t u16;
typedef uint64_t u64;
#define __user
#define __kernel
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define __GFP_NORETRY 0
#define __GFP_NOWARN 0
#define EXPORT_SYMBOL_GPL(name)
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define min_t(t, a, b) min((t)(a), (t)(b))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define unlikely(x) (x)
#define smp_wmb() atomic_thread_fence(memory_order_release)
#define smp_store_release(p, v) __atomic_store_n(p, v, __ATOMIC_RELEASE)
#define __KFIFO_PEEK(data, out, mask) ((data)[(out) & (mask)])
#define __KFIFO_POKE(data, in, mask, val) ((data)[(in) & (mask)] = (unsigned char)(val))
#define scnprintf snprintf /* All uses here have a statically bounded numeric prefix. */
struct mutex { pthread_mutex_t native; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->native, NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->native)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->native)); }
struct __kfifo { unsigned int in, out, mask, esize; void *data; };
struct kfifo_rec_ptr_2 { struct __kfifo kfifo; };
struct miev_device { struct kfifo_rec_ptr_2 fifo; struct mutex lock; int wait_queue; };
struct file { int unused; };
static struct miev_device device, *miev_dev = &device;
#define FIFO_SIZE (64 * 1024)
#define BUF_MAX_SIZE 4096
#define kfifo_size(f) ((f)->kfifo.mask + 1)
#define kfifo_len(f) ((f)->kfifo.in - (f)->kfifo.out)
#define kfifo_is_empty(f) ((f)->kfifo.in == (f)->kfifo.out)
#define kfifo_peek_len(f) __kfifo_len_r(&(f)->kfifo, 2)
#define kfifo_in(f, p, n) __kfifo_in_r(&(f)->kfifo, p, n, 2)
#define kfifo_out(f, p, n) __kfifo_out_r(&(f)->kfifo, p, n, 2)
#define kfifo_to_user(f, p, n, copied) __kfifo_to_user_r(&(f)->kfifo, p, n, copied, 2)
#define wake_up_interruptible(q) ((void)(q))
static atomic_int allocations;
static int fail_after = -1, fail_copy_at, copy_calls;
static bool fail_user;
static void *kmalloc(size_t size, int flags)
{
	if (fail_after >= 0 && !fail_after--)
		return NULL;
	void *p = malloc(size);
	if (p)
		atomic_fetch_add(&allocations, 1);
	return p;
}
static void *kzalloc(size_t size, int flags)
{
	void *p = kmalloc(size, flags);
	if (p)
		memset(p, 0, size);
	return p;
}
static void *kcalloc(size_t n, size_t size, int flags) { return kzalloc(n * size, flags); }
static void kfree(void *p)
{
	if (p) {
		atomic_fetch_sub(&allocations, 1);
		free(p);
	}
}
static unsigned long copy_to_user(void *to, const void *from, size_t size)
{
	if (++copy_calls == fail_copy_at)
		return size;
	memcpy(to, from, size);
	return 0;
}
static void *memdup_user(const void *from, size_t size)
{
	if (fail_user)
		return ERR_PTR(-EFAULT);
	void *p = kmalloc(size, 0);
	if (!p)
		return ERR_PTR(-ENOMEM);
	memcpy(p, from, size);
	return p;
}
static long long ktime_get_real_seconds(void) { return 123; }
#define TASK_COMM_LEN 16
static struct { int pid; char comm[16]; } task = { 10, "test" }, *current = &task;
#define get_task_comm(buf, task) memcpy(buf, (task)->comm, TASK_COMM_LEN)
static pthread_mutex_t bootprof_lock = PTHREAD_MUTEX_INITIALIZER;
#define spin_lock_irqsave(lock, flags) do { flags = 0; assert(!pthread_mutex_lock(lock)); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); assert(!pthread_mutex_unlock(lock)); } while (0)
static atomic_ullong ticks;
static u64 sched_clock(void) { return atomic_fetch_add(&ticks, 1); }
/* DRIVER_TYPES */
struct log_t *bootprof[BUF_COUNT];
unsigned long log_count;
static bool enabled;
/* DRIVER_CODE */

static void reset_fifo(unsigned int index)
{
	device.fifo.kfifo.in = index;
	device.fifo.kfifo.out = index;
}

static void test_fifo(void)
{
	char input[4096], output[4096];
	unsigned int indices[] = { 0, 65530, UINT_MAX - 8 };
	loff_t offset = 0;

	for (size_t t = 0; t < sizeof(indices) / sizeof(*indices); t++) {
		reset_fifo(indices[t]);
		for (int i = 0; i < 40; i++) {
			memset(input, i, sizeof(input));
			assert(write_kbuf(input, sizeof(input)) == sizeof(input));
		}
		/* 15 maximum-sized records fit; all older records are evicted whole. */
		for (int i = 25; i < 40; i++) {
			assert(miev_read(NULL, output, sizeof(output) - 1, &offset) == -EMSGSIZE);
			assert(miev_read(NULL, output, sizeof(output), &offset) == sizeof(output));
			for (size_t j = 0; j < sizeof(output); j++)
				assert((unsigned char)output[j] == i);
		}
		assert(kfifo_is_empty(&device.fifo));
		reset_fifo(indices[t]);
		assert(write_kbuf("abcdefgh", 8) == 8);
		copy_calls = 0;
		fail_copy_at = 1;
		assert(miev_read(NULL, output, 8, &offset) == -EFAULT);
		assert(!kfifo_is_empty(&device.fifo));
		fail_copy_at = 0;
		fail_user = true;
		assert(miev_write(NULL, "bad", 3, &offset) == -EFAULT);
		fail_user = false;
		assert(miev_read(NULL, output, 8, &offset) == 8);
		assert(!memcmp(output, "abcdefgh", 8));
	}
	assert(write_kbuf(NULL, 1) == -EINVAL);
	assert(write_kbuf(input, -1) == -EINVAL);
	assert(!write_kbuf(input, 0));
	assert(!miev_read(NULL, output, 0, &offset));
	puts("Event FIFO: wrap, unsigned-index wrap, eviction, short reads and user faults passed");
}

static void read_event(char *output)
{
	loff_t offset = 0;
	ssize_t len = miev_read(NULL, output, 4096, &offset);

	assert(len > 0 && len <= 4096);
	output[len] = 0;
}

static void test_events(void)
{
	char output[4097], expected[256], value[4097];
	struct misight_mievent *a, *b;

	reset_fifo(0);
	a = cdev_tevent_alloc(UINT_MAX);
	b = cdev_tevent_alloc(7);
	assert(a && b);
	a->time = LLONG_MAX;
	assert(!cdev_tevent_add_int(a, "n", LONG_MIN));
	assert(!cdev_tevent_add_str(b, "b", "second"));
	assert(!cdev_tevent_write(a));
	read_event(output);
	snprintf(expected, sizeof(expected), "EventId %u -t %lld -paraList {\"n\":%ld}", UINT_MAX, LLONG_MAX, LONG_MIN);
	assert(!strcmp(output, expected));
	assert(!cdev_tevent_write(b));
	read_event(output);
	assert(!strcmp(output, "EventId 7 -t 123 -paraList {\"b\":\"second\"}"));
	cdev_tevent_destroy(a);
	cdev_tevent_destroy(b);
	for (int c = 1; c <= 127; c++) {
		char text[] = { c, 0 };
		a = cdev_tevent_alloc(1);
		assert(a && !cdev_tevent_add_str(a, text, text));
		assert(!cdev_tevent_write(a));
		read_event(output);
		printf("JSON %s\n", strchr(output, '{'));
		cdev_tevent_destroy(a);
	}
	/* Exact payload limit and one-byte overflow. */
	const char *prefix = "EventId 1 -t 123 -paraList {\"x\":\"\"}";
	size_t length = 4096 - strlen(prefix);
	memset(value, 'v', sizeof(value));
	value[length] = 0;
	a = cdev_tevent_alloc(1);
	assert(a && !cdev_tevent_add_str(a, "x", value));
	assert(!cdev_tevent_write(a));
	read_event(output);
	assert(strlen(output) == 4096);
	value[length] = 'v'; value[length + 1] = 0;
	assert(!cdev_tevent_add_str(a, "x", value));
	assert(cdev_tevent_write(a) == -EMSGSIZE);
	assert(a->head->next && kfifo_is_empty(&device.fifo));
	cdev_tevent_destroy(a);
	for (int i = 0; i < 2; i++) {
		fail_after = i;
		assert(!cdev_tevent_alloc(1));
		fail_after = -1;
		assert(!atomic_load(&allocations));
	}
	a = cdev_tevent_alloc(1);
	assert(a);
	fail_after = 0;
	assert(cdev_tevent_add_int(a, "x", 42) == -ENOMEM && !a->head->next);
	fail_after = -1;
	assert(!cdev_tevent_add_int(a, "x", LONG_MAX));
	fail_after = 0;
	assert(cdev_tevent_write(a) == -ENOMEM && a->head->next);
	fail_after = -1;
	assert(!cdev_tevent_write(a));
	read_event(output);
	cdev_tevent_destroy(a);
	assert(!atomic_load(&allocations));
	puts("Event formatting: independent objects, limits, ownership and allocation failures passed");
}

static void *log_thread(void *unused)
{
	for (int i = 0; i < 160; i++)
		bootprof_log_boot("parallel");
	return NULL;
}

static void test_boot_logs(void)
{
	pthread_t threads[8];
	char long_log[1024];

	enabled = true;
	log_count = 0;
	fail_after = 0;
	bootprof_log_boot("first");
	assert(!log_count && !atomic_load(&allocations));
	fail_after = 1;
	bootprof_log_boot("first");
	assert(!log_count && atomic_load(&allocations) == 1);
	fail_after = -1;
	bootprof_log_boot("first");
	assert(log_count == 1 && atomic_load(&allocations) == 2);
	memset(long_log, 'x', sizeof(long_log)); long_log[1023] = 0;
	bootprof_log_boot(long_log);
	assert(strlen(bootprof[0][1].comm_event + TASK_COMM_LEN) == BOOT_STR_SIZE - 1);
	for (int i = 0; i < 8; i++)
		assert(!pthread_create(&threads[i], NULL, log_thread, NULL));
	for (int i = 0; i < 8; i++)
		assert(!pthread_join(threads[i], NULL));
	assert(log_count == BUF_COUNT * LOGS_PER_BUF && !enabled);
	enabled = true;
	bootprof_log_boot("overflow");
	assert(log_count == BUF_COUNT * LOGS_PER_BUF && !enabled);
	bootprof_free_logs();
	assert(!atomic_load(&allocations));
	puts("Boot logs: concurrent 960-entry capacity, truncation and allocation retries passed");
}

int main(void)
{
	mutex_init(&device.lock);
	device.fifo.kfifo.mask = FIFO_SIZE - 1;
	device.fifo.kfifo.esize = 1;
	device.fifo.kfifo.data = malloc(FIFO_SIZE);
	assert(device.fifo.kfifo.data);
	test_fifo();
	test_events();
	test_boot_logs();
	free(device.fifo.kfifo.data);
	return 0;
}
