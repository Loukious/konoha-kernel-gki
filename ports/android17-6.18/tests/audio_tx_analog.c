/* SERVICES */
/* SOURCE */

static void run_unmute(struct lpass_cdc_tx_macro_priv *p, int id)
{
	p->tx_dec_unmute_work[id].dwork.pending = false;
	mute_stream_dec_unmute(&p->tx_dec_unmute_work[id].dwork.work);
}

static void check_analog(unsigned int mux, unsigned int dec)
{
	static struct regmap map;
	struct device parent = {.regmap = &map}, dev = {.parent = &parent};
	struct snd_soc_component component = {.dev = &parent};
	struct snd_soc_dai dai = {.component = &component, .id = 1};
	struct lpass_cdc_tx_macro_priv p;
	unsigned int reg = LPASS_CDC_TX0_TX_PATH_CTL +
		LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * dec;
	unsigned long bit = 1UL << dec;
	int writes;

	memset(&map, 0, sizeof(map));
	init_tx(&p, &dev);
	macro_dev = &dev;
	p.component = &component;
	for (int id = 0; id < LPASS_CDC_TX_MACRO_MAX_DAIS; id++)
		p.tx_dec_unmute_work[id].tx_priv = &p;
	p.active_ch_mask[1] = p.active_ch_mask[2] = bit;
	map.values[LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
		LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * dec] = mux;
	/* Model hw_params: selected analog path starts with PGA mute set. */
	map.values[reg] = 0x10;
	assert(!p.swr_dmic_enable);
	assert(!lpass_cdc_tx_mute_stream(&dai, 0, 0));
	assert(p.stream_pending[1] == bit);
	assert(p.tx_dec_unmute_work[1].dwork.pending);
	assert(p.tx_dec_unmute_work[1].dwork.delay == 40);
	/* Analog channels must skip the synchronous reset entirely. */
	assert(map.writes == 0 && map.values[reg] == 0x10);
	assert(!lpass_cdc_tx_mute_stream(&dai, 1, 0));
	assert(!p.stream_pending[1] && !p.stream_owned[1]);
	assert(!p.tx_dec_unmute_work[1].dwork.pending);
	assert(!p.dec_ref_cnt[dec] && map.values[reg] == 0x10);

	assert(!lpass_cdc_tx_mute_stream(&dai, 0, 0));
	run_unmute(&p, 1);
	assert(map.values[reg] == 0 && p.dec_ref_cnt[dec] == 1);
	assert(p.stream_owned[1] == bit && !p.stream_pending[1]);
	writes = map.writes;
	assert(!lpass_cdc_tx_mute_stream(&dai, 0, 0));
	assert(!p.tx_dec_unmute_work[1].dwork.pending);
	assert(map.writes == writes && p.dec_ref_cnt[dec] == 1);
	dai.id = 2;
	assert(!lpass_cdc_tx_mute_stream(&dai, 0, 0));
	assert(map.writes == writes);
	run_unmute(&p, 2);
	assert(p.dec_ref_cnt[dec] == 2 && p.stream_owned[2] == bit);
	dai.id = 1;
	p.active_ch_mask[1] = 0;
	assert(!lpass_cdc_tx_mute_stream(&dai, 1, 0));
	assert(p.dec_ref_cnt[dec] == 1 && map.values[reg] == 0);
	dai.id = 2;
	assert(!lpass_cdc_tx_mute_stream(&dai, 1, 0));
	assert(!p.dec_ref_cnt[dec] && map.values[reg] == 0x10);

	/* Failed hardware unmute must not publish a reference or ownership. */
	assert(!lpass_cdc_tx_mute_stream(&dai, 0, 0));
	fault = steps + 1;
	run_unmute(&p, 2);
	fault = 0;
	assert(!p.dec_ref_cnt[dec] && !p.stream_owned[2]);
	assert(!p.stream_pending[2] && map.values[reg] == 0x10);
	/* Headset mute still takes precedence over stream unmute. */
	p.hs_muted_mask = bit;
	assert(!lpass_cdc_tx_mute_stream(&dai, 0, 0));
	run_unmute(&p, 2);
	assert(p.dec_ref_cnt[dec] == 1 && map.values[reg] == 0x10);
	assert(!lpass_cdc_tx_mute_stream(&dai, 1, 0));
	assert(!p.dec_ref_cnt[dec] && !p.hs_muted_mask);

	pthread_mutex_destroy(&p.wlock.lock);
	pthread_mutex_destroy(&p.mclk_lock.lock);
	pthread_mutex_destroy(&p.hs_lock.lock);
	pthread_mutex_destroy(&p.stream_lock.lock);
	pthread_mutex_destroy(&p.hs_wait.lock);
	pthread_cond_destroy(&p.hs_wait.cond);
}

int main(void)
{
	int cases = 0;
	for (unsigned int mux = 1; mux <= 3; mux++) {
		for (unsigned int dec = 0; dec < NUM_DECIMATORS; dec++) {
			check_analog(mux, dec);
			cases++;
		}
	}
	printf("{\"analog_route_lifecycles\":%d}\n", cases);
}
