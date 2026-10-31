/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t phys_addr_t;
#define __iomem
#define __user
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define SZ_4K 4096
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) (((~0UL) >> (63 - (h))) & ((~0UL) << (l)))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define WRITE_ONCE(x, val) ((x) = (val))
#define wmb() atomic_thread_fence(memory_order_release)
#define ALIGN(n, a) (((n) + (a) - 1) & ~((a) - 1))
#define round_down(n, a) ((n) & ~((a) - 1))
#define IS_ALIGNED(n, a) (!((n) & ((a) - 1)))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define lockdep_assert_held(lock) ((void)(lock))
#define kfree free
struct mutex { pthread_mutex_t native; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->native, NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->native)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->native)); }
static pthread_mutex_t memdump_table_lock = PTHREAD_MUTEX_INITIALIZER;
#define spin_lock_irqsave(lock, flags) do { (flags) = 0; assert(!pthread_mutex_lock(lock)); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); assert(!pthread_mutex_unlock(lock)); } while (0)
struct device { void *data; };
static void *dev_get_drvdata(struct device *d) { return d->data; }
enum msm_dump_table_ids { MSM_DUMP_TABLE_APPS, MSM_DUMP_TABLE_MAX = 0x150 };
enum msm_dump_type { MSM_DUMP_TYPE_DATA, MSM_DUMP_TYPE_TABLE };
/* DRIVER_TYPES */
static struct msm_memory_dump memdump;
struct dcc_drvdata { struct mutex mutex; void *ram_base; u32 ram_size; };
struct dcc_sram_device { struct mutex lock; struct dcc_drvdata *drvdata; };
struct file { void *private_data; };
static bool fail_allocation, fail_copy;
static atomic_bool hold_copy, copying, release_copy;
static void *kmalloc(size_t size, int flags) { return fail_allocation ? NULL : malloc(size); }
static size_t copy_to_user(void *to, const void *from, size_t size)
{
	if (fail_copy)
		return size;
	if (atomic_load(&hold_copy)) {
		atomic_store(&copying, true);
		while (!atomic_load(&release_copy))
			sched_yield();
	}
	memcpy(to, from, size);
	return 0;
}
static unsigned int __raw_readl(const void *p)
{
	unsigned int value;

	assert(IS_ALIGNED((uintptr_t)p, sizeof(value)));
	memcpy(&value, p, sizeof(value));
	return value;
}
/* DRIVER_CODE */

static void *register_thread(void *argument)
{
	uintptr_t id = (uintptr_t)argument;

	for (unsigned int i = 0; i < MAX_NUM_ENTRIES / 8; i++) {
		struct msm_dump_entry entry = { .id = id * 1000 + i, .addr = 0x10000 + id * 1000 + i };
		assert(!register_dump_table_entry(MSM_DUMP_TABLE_APPS, &entry));
	}
	return NULL;
}

static void test_tables(void)
{
	struct msm_dump_table *root = calloc(2, sizeof(*root));
	struct msm_dump_entry entry = { .id = 42, .addr = 0x12340 };
	pthread_t threads[8];

	assert(root);
	assert(!init_memory_dump(root, 0x10000000));
	assert(msm_dump_get_table(MSM_DUMP_TABLE_APPS) == root + 1);
	assert(IS_ERR(msm_dump_get_table(MSM_DUMP_TABLE_MAX)));
	assert(register_dump_table_entry(0, NULL) == -EINVAL);
	for (uintptr_t i = 0; i < 8; i++)
		assert(!pthread_create(&threads[i], NULL, register_thread, (void *)i));
	for (int i = 0; i < 8; i++)
		assert(!pthread_join(threads[i], NULL));
	assert(root[1].num_entries == MAX_NUM_ENTRIES);
	for (unsigned int i = 0; i < MAX_NUM_ENTRIES; i++) {
		assert(root[1].entries[i].addr == 0x10000 + root[1].entries[i].id);
		for (unsigned int j = i + 1; j < MAX_NUM_ENTRIES; j++)
			assert(root[1].entries[i].id != root[1].entries[j].id);
	}
	assert(register_dump_table_entry(0, &entry) == -ENOSPC);
	root->num_entries = MAX_NUM_ENTRIES + 1;
	assert(IS_ERR(msm_dump_get_table(0)));
	root->num_entries = 1;
	u64 address = root->entries[0].addr;
	const u64 bad[] = { 0, memdump.table_phys - 1, memdump.table_phys, address + 1, UINT64_MAX };
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
		root->entries[0].addr = bad[i];
		assert(IS_ERR(msm_dump_get_table(0)));
	}
	root->entries[0].addr = address;
	root->entries[0].type = MSM_DUMP_TYPE_DATA;
	assert(IS_ERR(msm_dump_get_table(0)));
	free(root);
	memdump.table = NULL;
	assert(PTR_ERR(msm_dump_get_table(0)) == -EPROBE_DEFER);
}

