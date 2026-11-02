#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x) || IS_ERR(x))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
typedef uint32_t u32;
typedef uint8_t u8;
struct device_node { int unused; };
struct device { struct device_node *of_node; void *data; };
struct swr_device { struct device dev; uint64_t addr; u8 dev_num; };
struct snd_soc_component { int unused; };
struct regulator { int count; };
struct regmap { int unused; };
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };
struct mutex { bool ready; };
static int failure, disable_calls, enumeration_calls, pmic_mode = 3;
static bool component_live, hboost_live, ssr_live;
static bool voltage_reset_fails;
static struct regulator slave, hpwr;
static struct regmap regmap;
static void *allocation;
static char teardown[32];
static unsigned teardown_length;
static void step(char c) { teardown[teardown_length++] = c; teardown[teardown_length] = 0; }
static void *swr_get_dev_data(struct swr_device *s) { return s->dev.data; }
static void swr_set_dev_data(struct swr_device *s, void *data) { s->dev.data = data; }
static void mutex_init(struct mutex *m) { m->ready = true; }
static void mutex_destroy(struct mutex *m) { assert(m->ready); m->ready = false; }
/* TYPES */
static int swr_hap_regmap_config, swr_haptics_component;
static int haptics_dai[1];
static void *devm_kzalloc(struct device *d, size_t n, int flags)
{
    if (failure == 1)
        return NULL;
    assert(!allocation);
    allocation = calloc(1, n);
    return allocation;
}
static const void *of_device_get_match_data(struct device *d) { return (void *)(intptr_t)pmic_mode; }
static int swr_haptics_parse_port_mapping(struct swr_device *s) { return failure == 2 ? -EINVAL : 0; }
static struct regulator *devm_regulator_get(struct device *d, const char *name)
{
    bool is_slave = !strcmp(name, "swr-slave");
    if (failure == (is_slave ? 3 : 4))
        return ERR_PTR(-EPROBE_DEFER);
    return is_slave ? &slave : &hpwr;
}
static void *of_find_property(struct device_node *n, const char *name, int *length) { return (void *)1; }
static int of_property_read_u32(struct device_node *n, const char *name, u32 *value)
{
    if (failure == 5)
        return -EINVAL;
    *value = 5000;
    return 0;
}
static int regulator_enable(struct regulator *r)
{
    if (failure == 6)
        return -EPERM;
    r->count++;
    return 0;
}
static int regulator_disable(struct regulator *r)
{
    if (failure == 12 && !disable_calls++)
        return -EIO;
    assert(r->count > 0);
    r->count--;
    step(r == &slave ? 'S' : 'V');
    return 0;
}
static int regulator_set_voltage(struct regulator *r, int minimum, int maximum)
{
    return voltage_reset_fails ? -EINVAL : 0;
}
static void usleep_range(unsigned low, unsigned high) { }
static void msleep(unsigned delay) { }
static int swr_get_logical_dev_num(struct swr_device *s, uint64_t address, u8 *number)
{
    enumeration_calls++;
    *number = 7;
    return failure == 7 ? -ENODEV : 0;
}
static struct regmap *devm_regmap_init_swr(struct swr_device *s, void *config)
{
    return failure == 8 ? ERR_PTR(-EIO) : &regmap;
}
static int snd_soc_register_component(struct device *d, void *component, void *dai, size_t n)
{
    assert(slave.count == 1);
    if (failure == 9)
        return -EBUSY;
    component_live = true;
    return 0;
}
static void snd_soc_unregister_component(struct device *d)
{
    assert(component_live && !hboost_live && !ssr_live);
    component_live = false;
    step('C');
}
static int hboost_notifier(struct notifier_block *nb, unsigned long e, void *d) { return 0; }
static int lpass_ssr_notifier(struct notifier_block *nb, unsigned long e, void *d) { return 0; }
static int register_hboost_event_notifier(struct notifier_block *nb)
{
    assert(component_live);
    if (failure == 10)
        return -EAGAIN;
    hboost_live = true;
    return 0;
}
static int unregister_hboost_event_notifier(struct notifier_block *nb)
{
    assert(hboost_live && !ssr_live);
    hboost_live = false;
    step('H');
    return 0;
}
static void *qcom_register_ssr_notifier(const char *name, struct notifier_block *nb)
{
    assert(component_live && hboost_live);
    if (failure == 11)
        return ERR_PTR(-EIO);
    ssr_live = true;
    return (void *)1;
}
static void qcom_unregister_ssr_notifier(void *handle, struct notifier_block *nb)
{
    assert(ssr_live);
    ssr_live = false;
    step('R');
}
/* SOURCE */

static void release_devres(struct swr_device *sdev)
{
    assert(!sdev->dev.data && !component_live && !hboost_live && !ssr_live);
    assert(!slave.count && !hpwr.count);
    if (allocation)
        assert(!((struct swr_haptics_dev *)allocation)->play_lock.ready);
    free(allocation);
    allocation = NULL;
    teardown_length = disable_calls = enumeration_calls = 0;
}
int main(void)
{
    struct device_node node;
    struct swr_device sdev = { .dev = { .of_node = &node } };
    const int expected[] = {0, -ENOMEM, -EINVAL, -EPROBE_DEFER, -EPROBE_DEFER,
        -EINVAL, -EPERM, -EPROBE_DEFER, -EIO, -EBUSY, -EAGAIN, -EIO, -EIO};
    for (failure = 1; failure <= 12; failure++) {
        assert(swr_haptics_probe(&sdev) == expected[failure]);
        if (failure == 7)
            assert(enumeration_calls == 30);
        release_devres(&sdev);
    }
    failure = 0;
    for (pmic_mode = 1; pmic_mode <= 3; pmic_mode++) {
        assert(!swr_haptics_probe(&sdev));
        struct swr_haptics_dev *hap = sdev.dev.data;
        assert(sdev.dev_num == 7);
        assert(hap->vmax == 100 && component_live && hboost_live);
        assert(ssr_live == (pmic_mode == PMIH010X));
        assert(slave.count == (pmic_mode == PMIH010X ? 0 : 1));
        teardown_length = 0;
        assert(!swr_haptics_remove(&sdev));
        assert(!strcmp(teardown, pmic_mode == PMIH010X ? "RHC" : "HCS"));
        release_devres(&sdev);
    }
    struct swr_haptics_dev local = { .hpwr_vreg = &hpwr, .hpwr_vreg_enabled = true };
    hpwr.count = 1;
    voltage_reset_fails = true;
    assert(swr_hap_disable_hpwr_vreg(&local) == -EINVAL);
    assert(!local.hpwr_vreg_enabled && !hpwr.count);
    assert(!swr_hap_disable_hpwr_vreg(&local));
    assert(!swr_haptics_remove(&sdev));
    puts("{\"probe_failure_paths\":12,\"native_pmic_variants\":3,\"enumeration_retries\":30,"
         "\"notifiers_and_component_before_power\":true,\"failed_voltage_reset_not_double_disabled\":true}");
    return 0;
}
