static const unsigned int lpass_cdc_rx_macro_dapm_widgets[] = {1};
static const unsigned int rx_audio_map[] = {1};
static const unsigned int lpass_cdc_rx_macro_snd_controls[] = {1};
static int lpass_cdc_rx_macro_mclk_event(struct snd_soc_dapm_widget *, struct snd_kcontrol *, int);
static int lpass_cdc_rx_macro_fir_set_enabled(struct snd_soc_component *, struct lpass_cdc_rx_macro_priv *, bool);
static struct snd_soc_dapm_context *snd_soc_component_get_dapm(struct snd_soc_component *c) {
    assert(c == &component); return &dapm;
}
static int snd_soc_dapm_new_controls(struct snd_soc_dapm_context *d, const unsigned int *controls, size_t n) {
    assert(d == &dapm && controls == lpass_cdc_rx_macro_dapm_widgets && n == 1);
    assert(rx.component == &component && pm_enabled);
    return fail() ? -EIO : 0;
}
static int snd_soc_dapm_add_routes(struct snd_soc_dapm_context *d, const unsigned int *routes, size_t n) {
    assert(d == &dapm && routes == rx_audio_map && n == 1);
    return fail() ? -EIO : 0;
}
static int snd_soc_dapm_new_widgets(void *card) {
    assert(card == dapm.card);
    if (fail()) return -EIO;
    int ret = lpass_cdc_rx_macro_mclk_event(&widget, NULL, SND_SOC_DAPM_PRE_PMU);
    if (ret) return ret;
    return lpass_cdc_rx_macro_fir_set_enabled(&component, &rx, true);
}
static int snd_soc_add_component_controls(struct snd_soc_component *c, const unsigned int *controls, size_t n) {
    assert(c == &component && controls == lpass_cdc_rx_macro_snd_controls && n == 1);
    return fail() ? -EIO : 0;
}
static int snd_soc_dapm_sync(struct snd_soc_dapm_context *d) {
    assert(d == &dapm); return fail() ? -EIO : 0;
}
static void snd_soc_dapm_ignore_suspend(struct snd_soc_dapm_context *d, const char *name) {
    assert(d == &dapm && name);
}
