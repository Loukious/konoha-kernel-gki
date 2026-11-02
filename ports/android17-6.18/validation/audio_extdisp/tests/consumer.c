#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define DP_DAI_MAX 5
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
struct device_node { int refs; };
struct device { struct device_node *of_node; void *data; int refs; };
struct platform_device { struct device dev; };
struct snd_soc_component { struct device *dev; const char *name; };
struct mutex { bool initialized, held; };
struct msm_ext_disp_audio_codec_ops { int slot; };
static int live_allocations, failure, withdraw_error;
static struct device_node parent_node;
static struct platform_device parent_device;
static struct msm_ext_disp_audio_codec_ops *published;
static void *kzalloc(size_t n, int flags)
{
    if (failure == 1)
        return NULL;
    live_allocations++;
    return calloc(1, n);
}
static void kfree(void *p)
{
    assert(!published);
    if (p) {
        live_allocations--;
        free(p);
    }
}
static struct device_node *of_get_parent(struct device_node *n)
{
    if (failure == 2)
        return NULL;
    parent_node.refs++;
    return &parent_node;
}
static void of_node_put(struct device_node *n) { assert(n->refs > 0); n->refs--; }
static struct platform_device *of_find_device_by_node(struct device_node *n)
{
    if (failure == 3)
        return NULL;
    parent_device.dev.refs++;
    return &parent_device;
}
static void put_device(struct device *dev) { assert(dev->refs > 0); dev->refs--; }
static void dev_set_drvdata(struct device *d, void *data) { d->data = data; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static void mutex_init(struct mutex *m) { m->initialized = true; }
static void mutex_lock(struct mutex *m) { assert(m->initialized && !m->held); m->held = true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; }
static void mutex_destroy(struct mutex *m) { assert(m->initialized && !m->held); m->initialized = false; }
/* TYPES */
static int msm_ext_disp_register_audio_codec(struct platform_device *p,
        struct msm_ext_disp_audio_codec_ops *ops)
{
    assert(p == &parent_device && p->dev.refs > 0);
    if (failure == 4)
        return -EBUSY;
    published = ops;
    return 0;
}
static int msm_ext_disp_unregister_audio_codec(struct platform_device *p,
        struct msm_ext_disp_audio_codec_ops *ops)
{
    struct msm_ext_disp_audio_codec_rx_data *data =
        (void *)((char *)ops - __builtin_offsetof(struct msm_ext_disp_audio_codec_rx_data, ext_disp_ops));
    assert(data->dp_ops_lock.held && p->dev.refs > 0 && published == ops);
    if (withdraw_error)
        return withdraw_error;
    published = NULL;
    return 0;
}
/* SOURCE */

int main(void)
{
    struct device dev = { .of_node = &parent_node };
    struct snd_soc_component component = { .dev = &dev, .name = "hdmi" };
    const int expected[] = { 0, -ENOMEM, -ENODEV, -EPROBE_DEFER, -EBUSY };
    for (failure = 1; failure <= 4; failure++) {
        assert(msm_ext_disp_audio_codec_rx_probe(&component) == expected[failure]);
        assert(!live_allocations && !parent_node.refs && !parent_device.dev.refs && !dev.data);
    }
    failure = 0;
    assert(msm_ext_disp_audio_codec_rx_probe(&component) == 0);
    assert(live_allocations == 1 && parent_device.dev.refs == 1 && !parent_node.refs);
    assert(published && dev.data);
    withdraw_error = -ENODEV;
    msm_ext_disp_audio_codec_rx_remove(&component);
    assert(live_allocations == 1 && parent_device.dev.refs == 1 && published && dev.data);
    withdraw_error = 0;
    msm_ext_disp_audio_codec_rx_remove(&component);
    assert(!live_allocations && !parent_device.dev.refs && !published && !dev.data);
    msm_ext_disp_audio_codec_rx_remove(&component);
    puts("{\"probe_failure_paths\":4,\"node_and_device_references\":true,"
         "\"withdraw_before_free\":true,\"failed_withdraw_retains_storage\":true,\"retry_cleanup\":true}");
    return 0;
}
