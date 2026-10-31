#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int irqreturn_t;
#define __iomem
#define ENOTSYNC 522
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define MCLK_FREQ 9600000
#define MCLK_FREQ_NATIVE 11289600
#define SWR_CLK_FREQ 17
#define SND_SOC_DAPM_PRE_PMU 1
#define SND_SOC_DAPM_POST_PMD 2
struct mutex { pthread_mutex_t native; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->native, NULL)); }
static void mutex_destroy(struct mutex *m) { assert(!pthread_mutex_destroy(&m->native)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->native)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->native)); }
typedef struct { int unused; } wait_queue_head_t;
struct device { struct device *parent; void *data; };
struct device_node { int unused; };
struct clk { int unused; };
struct snd_soc_component { struct device *dev; };
struct snd_soc_dapm_context { struct snd_soc_component *component; void *card; };
struct snd_soc_dapm_widget { struct snd_soc_dapm_context *dapm; };
struct snd_kcontrol { struct snd_soc_component *component; unsigned long private_value; };
struct snd_ctl_elem_value { union { struct { u8 data[512]; } bytes; } value; };
struct soc_bytes_ext { unsigned int max; };
struct soc_mixer_control { unsigned int shift; };
struct regmap { unsigned int regs[0x10000 / 4]; };
#include "rx_resource_layout.inc"

