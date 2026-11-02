#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_DEBUG_FS 1
#define GFP_KERNEL 0
#define __user
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define U8_MAX UINT8_MAX
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define ERR_PTR(error) ((void *)(intptr_t)(error))
#define READ_ONCE(value) (value)
#define WRITE_ONCE(value, replacement) ((value) = (replacement))
#define DEFINE_MUTEX(name) struct mutex name = { PTHREAD_MUTEX_INITIALIZER }
typedef uint32_t u32;
typedef uint8_t u8;
struct mutex { pthread_mutex_t native; };
void mutex_lock(struct mutex *lock) { assert(!pthread_mutex_lock(&lock->native)); }
void mutex_unlock(struct mutex *lock) { assert(!pthread_mutex_unlock(&lock->native)); }
struct device { int unused; };
struct dentry { int unused; };
struct swr_device { struct device dev; void *data; int dev_num; };
struct file { void *private_data; };
struct seq_file { void *private; };
static struct dentry directory;
static int codec_slave_comp_ops, codec_debug_read_ops, codec_debug_write_ops, codec_debug_dump_ops;
static void *allocation;
static bool fail_alloc, fail_component, fail_copy, component_alive, debug_alive;
static int debug_mode, debug_files, io_error, reads, writes, lines, fail_read_at;
static unsigned int last_register;
static unsigned char last_value;
static bool seen[0x200a];
static struct swr_device *current;
#if CODEC_KIND == 9390
static atomic_bool callback_entered, removal_entered, removal_done;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool release_callback;
#endif

void *devm_kzalloc(struct device *dev, size_t size, int flags)
{
	if (fail_alloc)
		return NULL;
	assert(!allocation);
	allocation = calloc(1, size);
	assert(allocation);
	return allocation;
}
void swr_set_dev_data(struct swr_device *pdev, void *data) { pdev->data = data; }
void *swr_get_dev_data(struct swr_device *pdev) { assert(pdev); return pdev->data; }
const char *dev_name(struct device *dev) { return "test-slave"; }
int component_add(struct device *dev, void *ops)
{
	assert(current->data && !component_alive && !debug_alive);
	if (fail_component)
		return -EIO;
	component_alive = true;
	return 0;
}
#if CODEC_KIND == 9390
int codec_slave_register_notify(struct swr_device *, int (*)(void *), void *);
#endif
void component_del(struct device *dev, void *ops)
{
	assert(component_alive && !debug_alive && current->data);
#if CODEC_KIND == 9390
	assert(codec_slave_register_notify(current, NULL, NULL) == 1);
#endif
	component_alive = false;
}
struct dentry *debugfs_create_dir(const char *name, void *parent)
{
	assert(component_alive && !debug_alive);
	if (debug_mode == 1)
		return NULL;
	if (debug_mode == 2)
		return ERR_PTR(-ENODEV);
	debug_alive = true;
	return &directory;
}
struct dentry *debugfs_create_file(const char *name, int mode, struct dentry *parent, void *data, void *ops)
{
	assert(component_alive && debug_alive && parent == &directory && data == current);
	debug_files++;
	return &directory;
}
void debugfs_remove_recursive(struct dentry *entry)
{
	if (IS_ERR_OR_NULL(entry))
		return;
	assert(entry == &directory && debug_alive && component_alive);
	debug_alive = false;
}
int kstrtou32(const char *text, unsigned int base, u32 *value)
{
	char *end;
	if (!*text || *text == '-')
		return -EINVAL;
	errno = 0;
	unsigned long long result = strtoull(text, &end, base);
	if (*end || errno || result > UINT32_MAX)
		return -EINVAL;
	*value = result;
	return 0;
}
unsigned long copy_from_user(void *dest, const void *source, size_t count)
{
	if (fail_copy)
		return count;
	memcpy(dest, source, count);
	return 0;
}
int swr_read(struct swr_device *pdev, int number, unsigned int reg, void *value, int count)
{
	assert(pdev == current && component_alive && reg < ARRAY_SIZE(seen) && count == 1);
	reads++;
	if (io_error || reads == fail_read_at)
		return -EREMOTEIO;
	seen[reg] = true;
	*(u8 *)value = 0x5a;
	return 0;
}
int swr_write(struct swr_device *pdev, int number, unsigned int reg, void *value)
{
	assert(pdev == current && component_alive && reg < ARRAY_SIZE(seen));
	writes++;
	last_register = reg;
	last_value = *(u8 *)value;
	return io_error ? -EREMOTEIO : 0;
}
void seq_printf(struct seq_file *seq, const char *format, int reg, int value)
{
	assert(seen[reg] && value == 0x5a);
	lines++;
}

#include "slave_production.inc"

