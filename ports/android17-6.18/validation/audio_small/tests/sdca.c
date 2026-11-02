/* Kernel services are simulated; the complete SDCA production source follows. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define __user
#define GFP_KERNEL 0
#define THIS_MODULE ((void *)1)
#define MODULE_LICENSE(x)
#define MODULE_DESCRIPTION(x)
#define EXPORT_SYMBOL_GPL(x)
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_RELAXED)
#define WRITE_ONCE(x, value) __atomic_store_n(&(x), (value), __ATOMIC_RELAXED)
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x) || IS_ERR(x))
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define pr_err(...) ((void)0)
typedef uint32_t u32;
struct device { int id; };
struct snd_soc_component { struct device *dev; };
struct file { void *private_data; };
struct dentry { int live; };
struct inode { int unused; };
struct file_operations {
    void *owner;
    int (*open)(struct inode *, struct file *);
    ssize_t (*read)(struct file *, char *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char *, size_t, loff_t *);
};

static int copy_fail_at, copy_calls, read_calls, update_error;
static int create_fail_at, create_calls, remove_calls;
static u32 last_read_address, last_write_address;
static struct dentry dentries[4];

static int scnprintf(char *buf, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buf, n, format, args);
    va_end(args);
    return ret < 0 ? ret : ret < (int)n ? ret : (int)n - 1;
}

static size_t copy_from_user(void *to, const void *from, size_t n)
{
    if (++copy_calls == copy_fail_at)
        return n;
    memcpy(to, from, n);
    return 0;
}

static ssize_t simple_read_from_buffer(void *to, size_t count, loff_t *pos,
                                       const void *from, size_t size)
{
    if (*pos < 0)
        return -EINVAL;
    if (!count || (uint64_t)*pos >= size)
        return 0;
    if (count > size - *pos)
        count = size - *pos;
    if (copy_from_user(to, (const char *)from + *pos, count))
        return -EFAULT;
    *pos += count;
    return count;
}

static int kstrtouint(const char *s, int base, u32 *value)
{
    char *end;
    if (*s == '-' || !*s)
        return -EINVAL;
    errno = 0;
    unsigned long v = strtoul(s, &end, base);
    if (errno || v > UINT32_MAX || end == s)
        return -ERANGE;
    if (*end == '\n')
        end++;
    if (*end)
        return -EINVAL;
    *value = v;
    return 0;
}

static unsigned int snd_soc_component_read(struct snd_soc_component *c, u32 reg)
{
    assert(c);
    read_calls++;
    last_read_address = reg;
    return reg & 0xff;
}

static int snd_soc_component_update_bits(struct snd_soc_component *c, u32 reg,
                                         u32 mask, u32 value)
{
    assert(c && mask == 0xff);
    last_write_address = reg;
    return update_error;
}

static int simple_open(struct inode *inode, struct file *file) { return 0; }
static const char *dev_name(struct device *d) { return "sdca-fixture"; }
static struct dentry *new_dentry(void)
{
    if (++create_calls == create_fail_at)
        return (void *)(intptr_t)-ENOMEM;
    assert(create_calls <= 4);
    dentries[create_calls - 1].live = 1;
    return &dentries[create_calls - 1];
}
static struct dentry *debugfs_create_dir(const char *name, void *parent)
{
    return new_dentry();
}
static struct dentry *debugfs_create_file(const char *name, unsigned mode,
        struct dentry *parent, void *data, const struct file_operations *fops)
{
    assert(parent && !IS_ERR(parent) && data && fops->owner == THIS_MODULE);
    assert(mode == (S_IFREG | 0444));
    return new_dentry();
}
static void debugfs_remove_recursive(struct dentry *dentry)
{
    if (!IS_ERR_OR_NULL(dentry))
        remove_calls++;
    memset(dentries, 0, sizeof(dentries));
}

/* TYPES */
void sdca_devices_debugfs_dentry_remove(struct sdca_debugfs_info *info);
/* SOURCE */

