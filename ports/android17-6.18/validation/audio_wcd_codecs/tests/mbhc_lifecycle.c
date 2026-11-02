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
#include <unistd.h>

#define EXPORT_SYMBOL(name)
#define EXPORT_SYMBOL_GPL(name)
#define GFP_KERNEL 0
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define pr_err(...) ((void)0)
#define pr_err_ratelimited(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define BLOCKING_INIT_NOTIFIER_HEAD(head) ((void)(head))
#define WCD_DETECTION_ADC 1
#define MIC_BIAS_2 2
#define MICB_DISABLE 0
typedef uint32_t u32;
struct mutex { pthread_mutex_t native; };
void mutex_init(struct mutex *lock)
{
	pthread_mutexattr_t attr;
	assert(!pthread_mutexattr_init(&attr));
	assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
	assert(!pthread_mutex_init(&lock->native, &attr));
	assert(!pthread_mutexattr_destroy(&attr));
}
void mutex_lock(struct mutex *lock) { assert(!pthread_mutex_lock(&lock->native)); }
void mutex_unlock(struct mutex *lock) { assert(!pthread_mutex_unlock(&lock->native)); }
void mutex_destroy(struct mutex *lock) { assert(!pthread_mutex_destroy(&lock->native)); }
struct device { void *data, *pdata; };
struct snd_soc_component { struct device *dev; };
struct wcd_mbhc_config { bool live; int identity; void *calibration; };
struct wcd_mbhc {
	bool mbhc_initialized, mbhc_started, micbias_enable;
	int mbhc_detection_logic, micb_mv;
	struct wcd_mbhc_config *mbhc_cfg;
	void *default_calibration;
};
struct fw_info { int unused; };
struct timer_list { bool initialized, shutdown; };
struct codec_mbhc {
	struct wcd_mbhc wcd_mbhc;
	struct fw_info *fw_data;
	int notifier;
	struct timer_list rdown_timer;
	int rdown_prev_iter;
	bool rdown_timer_complete;
};
struct codec_pdata { struct { int micb2_mv; } micbias; };
struct codec_priv {
	struct codec_mbhc *mbhc;
	struct mutex mbhc_lock;
	struct wcd_mbhc_config *mbhc_config;
	int irq_info;
	bool sysfs_alive;
};
static int steps, fail_at, allocations, initialized, active, starts, stops;
static int irq_disables, timer_shutdowns;
static bool block_start, start_entered, start_release;
static atomic_bool exit_entered, exit_done;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static struct codec_priv *current_priv;
static struct wcd_mbhc_config *exit_config;