static void test_cpu_registers(void)
{
	unsigned char *buffer = malloc(256);
	unsigned char before[256];
	struct cpuss_regdump_data regs = { .dump_vaddr = buffer, .size = 256 };
	struct cpuss_dump_data cpu = { .cpussregdata = &regs };
	struct device dev = { .data = &cpu };
	const u32 bad[] = { 29, 1000, 1U << 29, 1U << 31, (1U << 31) + 1, UINT32_MAX };

	assert(buffer);
	mutex_init(&cpu.mutex);
	for (u32 i = 0; i < 29; i++) {
		assert(!update_reg_dump_table(&dev, i));
		struct reg_dump_data *table = (void *)buffer;
		assert(table->magic == INPUT_DATA_BY_HLOS);
		assert(table->system_regs_input_index == 5 + i * 2);
		assert(table->regdump_output_byte_offset == (6 + i * 2) * 4);
	}
	memcpy(before, buffer, sizeof(before));
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
		assert(update_reg_dump_table(&dev, bad[i]) == -ENOMEM);
		assert(!memcmp(before, buffer, sizeof(before)));
	}
	free(buffer);
}

struct async_read { struct file *file; ssize_t result; };
static void *read_thread(void *argument)
{
	struct async_read *request = argument;
	unsigned char out[8];
	loff_t pos = 1;

	request->result = dcc_sram_read(request->file, (void *)out, sizeof(out), &pos);
	return NULL;
}

static void test_sram(void)
{
	u32 words[16];
	unsigned char *ram = (void *)words, out[80];
	struct dcc_drvdata drv = { .ram_base = words, .ram_size = sizeof(words) };
	struct dcc_sram_device sram = { .drvdata = &drv };
	struct file file = { .private_data = &sram };
	unsigned int cases = 0;

	mutex_init(&drv.mutex);
	mutex_init(&sram.lock);
	for (size_t i = 0; i < sizeof(words); i++)
		ram[i] = i + 1;
	for (loff_t start = -1; start <= 65; start++) {
		for (size_t len = 0; len <= 70; len++) {
			loff_t pos = start;
			memset(out, 0xcc, sizeof(out));
			ssize_t ret = dcc_sram_read(&file, (void *)out, len, &pos);
			ssize_t expected = start < 0 ? -EINVAL : start >= 64 ? 0 : min_t(size_t, len, 64 - start);
			assert(ret == expected);
			assert(pos == start + (expected > 0 ? expected : 0));
			if (expected > 0)
				assert(!memcmp(out, ram + start, expected));
			for (size_t i = expected > 0 ? expected : 0; i < sizeof(out); i++)
				assert(out[i] == 0xcc);
			cases++;
		}
	}
	loff_t pos = 1;
	fail_allocation = true;
	assert(dcc_sram_read(&file, (void *)out, 3, &pos) == -ENOMEM && pos == 1);
	fail_allocation = false;
	fail_copy = true;
	assert(dcc_sram_read(&file, (void *)out, 3, &pos) == -EFAULT && pos == 1);
	fail_copy = false;
	struct async_read request = { .file = &file };
	pthread_t thread;
	atomic_store(&hold_copy, true);
	assert(!pthread_create(&thread, NULL, read_thread, &request));
	while (!atomic_load(&copying))
		sched_yield();
	assert(pthread_mutex_trylock(&sram.lock.native) == EBUSY);
	atomic_store(&release_copy, true);
	assert(!pthread_join(thread, NULL));
	assert(request.result == 8);
	mutex_lock(&sram.lock);
	sram.drvdata = NULL;
	mutex_unlock(&sram.lock);
	assert(dcc_sram_read(&file, (void *)out, 3, &pos) == -ENODEV);
	printf("DCC SRAM: %u byte-range cases, errors and removal serialization passed\n", cases);
}

int main(void)
{
	for (int i = 0; i < 20; i++)
		test_tables();
	test_cpu_registers();
	test_sram();
	puts("Memory dump: concurrent full-capacity tables, invalid offsets and CPU count overflow passed");
	return 0;
}
