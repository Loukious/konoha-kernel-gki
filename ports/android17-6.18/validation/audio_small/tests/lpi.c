#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __iomem
#define __user
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define HZ 100
#define THIS_MODULE ((void *)1)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define DEFINE_RATELIMIT_STATE(name, interval, burst) int name
#define __ratelimit(r) 1
typedef uint32_t u32;
struct mutex { bool ready, held; };
struct device_node { int unused; };
struct device { struct device_node *of_node; void *data; };
struct platform_device { struct device dev; };
struct clk { int unused; };
struct pinctrl_dev { void *data; };
struct pinctrl_pin_desc { unsigned number; const char *name; void *drv_data; };
struct pinctrl_desc { void *pctlops, *pmxops, *confops, *owner; const char *name;
    struct pinctrl_pin_desc *pins; unsigned npins; };
struct gpio_chip { struct device *parent; int base; unsigned ngpio, of_gpio_n_cells;
    const char *label; bool can_sleep; void *data; };
enum { PIN_CONFIG_BIAS_DISABLE, PIN_CONFIG_BIAS_PULL_DOWN, PIN_CONFIG_BIAS_BUS_HOLD,
       PIN_CONFIG_BIAS_PULL_UP, PIN_CONFIG_INPUT_ENABLE, PIN_CONFIG_LEVEL,
       PIN_CONFIG_DRIVE_STRENGTH, PIN_CONFIG_SLEW_RATE };