static ssize_t poke(struct file *file, const char *input)
{
	loff_t position = 0;
	return codec_debug_write(file, input, strlen(input), &position);
}
static ssize_t peek(struct file *file, const char *input)
{
	loff_t position = 0;
	return codec_debug_peek_write(file, input, strlen(input), &position);
}
#if CODEC_KIND == 9390
static int reset_callback(void *handle)
{
	assert(handle == allocation && component_alive);
	assert(!pthread_mutex_lock(&gate));
	atomic_store(&callback_entered, true);
	assert(!pthread_cond_broadcast(&condition));
	while (!release_callback)
		assert(!pthread_cond_wait(&condition, &gate));
	assert(!pthread_mutex_unlock(&gate));
	assert(component_alive && current->data == allocation);
	return 17;
}
static void *reset_thread(void *arg) { assert(codec_swr_reset(arg) == 17); return NULL; }
static void *remove_thread(void *arg)
{
	atomic_store(&removal_entered, true);
	assert(codec_swr_remove(arg) == 0);
	atomic_store(&removal_done, true);
	return NULL;
}
#endif
static void reset_state(struct swr_device *pdev)
{
	assert(!component_alive && !debug_alive && !pdev->data);
	free(allocation);
	allocation = NULL;
	debug_files = 0;
	fail_alloc = fail_component = false;
}
int main(void)
{
	struct swr_device pdev = {};
	struct file file = { .private_data = &pdev };
	struct seq_file seq = { .private = &pdev };
	loff_t position = 0;
	current = &pdev;
	fail_alloc = true;
	assert(codec_swr_probe(&pdev) == -ENOMEM);
	reset_state(&pdev);
	fail_component = true;
	assert(codec_swr_probe(&pdev) == -EIO && debug_files == 0);
	reset_state(&pdev);
	for (debug_mode = 0; debug_mode <= 2; debug_mode++) {
		assert(codec_swr_probe(&pdev) == 0);
		assert(debug_files == (debug_mode ? 0 : 3));
		assert(codec_swr_remove(&pdev) == 0);
		reset_state(&pdev);
	}
	debug_mode = 0;
	assert(codec_swr_probe(&pdev) == 0);
	assert(poke(&file, "0x40 0x7f\n") == 10 && last_register == 0x40 && last_value == 0x7f);
	assert(poke(&file, "64\t255") == 6 && last_value == 255);
	int before = writes;
	const char *invalid_pokes[] = { " ", "1", "1 2 3", "1 256", "1 -1", "0x200a 1", "4294967296 1", "1x 0", "1 0x" };
	for (size_t i = 0; i < ARRAY_SIZE(invalid_pokes); i++)
		assert(poke(&file, invalid_pokes[i]) == -EINVAL);
	assert(writes == before);
	assert(codec_debug_write(&file, "", 0, &position) == 0);
	assert(codec_debug_write(&file, "", 32, &position) == -EINVAL);
	fail_copy = true;
	assert(poke(&file, "1 2") == -EFAULT && peek(&file, "1") == -EFAULT);
	fail_copy = false;
	io_error = 1;
	assert(poke(&file, "1 2") == -EREMOTEIO && peek(&file, "1") == -EREMOTEIO);
	assert(((struct codec_slave_priv *)allocation)->read_data == 0);
	io_error = 0;
	assert(peek(&file, "0x40\n") == 5);
	assert(((struct codec_slave_priv *)allocation)->read_data == 0x5a);
	const char *invalid_peeks[] = { " ", "1 2", "0x200a", "-1", "4294967296", "0x" };
	before = reads;
	for (size_t i = 0; i < ARRAY_SIZE(invalid_peeks); i++)
		assert(peek(&file, invalid_peeks[i]) == -EINVAL);
	assert(reads == before);
	memset(seen, 0, sizeof(seen));
	reads = 0;
	assert(codec_swrslave_reg_show(&seq, NULL) == 0 && lines == reads);
	assert(seen[0x40] && seen[0x46] && !seen[0x47] && seen[0x2009]);
	fail_read_at = reads + 3;
	before = lines;
	assert(codec_swrslave_reg_show(&seq, NULL) == -EREMOTEIO && lines == before + 2);
#if CODEC_KIND == 9390
	assert(codec_slave_register_notify(NULL, reset_callback, allocation) == -EINVAL);
	assert(codec_slave_register_notify(&pdev, reset_callback, allocation) == 1);
	pthread_t resetter, remover;
	assert(!pthread_create(&resetter, NULL, reset_thread, &pdev));
	assert(!pthread_mutex_lock(&gate));
	while (!atomic_load(&callback_entered))
		assert(!pthread_cond_wait(&condition, &gate));
	assert(!pthread_mutex_unlock(&gate));
	assert(!pthread_create(&remover, NULL, remove_thread, &pdev));
	while (!atomic_load(&removal_entered))
		usleep(100);
	usleep(10000);
	assert(!atomic_load(&removal_done));
	assert(!pthread_mutex_lock(&gate));
	release_callback = true;
	assert(!pthread_cond_broadcast(&condition));
	assert(!pthread_mutex_unlock(&gate));
	assert(!pthread_join(resetter, NULL) && !pthread_join(remover, NULL));
	assert(codec_swr_reset(&pdev) == 0);
	assert(codec_slave_register_notify(&pdev, reset_callback, allocation) == 0);
#else
	assert(codec_swr_remove(&pdev) == 0);
#endif
	reset_state(&pdev);
	printf("PASS: codec %d slave input bounds, I/O failures, dump holes, probe rollback and removal\n", CODEC_KIND);
	return 0;
}
