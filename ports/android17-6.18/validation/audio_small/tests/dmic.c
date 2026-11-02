#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define CONFIG_SND_SOC_WCD939X 1
#define SWR_UC_MAX 8
#define SWR_DMIC_MAX_PORTS 2
#define DL_FLAG_AUTOREMOVE_CONSUMER 1
#define SND_SOC_DAPM_PRE_PMU 1
#define SND_SOC_DAPM_POST_PMD 2
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
struct device_node { int references; };
struct device { struct device_node *of_node; void *data; int references; };
struct platform_device { struct device dev; };
struct swr_device { struct device dev; uint64_t addr; u8 dev_num; };
struct snd_soc_component { struct device *dev; const char *name_prefix; };
struct snd_soc_component_driver { const char *name; };
struct snd_soc_pcm_stream { const char *stream_name; };
struct snd_soc_dai_driver { const char *name; int id; struct snd_soc_pcm_stream capture; };
struct snd_soc_dapm_context { int unused; };
struct swr_port_params { u32 offset1, lane_ctrl; };
struct swr_dev_frame_config { struct swr_port_params *pp; };
struct notifier_block { int (*notifier_call)(struct notifier_block *, unsigned long, void *); };
struct mutex { bool ready, held; };
static int failure, allocations_count, enumeration_calls, enable_error, disable_error, notifier_error;
static int power_count;
static bool component_live, component_bound, notifier_live, managed_link, no_supplier;
static struct device_node supplier_node;
static struct platform_device supplier;
static struct snd_soc_component supplier_component = { .dev = &supplier.dev };
static struct snd_soc_component own_component;
static struct snd_soc_dapm_context dapm;
static void *allocations[8];
static char cleanup_order[32];
static unsigned cleanup_length;
static void step(char c) { cleanup_order[cleanup_length++] = c; cleanup_order[cleanup_length] = 0; }
static void *swr_get_dev_data(struct swr_device *s) { return s->dev.data; }
static void swr_set_dev_data(struct swr_device *s, void *data) { s->dev.data = data; }
static void *snd_soc_component_get_drvdata(struct snd_soc_component *c) { return c->dev->data; }
static void mutex_init(struct mutex *m) { m->ready = true; }
static void mutex_lock(struct mutex *m) { assert(m->ready && !m->held); m->held = true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; }
static void mutex_destroy(struct mutex *m) { assert(m->ready && !m->held); m->ready = false; }
static size_t strscpy(char *to, const char *from, size_t size)
{
    size_t len = strlen(from);
    assert(len < size);
    memcpy(to, from, len + 1);
    return len;
}
static size_t strlcat(char *to, const char *from, size_t size)
{
    assert(strlen(to) + strlen(from) < size);
    strcat(to, from);
    return strlen(to);
}
static struct snd_soc_dapm_context *snd_soc_component_get_dapm(struct snd_soc_component *c) { return &dapm; }
static void snd_soc_dapm_ignore_suspend(struct snd_soc_dapm_context *d, const char *n) { assert(strlen(n) < 40); }
static void snd_soc_dapm_sync(struct snd_soc_dapm_context *d) { }
static int swr_dmic_event_notify(struct notifier_block *nb, unsigned long e, void *data) { return 0; }
/* TYPES */
static struct snd_soc_component_driver soc_codec_dev_swr_dmic;
static struct snd_soc_dai_driver swr_dmic_dai[1];
static void *devm_kzalloc(struct device *d, size_t n, int flags)
{
    int call = allocations_count + 1;
    if ((failure == 1 && call == 1) || (failure == 11 && call == 2) || (failure == 13 && call == 3))
        return NULL;
    void *p = calloc(1, n);
    assert(p && allocations_count < 8);
    allocations[allocations_count++] = p;
    return p;
}
static int of_property_read_u32(struct device_node *n, const char *name, u32 *v)
{
    if (failure == 2)
        return -EINVAL;
    *v = 1;
    return 0;
}
static struct device_node *of_parse_phandle(struct device_node *n, const char *key, int index)
{
    if (no_supplier)
        return NULL;
    supplier_node.references++;
    return &supplier_node;
}
static void of_node_put(struct device_node *n)
{
    if (n) { assert(n->references > 0); n->references--; step('O'); }
}
static struct platform_device *of_find_device_by_node(struct device_node *n)
{
    if (failure == 4)
        return NULL;
    supplier.dev.references++;
    return &supplier;
}
static void put_device(struct device *d)
{
    if (d) { assert(d->references > 0); d->references--; step('D'); }
}
static void *device_link_add(struct device *consumer, struct device *provider, int flags)
{
    assert(flags == DL_FLAG_AUTOREMOVE_CONSUMER && provider == &supplier.dev);
    if (failure == 5)
        return NULL;
    managed_link = true;
    return (void *)1;
}
static struct snd_soc_component *snd_soc_lookup_component(struct device *d, const char *name)
{
    if (d == &supplier.dev) {
        assert(managed_link);
        return failure == 6 ? NULL : &supplier_component;
    }
    assert(component_live);
    own_component.dev = d;
    own_component.name_prefix = "DMIC";
    return failure == 15 ? NULL : &own_component;
}
static int of_property_read_string(struct device_node *n, const char *name, const char **v)
{
    if (failure == 8)
        return -EINVAL;
    *v = failure == 12 ? "bad-codec" : "swr-dmic.01";
    return 0;
}
static int swr_dmic_parse_port_params(struct device *d, const char *name) { return failure == 9 ? -EINVAL : 0; }
static void usleep_range(unsigned low, unsigned high) { }
static int swr_get_logical_dev_num(struct swr_device *s, uint64_t address, u8 *number)
{
    enumeration_calls++;
    *number = 4;
    return failure == 10 ? -ETIMEDOUT : 0;
}
static void swr_init_port_params(struct swr_device *s, unsigned n, void *p) { assert(n == SWR_DMIC_MAX_PORTS); }
static int snd_soc_register_component(struct device *d, const struct snd_soc_component_driver *c,
                                      const struct snd_soc_dai_driver *dai, int n)
{
    assert(!strcmp(c->name, "swr-dmic.01") && !strcmp(dai->name, "swr_dmic_tx0") && n == 1);
    if (failure == 14)
        return -EIO;
    component_live = true;
    return 0;
}
static void swr_dmic_codec_remove(struct snd_soc_component *component);
static void snd_soc_unregister_component(struct device *d)
{
    assert(component_live);
    if (component_bound)
        swr_dmic_codec_remove(&own_component);
    component_live = component_bound = false;
    step('C');
}
static int wcd939x_codec_force_enable_micbias_v2(struct snd_soc_component *c, int event, int number)
{
    assert(c == &supplier_component && supplier.dev.references > 0 && managed_link && number == 1);
    if (event == SND_SOC_DAPM_PRE_PMU) {
        if (failure == 7 || enable_error)
            return -EIO;
        power_count++;
    } else {
        if (disable_error)
            return -EIO;
        assert(power_count > 0);
        power_count--;
        step('P');
    }
    return 0;
}
static int wcd939x_swr_dmic_register_notifier(struct snd_soc_component *c,
                                            struct notifier_block *nb, bool enable)
{
    assert(c == &supplier_component && supplier.dev.references > 0);
    if (enable) {
        if (notifier_error)
            return notifier_error;
        assert(!notifier_live);
        notifier_live = true;
    } else {
        assert(notifier_live);
        notifier_live = false;
        step('N');
    }
    return 0;
}
/* SOURCE */