int checkpoint(void) { return ++steps == fail_at ? -EIO : 0; }
void *snd_soc_component_get_drvdata(struct snd_soc_component *component) { return component->dev->data; }
void *dev_get_drvdata(struct device *dev) { return dev->data; }
void *dev_get_platdata(struct device *dev) { return dev->pdata; }
void *devm_kzalloc(struct device *dev, size_t bytes, int flags)
{
	if (checkpoint())
		return NULL;
	void *ptr = calloc(1, bytes);
	assert(ptr);
	allocations++;
	return ptr;
}
void devm_kfree(struct device *dev, void *ptr)
{
	if (!ptr)
		return;
	assert(initialized == 0 && active == 0 && !current_priv->sysfs_alive);
	allocations--;
	assert(allocations >= 0);
	free(ptr);
}
void wcd_mbhc_stop(struct wcd_mbhc *mbhc)
{
	if (!mbhc->mbhc_initialized)
		return;
	if (mbhc->mbhc_cfg) {
		assert(mbhc->mbhc_cfg->live);
		mbhc->mbhc_cfg->calibration = mbhc->default_calibration;
	}
	if (mbhc->mbhc_started) {
		active--;
		mbhc->mbhc_started = false;
	}
	stops++;
}
int wcd_mbhc_init(struct wcd_mbhc *mbhc, struct snd_soc_component *component,
		 const void *callbacks, const void *interrupts, const void *registers, bool zdet)
{
	assert(!mbhc->mbhc_initialized);
	if (checkpoint())
		return -EIO;
	mbhc->mbhc_initialized = true;
	mbhc->mbhc_cfg = NULL;
	mbhc->default_calibration = NULL;
	initialized++;
	return 0;
}
void wcd_mbhc_deinit(struct wcd_mbhc *mbhc)
{
	if (!mbhc->mbhc_initialized)
		return;
	wcd_mbhc_stop(mbhc);
	mbhc->mbhc_initialized = false;
	initialized--;
}
int wcd_mbhc_start(struct wcd_mbhc *mbhc, struct wcd_mbhc_config *config)
{
	if (!mbhc->mbhc_initialized || !config)
		return -EINVAL;
	assert(config->live);
	if (mbhc->mbhc_started)
		return mbhc->mbhc_cfg == config ? 0 : -EBUSY;
	mbhc->mbhc_cfg = config;
	mbhc->default_calibration = config->calibration;
	if (checkpoint()) {
		wcd_mbhc_stop(mbhc);
		return -EIO;
	}
	if (block_start) {
		assert(!pthread_mutex_lock(&gate_lock));
		start_entered = true;
		assert(!pthread_cond_broadcast(&gate_cond));
		while (!start_release)
			assert(!pthread_cond_wait(&gate_cond, &gate_lock));
		assert(!pthread_mutex_unlock(&gate_lock));
	}
	assert(config->live);
	mbhc->mbhc_started = true;
	active++;
	starts++;
	return 0;
}
int snd_soc_add_component_controls(struct snd_soc_component *component, const void *controls, size_t count)
{
	assert(initialized == 1 && count > 0);
	return checkpoint();
}
int snd_soc_component_update_bits(struct snd_soc_component *component, unsigned int reg, unsigned int mask, unsigned int value) { return 0; }
void wcd_disable_irq(void *info, int irq) { assert(initialized == 1); irq_disables++; }
int codec_micbias_control(struct snd_soc_component *component, int micb, int action, bool dapm) { return 0; }
void rdown_timer_callback(struct timer_list *timer) {}
void timer_setup(struct timer_list *timer, void (*callback)(struct timer_list *), int flags) { timer->initialized = true; }
void timer_shutdown_sync(struct timer_list *timer)
{
	assert(timer->initialized && initialized == 0);
	timer->shutdown = true;
	timer_shutdowns++;
}
int usbcss_hs_sysfs_init(struct codec_priv *priv)
{
	if (checkpoint())
		return -EIO;
	assert(!priv->sysfs_alive);
	priv->sysfs_alive = true;
	return 0;
}
void usbcss_hs_sysfs_deinit(struct codec_priv *priv) { priv->sysfs_alive = false; }
const int mbhc_cb = 1, intr_ids = 2;
const int wcd_mbhc_registers[] = { 3 };
const int impedance_detect_controls[] = { 1, 2 };
const int hph_type_detect_controls[] = { 1 };

#include "mbhc_production.inc"

static int initialize(struct codec_priv *priv, struct snd_soc_component *component)
{
#if CODEC_KIND == 9378
	return codec_mbhc_init(&priv->mbhc, component);
#else
	return codec_mbhc_init(&priv->mbhc, component, NULL);
#endif
}
static void *recover_thread(void *arg)
{
	assert(codec_mbhc_recover(arg) == 0);
	return NULL;
}
static void *exit_thread(void *arg)
{
	atomic_store(&exit_entered, true);
	codec_mbhc_hs_detect_exit(arg);
	assert(!current_priv->mbhc_config);
	exit_config->live = false;
	atomic_store(&exit_done, true);
	return NULL;
}

