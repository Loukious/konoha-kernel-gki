#define RX_CLEANUP_TEST 1
#define main resources_main
#include "audio_rx_resources.c"
#undef main

static struct clk hifi;
static void cleanup_reset(void) {
    reset(); rx.hifi_fir_clk = &hifi; rx.is_fir_capable = true;
    rx.component = NULL;
}
int main(void) {
    unsigned int cases = 0;
    cleanup_reset(); assert(!lpass_cdc_rx_macro_init(&component)); int init_steps = step;
    assert(rx.component == &component && rx.dapm_mclk_enable && rx.is_fir_filter_on);
    assert(!lpass_cdc_rx_macro_deinit(&component)); assert(!rx.component); clean(); cases++;
    for (int i = 1; i <= init_steps; i++) {
        cleanup_reset(); fail_first = i;
        assert(lpass_cdc_rx_macro_init(&component) < 0);
        assert(!rx.component && !rx.dapm_mclk_enable && !rx.is_fir_filter_on);
        clean(); cases++;
    }
    cleanup_reset(); assert(!lpass_cdc_rx_macro_init(&component));
    assert(!rx_swrm_clock(&rx, true)); assert(!rx_swrm_clock(&rx, true));
    assert(!lpass_cdc_rx_macro_deinit(&component));
    assert(rx.swr_clk_users == 2 && rx.rx_mclk_users == 1 && !rx.dapm_mclk_enable && !hifi_refs);
    lpass_cdc_rx_macro_release_clocks(&rx); clean(); cases++;
    cleanup_reset(); assert(!lpass_cdc_rx_macro_init(&component));
    assert(!lpass_cdc_rx_macro_mclk_enable(&rx, true, false)); rx.fir_mclk_owned = true;
    assert(!lpass_cdc_rx_macro_deinit(&component)); assert(!rx.fir_mclk_owned); clean(); cases++;
    cleanup_reset(); assert(!lpass_cdc_rx_macro_init(&component)); step = 0; fail_first = 1;
    assert(lpass_cdc_rx_macro_deinit(&component) < 0);
    assert(!rx.component && !rx.dapm_mclk_enable && rx.fir_clock_owned && hifi_refs == 1);
    fail_first = 0; lpass_cdc_rx_macro_release_clocks(&rx); clean(); cases++;
    cleanup_reset(); fail_first = 7; fail_second = 10;
    assert(lpass_cdc_rx_macro_mclk_enable(&rx, true, false) < 0 && rx.failed_clock_owned);
    fail_first = fail_second = 0;
    lpass_cdc_rx_macro_release_clocks(&rx); assert(!rx.failed_clock_owned); clean(); cases++;
    for (int i = 1; i <= 4; i++) {
        cleanup_reset(); fail_first = i;
        assert(lpass_cdc_rx_macro_reset_clock(&rx) < 0 && !pm_refs);
        assert(rx.ssr_clock_owned == (i == 4));
        fail_first = 0;
        assert(!lpass_cdc_rx_macro_reset_clock(&rx)); assert(!rx.ssr_clock_owned);
        clean(); cases++;
    }
    cleanup_reset(); fail_first = 4;
    assert(lpass_cdc_rx_macro_reset_clock(&rx) < 0 && rx.ssr_clock_owned);
    fail_first = 0; rx.pre_dev_up = false;
    lpass_cdc_rx_macro_release_clocks(&rx); assert(!rx.ssr_clock_owned); clean(); cases++;
    cleanup_reset(); fail_first = 1;
    lpass_cdc_rx_macro_disable_pm(&rx);
    assert(!pm_enabled && !pm_suspended);
    fail_first = 0; lpass_cdc_rx_macro_disable_pm(&rx);
    assert(!pm_enabled && pm_suspended); clean(); cases++;
    printf("{\"cases\":%u,\"init_failure_sites\":%d,\"SSR_reset_failure_sites\":4}\n", cases, init_steps);
    return 0;
}