static void release_devres(struct swr_device *sdev)
{
    assert(!sdev->dev.data && !supplier_node.references && !supplier.dev.references);
    assert(!component_live && !notifier_live && !power_count);
    if (allocations_count)
        assert(!((struct swr_dmic_priv *)allocations[0])->supply_lock.ready);
    while (allocations_count)
        free(allocations[--allocations_count]);
    managed_link = false;
    enumeration_calls = cleanup_length = 0;
}
int main(void)
{
    struct device_node node;
    struct swr_device sdev = { .dev = { .of_node = &node } };
    const int expected[] = {0, -ENOMEM, -EINVAL, 0, -EPROBE_DEFER, -ENOMEM,
        -EPROBE_DEFER, -EPROBE_DEFER, -EINVAL, -EINVAL, -EPROBE_DEFER, -ENOMEM,
        -EINVAL, -ENOMEM, -EIO, -EPROBE_DEFER};
    for (failure = 1; failure <= 15; failure++) {
        if (failure == 3)
            continue;
        assert(swr_dmic_probe(&sdev) == expected[failure]);
        if (failure == 10)
            assert(enumeration_calls == NUM_ATTEMPTS);
        release_devres(&sdev);
    }
    failure = 0;
    assert(!swr_dmic_probe(&sdev));
    assert(supplier.dev.references == 1 && supplier_node.references == 1 && power_count == 1);
    struct swr_dmic_priv *priv = sdev.dev.data;
    notifier_error = -EAGAIN;
    assert(swr_dmic_codec_probe(&own_component) == -EAGAIN && !priv->notifier_registered);
    notifier_error = 0;
    assert(!swr_dmic_codec_probe(&own_component));
    component_bound = true;
    assert(priv->notifier_registered && notifier_live);
    assert(!swr_dmic_up(&sdev) && priv->is_en_supply == 2 && power_count == 1);
    assert(!swr_dmic_down(&sdev) && priv->is_en_supply == 1 && power_count == 1);
    disable_error = -EIO;
    assert(swr_dmic_down(&sdev) == -EIO && priv->is_en_supply == 1 && power_count == 1);
    disable_error = 0;
    assert(!swr_dmic_down(&sdev) && !priv->is_en_supply && !power_count);
    assert(!swr_dmic_down(&sdev) && !priv->is_en_supply);
    enable_error = -EIO;
    assert(swr_dmic_up(&sdev) == -EIO && !priv->is_en_supply && !power_count);
    enable_error = 0;
    assert(!swr_dmic_up(&sdev));
    cleanup_length = 0;
    assert(!swr_dmic_remove(&sdev));
    assert(!strcmp(cleanup_order, "NCPDO"));
    release_devres(&sdev);
    no_supplier = true;
    assert(!swr_dmic_probe(&sdev));
    assert(!swr_dmic_codec_probe(&own_component));
    component_bound = true;
    assert(!notifier_live && !swr_dmic_up(&sdev) && !swr_dmic_down(&sdev));
    assert(!swr_dmic_remove(&sdev));
    release_devres(&sdev);
    puts("{\"probe_failure_paths\":14,\"supplier_ref_and_link\":true,\"native_enumeration_retries\":5,"
         "\"notifier_registration_error_and_drain\":true,\"micbias_vote_failures\":true,\"optional_supplier\":true}");
    return 0;
}