int main(void)
{
    struct device dev = {0};
    struct snd_soc_component component = { .dev = &dev };
    const u32 registers[] = { 0x40000011, 0x40000022 };
    struct sdca_regdump_info info = { .component = &component,
        .reg_array = registers, .reg_num = 2, .reg_address = 0x40000011 };
    struct file file = { .private_data = &info };
    char result[256];
    loff_t pos = 0;
    memset(result, 0xa5, sizeof(result));
    ssize_t ret = codec_debug_address_read(&file, result, sizeof(result), &pos);
    assert(ret == 11 && pos == 11 && !memcmp(result, "0x40000011\n", 11));
    assert((unsigned char)result[11] == 0xa5);
    assert(codec_debug_address_read(&file, result, sizeof(result), &pos) == 0);
    pos = 0;
    assert(codec_debug_address_read(&file, result, 0, &pos) == 0 && !pos);
    pos = -1;
    assert(codec_debug_address_read(&file, result, 10, &pos) == -EINVAL);

    pos = 0;
    assert(codec_debug_address_write(&file, "0x40000022", 10, &pos) == 10);
    assert(info.reg_address == 0x40000022);
    assert(codec_debug_address_write(&file, "-2", 2, &pos) == -EINVAL);
    assert(codec_debug_address_write(&file, result, 20, &pos) == -EINVAL);
    copy_fail_at = copy_calls + 1;
    assert(codec_debug_address_write(&file, "42", 2, &pos) == -EFAULT);
    assert(info.reg_address == 0x40000022);
    copy_fail_at = 0;

    memset(result, 0xa5, sizeof(result));
    ret = codec_debug_data_read(&file, result, sizeof(result), &pos);
    assert(ret == 17 && pos == 17 && last_read_address == 0x40000022);
    assert(!memcmp(result, "0x40000022: 0x22\n", 17));
    assert((unsigned char)result[17] == 0xa5);
    pos = 0;
    update_error = -EIO;
    assert(codec_debug_data_write(&file, "255", 3, &pos) == -EIO);
    assert(last_write_address == info.reg_address);
    update_error = 1;
    assert(codec_debug_data_write(&file, "255", 3, &pos) == 3);
    info.reg_address = 0x40000000;
    assert(codec_debug_data_write(&file, "12", 2, &pos) == -EINVAL);
    assert(codec_debug_data_read(&file, result, 20, &pos) == -EINVAL);

    const char expected[] = "0x40000011: 0x11\n0x40000022: 0x22\n";
    for (size_t chunk = 1; chunk <= sizeof(expected) + 1; chunk++) {
        pos = 0;
        memset(result, 0xa5, sizeof(result));
        size_t total = 0;
        while ((ret = codec_debug_registers_dump(&file, result + total, chunk, &pos)) > 0)
            total += ret;
        assert(!ret && total == 34 && pos == 34);
        assert(!memcmp(result, expected, 34) && (unsigned char)result[34] == 0xa5);
    }
    pos = -34;
    assert(codec_debug_registers_dump(&file, result, 100, &pos) == -EINVAL);
    pos = INT64_MAX;
    int reads_before = read_calls;
    assert(codec_debug_registers_dump(&file, result, 100, &pos) == 0);
    assert(read_calls == reads_before);
    pos = 0;
    copy_fail_at = copy_calls + 2;
    assert(codec_debug_registers_dump(&file, result, 100, &pos) == 17 && pos == 17);
    copy_fail_at = 0;
    assert(codec_debug_registers_dump(&file, result + 17, 100, &pos) == 17);
    assert(!memcmp(result, expected, 34));
    info.reg_array = NULL;
    pos = 0;
    assert(codec_debug_registers_dump(&file, result, 20, &pos) == -EINVAL);
    info.reg_array = registers;

    for (int fail = 0; fail <= 4; fail++) {
        struct sdca_debugfs_info debug = {0};
        create_fail_at = fail;
        create_calls = remove_calls = 0;
        sdca_devices_debugfs_dentry_create(&debug, &info);
        if (fail)
            assert(!debug.sdca_dentry && !debug.address && !debug.data && !debug.registers);
        else
            assert(debug.sdca_dentry && debug.address && debug.data && debug.registers);
        sdca_devices_debugfs_dentry_remove(&debug);
        assert(!debug.sdca_dentry && !debug.address && !debug.data && !debug.registers);
        assert(remove_calls == (fail == 1 ? 0 : 1));
    }
    puts("{\"bounded_reads\":true,\"short_read_chunks\":36,\"positions_and_faults\":true,"
         "\"write_errors\":true,\"creation_failures\":4,\"owner_and_permissions\":true}");
    return 0;
}