#define pinconf_to_config_param(c) ((c) & 255)
#define pinconf_to_config_argument(c) ((c) >> 8)
#define pinconf_to_config_packed(p, a) ((unsigned long)(p) | ((unsigned long)(a) << 8))
static void mutex_init(struct mutex *m) { m->ready = true; }
static void mutex_lock(struct mutex *m) { assert(m->ready && !m->held); m->held = true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; }
static void mutex_destroy(struct mutex *m) { assert(m->ready && !m->held); m->ready = false; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static void platform_set_drvdata(struct platform_device *p, void *data) { p->dev.data = data; }
static void *platform_get_drvdata(struct platform_device *p) { return p->dev.data; }
static void *pinctrl_dev_get_drvdata(struct pinctrl_dev *p) { return p->data; }
static void *gpiochip_get_data(struct gpio_chip *p) { return p->data; }
static const char *dev_name(struct device *d) { return "lpi"; }
static unsigned long set_bit(int b, volatile unsigned long *v) { *v |= 1UL << b; return *v; }
static unsigned long clear_bit(int b, volatile unsigned long *v) { *v &= ~(1UL << b); return *v; }
/* TYPES */
static bool lpi_dev_up, initial_boot;
static struct device *lpi_dev;
static u32 lpi_offset[LPI_TLMM_MAX_PINS], lpi_slew_offset[LPI_TLMM_MAX_PINS];
static u32 lpi_slew_base[LPI_TLMM_MAX_PINS];
static int lpi_gpio_pinctrl_ops, lpi_gpio_pinmux_ops, lpi_gpio_pinconf_ops;
static int lpi_pinctrl_ssr_ops, service_nb;
static const struct gpio_chip lpi_gpio_template = {0};
#define SND_EVENT_UP 1
#define AUDIO_NOTIFIER_ADSP_DOMAIN 1
static int failure, vote_error, pm_users, gpio_live, pinctrl_live, event_live, notifier_live;
static bool pm_enabled, invalid_offset, invalid_slew;
static u32 dt_pins = 2;
static struct clk vote_clk;
static struct pinctrl_dev pctldev;
static void *allocations[1024];
static unsigned allocated;
static char teardown_order[32];
static unsigned teardown_len;
static void step(char c) { teardown_order[teardown_len++] = c; teardown_order[teardown_len] = 0; }
static void *devm_kcalloc(struct device *d, size_t n, size_t size, int flags)
{
    assert(allocated < ARRAY_SIZE(allocations));
    void *p = calloc(n, size);
    assert(p);
    allocations[allocated++] = p;
    return p;
}
static void *devm_kzalloc(struct device *d, size_t n, int flags) { return devm_kcalloc(d, 1, n, flags); }
static void *devm_ioremap(struct device *d, u32 address, size_t size) { return devm_kcalloc(d, 1, size, 0); }
static bool audio_notifier_probe_status(void) { return failure != 1; }
static int of_property_read_u32(struct device_node *n, const char *key, u32 *value)
{
    if (!strcmp(key, "qcom,gpios-count"))
        *value = dt_pins;
    else if (!strcmp(key, "qcom,slew-reg"))
        *value = 0x2000;
    else
        *value = 0x1000;
    return 0;
}
static int of_property_read_u32_array(struct device_node *n, const char *key, u32 *value, size_t count)
{
    for (size_t i = 0; i < count; i++)
        value[i] = !strcmp(key, "qcom,lpi-offset-tbl") ? i * 8 : 0;
    if (invalid_offset && !strcmp(key, "qcom,lpi-offset-tbl"))
        value[0] = LPI_ADDRESS_SIZE;
    if (invalid_slew && !strcmp(key, "qcom,lpi-slew-offset-tbl"))
        value[0] = 31;
    return 0;
}
static struct clk *devm_clk_get(struct device *dev, const char *name)
{
    if (failure == 2 && !strcmp(name, "lpass_core_hw_vote"))
        return ERR_PTR(-EPROBE_DEFER);
    if (failure == 3 && !strcmp(name, "lpass_audio_hw_vote"))
        return ERR_PTR(-EPROBE_DEFER);
    return &vote_clk;
}
static void pm_runtime_set_autosuspend_delay(struct device *d, int delay) { }
static void pm_runtime_use_autosuspend(struct device *d) { }
static void pm_runtime_set_suspended(struct device *d) { }
static void pm_runtime_enable(struct device *d) { assert(!pm_enabled); pm_enabled = true; }
static void pm_runtime_disable(struct device *d) { assert(pm_enabled); pm_enabled = false; step('P'); }
static struct pinctrl_dev *pinctrl_register(struct pinctrl_desc *desc, struct device *d, void *data)
{
    assert(desc->npins == dt_pins && pm_enabled && lpi_dev == d);
    if (failure == 4)
        return ERR_PTR(-EINVAL);
    pctldev.data = data;
    pinctrl_live++;
    return &pctldev;
}
static void pinctrl_unregister(struct pinctrl_dev *p) { assert(pinctrl_live == 1); pinctrl_live--; step('C'); }
static int gpiochip_add_data(struct gpio_chip *c, void *data)
{
    if (failure == 5)
        return -ENOMEM;
    assert(c->can_sleep && pinctrl_live);
    c->data = data;
    gpio_live++;
    return 0;
}
static int gpiochip_add_pin_range(struct gpio_chip *c, const char *n, unsigned a, unsigned b, unsigned count)
{
    return failure == 6 ? -ERANGE : 0;
}
static void gpiochip_remove(struct gpio_chip *c) { assert(gpio_live == 1); gpio_live--; step('G'); }
static int snd_event_client_register(struct device *d, void *ops, void *data)
{
    assert(pm_enabled && pinctrl_live && gpio_live);
    if (failure == 7)
        return -EIO;
    event_live++;
    return 0;
}
static void snd_event_client_deregister(struct device *d) { assert(event_live == 1); event_live--; step('E'); }
static void snd_event_notify(struct device *d, int value) { assert(event_live); }
static int audio_notifier_register(const char *n, int domain, void *nb)
{
    if (failure == 8)
        return -EAGAIN;
    notifier_live++;
    return 0;
}
static void audio_notifier_deregister(const char *n) { assert(notifier_live == 1); notifier_live--; step('N'); }
static int digital_cdc_rsc_mgr_hw_vote_enable(struct clk *c, struct device *d) { return vote_error; }
static void digital_cdc_rsc_mgr_hw_vote_disable(struct clk *c, struct device *d) { }
static int lpi_pinctrl_runtime_resume(struct device *dev);
static int pm_runtime_resume_and_get(struct device *dev)
{
    int ret = lpi_pinctrl_runtime_resume(dev);
    if (!ret)
        pm_users++;
    return ret;
}
static void pm_runtime_mark_last_busy(struct device *d) { }
static void pm_runtime_put_autosuspend(struct device *d) { assert(pm_users > 0); pm_users--; }
static u32 ioread32(void *p) { return *(u32 *)p; }
static void iowrite32(u32 v, void *p) { *(u32 *)p = v; }
/* SOURCE */

static void release_devres(struct platform_device *p)
{
    assert(!gpio_live && !pinctrl_live && !event_live && !notifier_live && !pm_enabled && !pm_users);
    assert(!lpi_dev);
    while (allocated)
        free(allocations[--allocated]);
    p->dev.data = NULL;
    teardown_len = 0;
}
int main(void)
{
    struct device_node node;
    struct platform_device pdev = { .dev = { .of_node = &node } };
    int errors[] = {0, -EPROBE_DEFER, -EPROBE_DEFER, -EPROBE_DEFER,
                    -EINVAL, -ENOMEM, -ERANGE, -EIO, -EAGAIN};
    for (failure = 1; failure <= 8; failure++) {
        assert(lpi_pinctrl_probe(&pdev) == errors[failure]);
        release_devres(&pdev);
    }
    failure = 0;
    dt_pins = 0;
    assert(lpi_pinctrl_probe(&pdev) == -EINVAL);
    dt_pins = 33;
    assert(lpi_pinctrl_probe(&pdev) == -EINVAL);
    dt_pins = 2;
    invalid_offset = true;
    assert(lpi_pinctrl_probe(&pdev) == -EINVAL);
    release_devres(&pdev);
    invalid_offset = false;
    invalid_slew = true;
    assert(lpi_pinctrl_probe(&pdev) == -EINVAL);
    release_devres(&pdev);
    invalid_slew = false;
    assert(lpi_pinctrl_probe(&pdev) == 0);
    struct lpi_gpio_state *state = pdev.dev.data;
    assert(lpi_gpio_get_groups_count(state->ctrl) == 2);
    assert(!strcmp(lpi_gpio_get_group_name(state->ctrl, 1), "gpio1"));
    unsigned count;
    const unsigned *pins;
    assert(!lpi_gpio_get_group_pins(state->ctrl, 1, &pins, &count));
    assert(count == 1 && *pins == 1);
    struct lpi_gpio_pad *pad = state->desc->pins[0].drv_data;
    vote_error = -EIO;
    assert(lpi_gpio_read(pad, 0) == -EIO && !pm_users);
    assert(lpi_gpio_write(pad, 0, 42) == -EIO && !pm_users);
    vote_error = 0;
    assert(lpi_gpio_write(pad, 0, 0xabc) == 0);
    assert(lpi_gpio_read(pad, 0) == 0xabc && !pm_users);
    unsigned long configs[] = {
        pinconf_to_config_packed(PIN_CONFIG_SLEW_RATE, 3),
        pinconf_to_config_packed(PIN_CONFIG_DRIVE_STRENGTH, 12),
        pinconf_to_config_packed(PIN_CONFIG_LEVEL, 1),
        pinconf_to_config_packed(PIN_CONFIG_BIAS_PULL_UP, 1),
    };
    assert(!lpi_config_set(state->ctrl, 0, configs, ARRAY_SIZE(configs)));
    assert(pad->strength == 12 && pad->value && pad->output_enabled && pad->pullup == LPI_GPIO_PULL_UP);
    assert((*(u32 *)pad->slew_base & 3) == 3);
    pad->lpi_slew_reg = pad->slew_base;
    pad->strength = 0;
    pad->value = false;
    pad->pullup = LPI_GPIO_BIAS_DISABLE;
    assert(!lpi_config_set(state->ctrl, 0, configs, ARRAY_SIZE(configs)));
    assert(pad->strength == 12 && pad->value && pad->pullup == LPI_GPIO_PULL_UP);
    assert(!lpi_gpio_set(&state->chip, 0, 0) && !pad->value);
    unsigned long config = pinconf_to_config_packed(PIN_CONFIG_BIAS_DISABLE, 0);
    assert(!lpi_config_get(state->ctrl, 0, &config));
    assert(!pinconf_to_config_argument(config) && pad->pullup == LPI_GPIO_PULL_UP);
    teardown_len = 0;
    lpi_pinctrl_remove(&pdev);
    assert(!strcmp(teardown_order, "NEGCP"));
    release_devres(&pdev);
    dt_pins = 32;
    assert(!lpi_pinctrl_probe(&pdev));
    assert(lpi_gpio_get_groups_count(&pctldev) == 32);
    lpi_pinctrl_remove(&pdev);
    release_devres(&pdev);
    puts("{\"probe_failures\":8,\"pin_and_mmio_bounds\":true,\"public_pinctrl_access\":true,"
         "\"runtime_errors_balanced\":true,\"multiple_pin_configurations\":true,\"ordered_teardown\":true}");
    return 0;
}