int main(void)
{
	struct codec_priv priv = {};
	struct codec_pdata pdata = { .micbias.micb2_mv = 1800 };
	struct device dev = { .data = &priv, .pdata = &pdata };
	struct snd_soc_component component = { .dev = &dev };
	struct wcd_mbhc_config a = { true, 1, (void *)0x1000 };
	struct wcd_mbhc_config b = { true, 2, (void *)0x2000 };
	int init_steps;
	current_priv = &priv;
	mutex_init(&priv.mbhc_lock);
	assert(initialize(&priv, &component) == 0);
	init_steps = steps;
	assert(priv.mbhc && initialized == 1 && allocations == 1);
	assert(initialize(&priv, &component) == -EBUSY);
	codec_mbhc_deinit(&component);
	assert(!priv.mbhc && !priv.mbhc_config && allocations == 0);

	for (int failure = 1; failure <= init_steps; failure++) {
		steps = 0;
		fail_at = failure;
		assert(initialize(&priv, &component) < 0);
		assert(!priv.mbhc && !priv.mbhc_config);
		assert(allocations == 0 && initialized == 0 && active == 0);
		assert(!priv.sysfs_alive);
	}
	fail_at = 0;
	steps = 0;
	assert(initialize(&priv, &component) == 0);
	assert(codec_mbhc_hs_detect(NULL, &a) == -EINVAL);
	assert(codec_mbhc_hs_detect(&component, NULL) == -EINVAL);
	assert(codec_mbhc_post_ssr_init(NULL, &component) == -EINVAL);
	fail_at = steps + 1;
	assert(codec_mbhc_hs_detect(&component, &a) == -EIO);
	assert(!priv.mbhc_config && !priv.mbhc->wcd_mbhc.mbhc_cfg);
	assert(!priv.mbhc->wcd_mbhc.default_calibration);
	fail_at = 0;
	assert(codec_mbhc_hs_detect(&component, &a) == 0);
	assert(codec_mbhc_hs_detect(&component, &a) == 0);
	assert(codec_mbhc_hs_detect(&component, &b) == -EBUSY);
	assert(priv.mbhc_config == &a && active == 1);
	codec_mbhc_ssr_down(priv.mbhc, &component);
	assert(priv.mbhc_config == &a && initialized == 0 && active == 0);
	assert(codec_mbhc_recover(&component) == 0);
	assert(priv.mbhc->wcd_mbhc.mbhc_cfg == &a && initialized == 1 && active == 1);
	codec_mbhc_hs_detect_exit(&component);
	assert(!priv.mbhc_config && !priv.mbhc->wcd_mbhc.mbhc_cfg);
	assert(!priv.mbhc->wcd_mbhc.default_calibration && active == 0);
	a.live = false;
	codec_mbhc_ssr_down(priv.mbhc, &component);
	assert(codec_mbhc_recover(&component) == 0 && active == 0);
	assert(codec_mbhc_hs_detect(&component, &b) == 0);
	codec_mbhc_ssr_down(priv.mbhc, &component);
	codec_mbhc_hs_detect_exit(&component);
	b.live = false;
	assert(codec_mbhc_recover(&component) == 0 && active == 0);

	a.live = true;
	assert(codec_mbhc_hs_detect(&component, &a) == 0);
	codec_mbhc_ssr_down(priv.mbhc, &component);
	block_start = true;
	exit_config = &a;
	pthread_t recovering, exiting;
	assert(!pthread_create(&recovering, NULL, recover_thread, &component));
	assert(!pthread_mutex_lock(&gate_lock));
	while (!start_entered)
		assert(!pthread_cond_wait(&gate_cond, &gate_lock));
	assert(!pthread_mutex_unlock(&gate_lock));
	assert(!pthread_create(&exiting, NULL, exit_thread, &component));
	while (!atomic_load(&exit_entered))
		usleep(100);
	usleep(10000);
	assert(!atomic_load(&exit_done));
	assert(!pthread_mutex_lock(&gate_lock));
	start_release = true;
	assert(!pthread_cond_broadcast(&gate_cond));
	assert(!pthread_mutex_unlock(&gate_lock));
	assert(!pthread_join(recovering, NULL));
	assert(!pthread_join(exiting, NULL));
	block_start = false;
	assert(!a.live && !priv.mbhc_config && active == 0);
	assert(codec_mbhc_recover(&component) == 0 && active == 0);
	codec_mbhc_deinit(&component);
	codec_mbhc_deinit(&component);
	codec_mbhc_hs_detect_exit(&component);
	assert(codec_mbhc_hs_detect(&component, &b) == -ENODEV);
	assert(!priv.mbhc && !priv.mbhc_config && allocations == 0);
	assert(initialized == 0 && active == 0);
	mutex_destroy(&priv.mbhc_lock);
	printf("PASS: codec %d MBHC lifecycle, %d injected init failures, permanent exit versus SSR, concurrent recovery/exit\\n", CODEC_KIND, init_steps);
	return 0;
}