static struct regmap map;
static int step, fail_first, fail_second, pm_refs, fs_refs, clock_refs[MAX_CLK];
static int pin_refs, notify_error, notifications, observed_frequency;
static int hifi_refs;
static bool pm_enabled, pm_suspended;
static bool emulate_fir;
static u32 fir_ram[FIR_PATH_MAX][GRP_MAX * LPASS_CDC_RX_MACRO_FIR_COEFF_MAX];
static bool missing_map;
static struct device parent, child;
static struct lpass_cdc_rx_macro_priv rx;
static struct snd_soc_component component;
static struct snd_soc_dapm_context dapm;
static struct snd_soc_dapm_widget widget;
static bool fail(void) { step++; return step == fail_first || step == fail_second; }
static struct device *lpass_cdc_get_device_ptr(struct device *d, int id) {
    assert(d == &parent && id == RX_MACRO); return &child;
}
static void *dev_get_drvdata(struct device *d) { return d->data; }
static struct regmap *dev_get_regmap(struct device *d, const char *name) {
    assert(d == &parent && !name); return missing_map ? NULL : &map;
}
static int pm_runtime_resume_and_get(struct device *d) {
    assert(d == &child); if (fail()) return -EIO; pm_refs++; return 0;
}
static bool lpass_cdc_check_core_votes(struct device *d) {
    assert(d == &child && pm_refs > 0); return !fail();
}
static void pm_runtime_mark_last_busy(struct device *d) { assert(d == &child && pm_refs > 0); }
static int pm_runtime_put_autosuspend(struct device *d) {
    assert(d == &child && pm_refs > 0); pm_refs--; return 0;
}
static int lpass_cdc_clk_rsc_request_clock(struct device *d, int def, int id, bool enable) {
    assert(d == &child && def >= 0 && def < MAX_CLK && id >= 0 && id < MAX_CLK);
    if (fail()) return -EIO;
    if (!enable) assert(clock_refs[id] > 0);
    clock_refs[id] += enable ? 1 : -1;
    return 0;
}
static void lpass_cdc_clk_rsc_fs_gen_request(struct device *d, bool enable) {
    assert(d == &child); if (!enable) assert(fs_refs > 0); fs_refs += enable ? 1 : -1;
}
static void regcache_mark_dirty(struct regmap *m) { assert(m == &map); }
static int regcache_sync_region(struct regmap *m, unsigned int start, unsigned int end) {
    assert(m == &map && start == RX_START_OFFSET && end == RX_MAX_OFFSET);
    return fail() ? -EIO : 0;
}
static int regmap_read(struct regmap *m, unsigned int reg, unsigned int *val) {
    assert(m == &map && !(reg % 4) && reg < sizeof(map.regs));
    if (fail()) return -EIO; *val = m->regs[reg / 4]; return 0;
}
static int regmap_update_bits(struct regmap *m, unsigned int reg, unsigned int mask, unsigned int val) {
    assert(m == &map && !(reg % 4) && reg < sizeof(map.regs));
    /* A failing bus operation may already have changed the register. */
    m->regs[reg / 4] = (m->regs[reg / 4] & ~mask) | (val & mask);
    if (emulate_fir) {
        for (unsigned int p = 0; p < FIR_PATH_MAX; p++) {
            unsigned int path = p ? LPASS_CDC_RX_RX1_RX_PATH_CTL : LPASS_CDC_RX_RX0_RX_PATH_CTL;
            unsigned int ctl = p ? LPASS_CDC_RX_RX1_RX_FIR_CTL : LPASS_CDC_RX_RX0_RX_FIR_CTL;
            unsigned int address = p ? LPASS_CDC_RX_RX1_RX_FIR_COEFF_ADDR : LPASS_CDC_RX_RX0_RX_FIR_COEFF_ADDR;
            unsigned int data = p ? LPASS_CDC_RX_RX1_RX_FIR_COEFF_WDATA0 : LPASS_CDC_RX_RX0_RX_FIR_COEFF_WDATA0;
            if (reg == path && (mask & val & 0x40)) memset(fir_ram[p], 0, sizeof(fir_ram[p]));
            if (reg == ctl && (mask & val & 2)) {
                unsigned int offset = 2 * m->regs[address / 4];
                assert(offset + 1 < ARRAY_SIZE(fir_ram[p]));
                for (unsigned int pair = 0; pair < 2; pair++) {
                    u32 value = 0;
                    for (unsigned int b = 0; b < 4; b++) value |= m->regs[(data + 16 * pair + 4 * b) / 4] << (8 * b);
                    fir_ram[p][offset + pair] = value;
                }
            }
        }
    }
    return fail() ? -EIO : 0;
}
static int clk_prepare_enable(struct clk *c) {
    assert(c); if (fail()) return -EIO; hifi_refs++; return 0;
}
static void clk_disable_unprepare(struct clk *c) { assert(c && hifi_refs > 0); hifi_refs--; }
static struct snd_soc_component *snd_soc_kcontrol_component(struct snd_kcontrol *c) { return c->component; }
static int snd_soc_component_update_bits(struct snd_soc_component *c, unsigned int reg, unsigned int mask, unsigned int val) {
    assert(c == &component);
    unsigned int old = map.regs[reg / 4];
    int ret = regmap_update_bits(&map, reg, mask, val);
    return ret ? ret : old != map.regs[reg / 4];
}
static int snd_soc_component_write(struct snd_soc_component *c, unsigned int reg, unsigned int val) {
    assert(c == &component && !(reg % 4) && reg < sizeof(map.regs));
    map.regs[reg / 4] = val;
    return fail() ? -EIO : 0;
}
static void usleep_range(unsigned int low, unsigned int high) { assert(high >= low); }
static void pm_runtime_disable(struct device *d) { assert(d == &child); pm_enabled = false; }
static bool pm_runtime_status_suspended(struct device *d) { assert(d == &child); return pm_suspended; }
static int lpass_cdc_runtime_suspend(struct device *d) {
    assert(d == &child && !pm_enabled && !pm_refs); return fail() ? -EIO : 0;
}
static int pm_runtime_set_suspended(struct device *d) {
    assert(d == &child && !pm_enabled && !pm_refs); pm_suspended = true; return 0;
}
static int msm_cdc_pinctrl_select_active_state(struct device_node *n) {
    (void)n; if (fail()) return -EIO; assert(!pin_refs); pin_refs++; return 0;
}
static int msm_cdc_pinctrl_select_sleep_state(struct device_node *n) {
    (void)n; if (fail()) return -EIO; assert(pin_refs == 1); pin_refs--; return 0;
}
static struct snd_soc_component *snd_soc_dapm_to_component(struct snd_soc_dapm_context *d) {
    return d->component;
}
static int lpass_cdc_rx_macro_notify_swr(struct lpass_cdc_rx_macro_priv *p, u32 event, void *value) {
    assert(p == &rx && event == SWR_CLK_FREQ && value);
    notifications++; observed_frequency = *(int *)value; return notify_error;
}
#ifdef RX_CLEANUP_TEST
#include "audio_rx_cleanup_services.h"
#endif
#include "rx_resource_functions.inc"

