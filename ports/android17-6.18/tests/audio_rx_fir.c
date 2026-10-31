#define main resources_main
#include "audio_rx_resources.c"
#undef main

static struct clk hifi;
static struct snd_kcontrol control;
static struct snd_ctl_elem_value value;
static struct lpass_cdc_rx_macro_fir_filter_ctl byte_control;
static struct soc_mixer_control count_control;

static void fir_reset(void) {
    reset(); emulate_fir = true; rx.hifi_fir_clk = &hifi; rx.is_fir_capable = true;
    memset(&value, 0, sizeof(value)); memset(&byte_control, 0, sizeof(byte_control));
    memset(&count_control, 0, sizeof(count_control));
    control.component = &component; control.private_value = (unsigned long)&byte_control;
}
static void coefficients(unsigned int p) {
    rx.num_fir_coeff[p][0] = 3; rx.num_fir_coeff[p][1] = 2;
    rx.fir_total_coeff_num[p] = 5;
    for (unsigned int group = 0; group < GRP_MAX; group++)
        for (unsigned int i = 0; i < rx.num_fir_coeff[p][group]; i++)
            rx.fir_coeff_array[p][group][i] = 0xfade0001U + group * 0x1210U + i;
}
static void check_ram(unsigned int p) {
    for (unsigned int group = 0; group < GRP_MAX; group++) {
        assert(rx.is_fir_coeff_written[p][group]);
        for (unsigned int i = 0; i < rx.num_fir_coeff[p][group]; i++)
            assert(fir_ram[p][group * LPASS_CDC_RX_MACRO_FIR_COEFF_MAX + i] == rx.fir_coeff_array[p][group][i]);
        if (rx.num_fir_coeff[p][group] % 2)
            assert(!fir_ram[p][group * LPASS_CDC_RX_MACRO_FIR_COEFF_MAX + rx.num_fir_coeff[p][group]]);
    }
}
int main(void) {
    unsigned int cases = 0;
    fir_reset(); value.value.bytes.data[0] = 1;
    assert(!lpass_cdc_rx_macro_fir_filter_enable_put(&control, &value));
    int enable_steps = step;
    assert(rx.is_fir_filter_on && rx.fir_clock_owned && hifi_refs == 1);
    for (int i = 0; i < 4; i++) assert(!lpass_cdc_rx_macro_fir_filter_enable_put(&control, &value));
    assert(step == enable_steps);
    assert(hifi_refs == 1);
    value.value.bytes.data[0] = 0; step = 0;
    assert(!lpass_cdc_rx_macro_fir_filter_enable_put(&control, &value)); int disable_steps = step;
    assert(!rx.is_fir_filter_on && !rx.fir_clock_owned && !hifi_refs);
    assert(!lpass_cdc_rx_macro_fir_filter_enable_put(&control, &value)); clean(); cases++;
    for (int i = 1; i <= enable_steps; i++) {
        fir_reset(); fail_first = i; value.value.bytes.data[0] = 1;
        assert(lpass_cdc_rx_macro_fir_filter_enable_put(&control, &value) < 0);
        assert(!rx.is_fir_filter_on && !rx.fir_clock_owned && !hifi_refs); clean(); cases++;
    }
    for (int i = 1; i <= disable_steps; i++) {
        fir_reset(); assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, true));
        step = 0; fail_first = i;
        assert(lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, false) < 0);
        assert(!rx.is_fir_filter_on && rx.fir_clock_owned && hifi_refs == 1);
        fail_first = 0; assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, true));
        assert(rx.is_fir_filter_on && hifi_refs == 1);
        assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, false)); clean(); cases++;
    }
    fir_reset(); fail_first = 2; fail_second = 3;
    assert(lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, true) < 0);
    assert(rx.fir_clock_owned && !rx.is_fir_filter_on && hifi_refs == 1);
    fail_first = fail_second = 0;
    assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, false)); clean(); cases++;
    fir_reset(); assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, true));
    step = 0; fail_first = 1;
    assert(lpass_cdc_rx_set_fir_capability(&component, false) < 0);
    assert(rx.is_fir_capable && rx.fir_clock_owned);
    fail_first = 0; assert(!lpass_cdc_rx_set_fir_capability(&component, false));
    assert(!rx.is_fir_capable && !hifi_refs); clean(); cases++;

    fir_reset(); coefficients(0); assert(!set_fir_filter_coeff(&component, &rx, 0));
    int coefficient_steps = step; check_ram(0); clean(); cases++;
    for (int fault = 1; fault <= coefficient_steps; fault++) {
        fir_reset(); coefficients(0); fail_first = fault;
        assert(set_fir_filter_coeff(&component, &rx, 0) < 0);
        assert(!pm_refs && !hifi_refs);
        assert(rx.rx_mclk_users == (rx.fir_mclk_owned ? 1 : 0));
        assert(!rx.is_fir_coeff_written[0][0] || !rx.is_fir_coeff_written[0][1]);
        fail_first = 0;
        assert(!set_fir_filter_coeff(&component, &rx, 0));
        check_ram(0); clean(); cases++;
    }
    fir_reset(); coefficients(1);
    assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, true));
    unsigned int path = map.regs[LPASS_CDC_RX_RX1_RX_PATH_CTL / 4];
    unsigned int top = map.regs[LPASS_CDC_RX_TOP_TOP_CFG1 / 4];
    unsigned int ctl = map.regs[LPASS_CDC_RX_RX1_RX_FIR_CTL / 4];
    assert(!set_fir_filter_coeff(&component, &rx, 1)); check_ram(1);
    assert(rx.is_fir_filter_on && hifi_refs == 1 && !rx.fir_mclk_owned);
    assert(path == map.regs[LPASS_CDC_RX_RX1_RX_PATH_CTL / 4]);
    assert(top == map.regs[LPASS_CDC_RX_TOP_TOP_CFG1 / 4]);
    assert(ctl == map.regs[LPASS_CDC_RX_RX1_RX_FIR_CTL / 4]);
    rx.fir_coeff_array[1][1][0] ^= 0x12345678; rx.is_fir_coeff_written[1][1] = false;
    assert(!set_fir_filter_coeff(&component, &rx, 1)); check_ram(1);
    assert(!lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, false)); clean(); cases++;
    for (unsigned int n = 0; n <= LPASS_CDC_RX_MACRO_FIR_COEFF_MAX; n++) {
        fir_reset();
        for (unsigned int p = 0; p < FIR_PATH_MAX; p++) {
            rx.num_fir_coeff[p][0] = n; rx.num_fir_coeff[p][1] = 100 - n;
            for (unsigned int group = 0; group < GRP_MAX; group++)
                for (unsigned int i = 0; i < rx.num_fir_coeff[p][group]; i++)
                    rx.fir_coeff_array[p][group][i] = UINT32_MAX - (i << 24) - n - group;
            assert(!set_fir_filter_coeff(&component, &rx, p)); check_ram(p);
        }
        clean(); cases++;
    }
    fir_reset(); coefficients(0);
    u32 invalid = UINT32_MAX; memcpy(value.value.bytes.data, &invalid, sizeof(invalid));
    assert(lpass_cdc_rx_macro_fir_audio_mixer_put(&control, &value) == -EINVAL);
    assert(rx.num_fir_coeff[0][0] == 3);
    memset(&value, 0xa5, sizeof(value));
    assert(!lpass_cdc_rx_macro_fir_audio_mixer_get(&control, &value));
    u32 values[LPASS_CDC_RX_MACRO_FIR_COEFF_ARRAY_MAX]; memcpy(values, value.value.bytes.data, sizeof(values));
    assert(values[0] == 3 && values[1] == rx.fir_coeff_array[0][0][0]);
    for (unsigned int i = 4; i < ARRAY_SIZE(values); i++) assert(!values[i]);
    values[0] = 2; values[1] = 0xfedcba98U; values[2] = 0x12345678;
    memcpy(value.value.bytes.data, values, sizeof(values));
    assert(!lpass_cdc_rx_macro_fir_audio_mixer_put(&control, &value));
    assert(rx.num_fir_coeff[0][0] == 2 && !rx.is_fir_coeff_written[0][0]);
    control.private_value = (unsigned long)&count_control;
    value.value.bytes.data[0] = 4;
    assert(!lpass_cdc_rx_macro_fir_coeff_num_put(&control, &value)); check_ram(0);
    value.value.bytes.data[0] = 201;
    assert(lpass_cdc_rx_macro_fir_coeff_num_put(&control, &value) == -EINVAL);
    assert(rx.fir_total_coeff_num[0] == 4);
    count_control.shift = FIR_PATH_MAX;
    assert(lpass_cdc_rx_macro_fir_coeff_num_put(&control, &value) == -EINVAL);
    control.private_value = (unsigned long)&byte_control; byte_control.path_idx = FIR_PATH_MAX;
    assert(lpass_cdc_rx_macro_fir_audio_mixer_get(&control, &value) == -EINVAL);
    assert(lpass_cdc_rx_macro_fir_audio_mixer_put(&control, &value) == -EINVAL);
    byte_control.path_idx = 0; byte_control.grp_idx = GRP_MAX;
    assert(lpass_cdc_rx_macro_fir_audio_mixer_get(&control, &value) == -EINVAL);
    byte_control.grp_idx = 0; rx.num_fir_coeff[0][0] = 101;
    assert(lpass_cdc_rx_macro_fir_audio_mixer_get(&control, &value) == -EINVAL); clean(); cases++;
    fir_reset(); rx.hifi_fir_clk = NULL; value.value.bytes.data[0] = 1;
    assert(!lpass_cdc_rx_macro_fir_filter_enable_put(&control, &value));
    assert(!rx.is_fir_filter_on); coefficients(0);
    assert(!set_fir_filter_coeff(&component, &rx, 0)); assert(!step); clean(); cases++;
    printf("{\"cases\":%u,\"enable_faults\":%d,\"disable_faults\":%d,\"coefficient_faults\":%d,"
           "\"coefficient_count_cases\":202,\"ABI\":\"native 404-byte group, native one-byte total\"}\n",
           cases, enable_steps, disable_steps, coefficient_steps);
}