static int total_clocks(void) {
    int total = 0; for (int i = 0; i < MAX_CLK; i++) total += clock_refs[i]; return total;
}
static void reset(void) {
    assert(!pm_refs && !fs_refs && !total_clocks() && !pin_refs && !hifi_refs);
    memset(&rx, 0, sizeof(rx)); memset(&map, 0, sizeof(map));
    step = fail_first = fail_second = notify_error = notifications = 0;
    missing_map = false; child.parent = &parent; child.data = &rx;
    emulate_fir = false; memset(fir_ram, 0, sizeof(fir_ram));
    pm_enabled = true; pm_suspended = false;
    rx.dev = &child; rx.pre_dev_up = true; rx.reset_swr = true;
    rx.default_clk_id = RX_TX_CORE_CLK; rx.clk_id = rx.default_clk_id;
    component.dev = &parent; rx.component = &component;
    dapm.component = &component; widget.dapm = &dapm;
    mutex_init(&rx.mclk_lock); mutex_init(&rx.swr_clk_lock); mutex_init(&rx.fir_lock);
}
static void clean(void) {
    assert(!pm_refs && !fs_refs && !total_clocks() && !pin_refs && !hifi_refs);
    assert(!rx.rx_mclk_users && !rx.swr_clk_users && !rx.swr_mclk_owned &&
           !rx.swr_pins_owned && !rx.swr_enabled && !rx.failed_clock_owned);
    mutex_destroy(&rx.mclk_lock); mutex_destroy(&rx.swr_clk_lock); mutex_destroy(&rx.fir_lock);
}
static void *clock_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 200; i++) {
        assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false));
        assert(!lpass_cdc_rx_macro_mclk_enable(&rx, false, false));
    }
    return NULL;
}
int main(void) {
    unsigned int cases = 0;
    reset(); assert(lpass_cdc_rx_macro_core_vote(NULL, true) == -EINVAL); cases++;
    rx.pre_dev_up = false;
    assert(lpass_cdc_rx_macro_core_vote(&rx, true) == -ENOTSYNC && !pm_refs); cases++;
    rx.pre_dev_up = true;
    for (int fail_at = 1; fail_at <= 2; fail_at++) {
        step = 0; fail_first = fail_at;
        assert(lpass_cdc_rx_macro_core_vote(&rx, true) < 0 && !pm_refs); cases++;
    }
    fail_first = 0;
    assert(!lpass_cdc_rx_macro_core_vote(&rx, true) && pm_refs == 1);
    assert(!lpass_cdc_rx_macro_core_vote(&rx, false)); cases++; clean();

    reset();
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false));
    int enable_steps = step;
    assert(rx.rx_mclk_users == 1 && total_clocks() == 1 && fs_refs == 1 && !pm_refs);
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false));
    assert(rx.rx_mclk_users == 2 && total_clocks() == 1 && fs_refs == 1);
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, false, false));
    step = 0;
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, false, false));
    int disable_steps = step; clean(); cases++;
    for (int i = 1; i <= enable_steps; i++) {
        reset(); fail_first = i;
        assert(lpass_cdc_rx_macro_mclk_enable(&rx, true, false) < 0);
        assert(!(map.regs[LPASS_CDC_RX_CLK_RST_CTRL_MCLK_CONTROL / 4] & 3));
        assert(!(map.regs[LPASS_CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL / 4] & 3));
        clean(); cases++;
    }
    for (int i = 1; i <= disable_steps; i++) {
        reset(); assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false));
        step = 0; fail_first = i;
        assert(lpass_cdc_rx_macro_mclk_enable(&rx, false, false) < 0);
        assert(rx.rx_mclk_users == 1 && total_clocks() == 1 && fs_refs == 1 && !pm_refs);
        assert((map.regs[LPASS_CDC_RX_CLK_RST_CTRL_MCLK_CONTROL / 4] & 3) == 3);
        assert((map.regs[LPASS_CDC_RX_CLK_RST_CTRL_FS_CNT_CONTROL / 4] & 3) == 1);
        fail_first = 0;
        assert(!lpass_cdc_rx_macro_mclk_enable(&rx, false, false)); clean(); cases++;
    }
    reset(); fail_first = 7; fail_second = 10;
    assert(lpass_cdc_rx_macro_mclk_enable(&rx, true, false) < 0);
    assert(rx.failed_clock_owned && !rx.rx_mclk_users && total_clocks() == 1 && !fs_refs);
    fail_first = fail_second = 0; rx.is_native_on = true;
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false));
    assert(!rx.failed_clock_owned && clock_refs[RX_CORE_CLK] == 1 && total_clocks() == 1);
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, false, false)); clean(); cases++;
    reset(); rx.rx_mclk_users = INT_MAX;
    assert(lpass_cdc_rx_macro_mclk_enable(&rx, true, false) == -EOVERFLOW);
    rx.rx_mclk_users = 0;
    assert(lpass_cdc_rx_macro_mclk_enable(&rx, false, false) == -EINVAL); clean(); cases++;
    reset(); assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false)); rx.pre_dev_up = false;
    step = 0; assert(!lpass_cdc_rx_macro_mclk_enable(&rx, false, false)); clean(); cases++;
    reset();
    pthread_t workers[4];
    for (int i = 0; i < 4; i++) assert(!pthread_create(&workers[i], NULL, clock_worker, NULL));
    for (int i = 0; i < 4; i++) assert(!pthread_join(workers[i], NULL));
    clean(); cases++;

    reset(); notify_error = -EIO;
    assert(lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_PRE_PMU) == -EIO);
    assert(!rx.dapm_mclk_enable && !rx.rx_mclk_users);
    notify_error = 0; rx.is_native_on = true;
    assert(!lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_PRE_PMU));
    assert(observed_frequency == MCLK_FREQ_NATIVE);
    assert(!lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_PRE_PMU));
    assert(rx.rx_mclk_users == 1 && notifications == 2);
    step = 0; fail_first = 1;
    assert(lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_POST_PMD) < 0);
    assert(rx.dapm_mclk_enable);
    fail_first = 0;
    assert(!lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_POST_PMD));
    assert(!rx.dapm_mclk_enable);
    assert(!lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_POST_PMD)); clean(); cases++;

    reset(); assert(!rx_swrm_clock(&rx, true)); int swr_enable_steps = step;
    assert(rx.swr_clk_users == 1 && total_clocks() == 1 && pin_refs == 1 && !pm_refs);
    assert(!rx_swrm_clock(&rx, true)); assert(rx.swr_clk_users == 2 && rx.rx_mclk_users == 1);
    assert(!rx_swrm_clock(&rx, false)); step = 0;
    assert(!rx_swrm_clock(&rx, false)); int swr_disable_steps = step; clean(); cases++;
    for (int i = 1; i <= swr_enable_steps; i++) {
        reset(); fail_first = i;
        assert(rx_swrm_clock(&rx, true) < 0);
        assert(!rx.swr_clk_users && !pm_refs); clean(); cases++;
    }
    for (int i = 1; i <= swr_disable_steps; i++) {
        reset(); assert(!rx_swrm_clock(&rx, true)); step = 0; fail_first = i;
        assert(rx_swrm_clock(&rx, false) < 0 && !pm_refs && rx.swr_clk_users == 1);
        fail_first = 0;
        assert(!rx_swrm_clock(&rx, false)); clean(); cases++;
    }
    reset(); assert(!rx_swrm_clock(&rx, true)); step = 0; fail_first = swr_disable_steps;
    assert(rx_swrm_clock(&rx, false) < 0 && rx.swr_pins_owned && !rx.swr_mclk_owned);
    fail_first = 0;
    assert(!rx_swrm_clock(&rx, true));
    assert(rx.swr_enabled && rx.swr_mclk_owned && rx.swr_clk_users == 2);
    assert(!rx_swrm_clock(&rx, false)); assert(!rx_swrm_clock(&rx, false)); clean(); cases++;
    reset(); rx.swr_clk_users = INT_MAX;
    assert(rx_swrm_clock(&rx, true) == -EOVERFLOW); rx.swr_clk_users = 0;
    assert(!rx_swrm_clock(&rx, false)); assert(rx_swrm_clock(NULL, true) == -EINVAL);
    missing_map = true; assert(rx_swrm_clock(&rx, true) == -ENODEV); clean(); cases++;
    printf("{\"cases\":%u,\"mclk_enable_faults\":%d,\"mclk_disable_faults\":%d,"
           "\"swr_enable_faults\":%d,\"swr_disable_faults\":%d,\"concurrent_mclk_cycles\":800}\n",
           cases, enable_steps, disable_steps, swr_enable_steps, swr_disable_steps);
    return 0;
}
