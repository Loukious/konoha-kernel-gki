// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <asoc/msm-cdc-pinctrl.h>
#include "lpass-cdc.h"
#include "lpass-cdc-registers.h"
#include "lpass-cdc-clk-rsc.h"

#define AUTO_SUSPEND_DELAY  50 /* delay in msec */
#define LPASS_CDC_TX_MACRO_MAX_OFFSET 0x1000

#define NUM_DECIMATORS 8

#define LPASS_CDC_TX_MACRO_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
			SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |\
			SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define LPASS_CDC_TX_MACRO_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
		SNDRV_PCM_FMTBIT_S24_LE |\
		SNDRV_PCM_FMTBIT_S24_3LE)

#define  TX_HPF_CUT_OFF_FREQ_MASK	0x60
#define  CF_MIN_3DB_4HZ			0x0
#define  CF_MIN_3DB_75HZ		0x1
#define  CF_MIN_3DB_150HZ		0x2

#define LPASS_CDC_TX_MACRO_DMIC_SAMPLE_RATE_UNDEFINED 0
#define LPASS_CDC_TX_MACRO_TX_PATH_OFFSET \
	(LPASS_CDC_TX1_TX_PATH_CTL - LPASS_CDC_TX0_TX_PATH_CTL)
#define LPASS_CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK 0xF
#define LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET 0x8
#define LPASS_CDC_TX_MACRO_ADC_MODE_CFG0_SHIFT 1

#define LPASS_CDC_TX_MACRO_DMIC_UNMUTE_DELAY_MS	40
#define LPASS_CDC_TX_MACRO_AMIC_UNMUTE_DELAY_MS	100
#define LPASS_CDC_TX_MACRO_DMIC_HPF_DELAY_MS	300
#define LPASS_CDC_TX_MACRO_AMIC_HPF_DELAY_MS	300
#if defined(CONFIG_TARGET_PRODUCT_ONYX)
#define LPASS_CDC_TX_MACRO_DEC_UNMUTE_DELAY_MS  10
#else
#define LPASS_CDC_TX_MACRO_DEC_UNMUTE_DELAY_MS  40
#endif

static struct lpass_cdc_tx_macro_priv *g_lpass_cdc_tx_priv;
static DEFINE_MUTEX(tx_global_lock);


static int tx_unmute_delay = LPASS_CDC_TX_MACRO_DMIC_UNMUTE_DELAY_MS;
module_param(tx_unmute_delay, int, 0664);
MODULE_PARM_DESC(tx_unmute_delay, "delay to unmute the tx path");

static const DECLARE_TLV_DB_SCALE(digital_gain, 0, 1, 0);

static int lpass_cdc_tx_macro_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai);
static int lpass_cdc_tx_macro_get_channel_map(const struct snd_soc_dai *dai,
					      unsigned int *tx_num, unsigned int *tx_slot,
					      unsigned int *rx_num, unsigned int *rx_slot);

#define LPASS_CDC_TX_MACRO_SWR_STRING_LEN 80
#define LPASS_CDC_TX_MACRO_CHILD_DEVICES_MAX 3

enum {
	LPASS_CDC_TX_MACRO_AIF_INVALID = 0,
	LPASS_CDC_TX_MACRO_AIF1_CAP,
	LPASS_CDC_TX_MACRO_AIF2_CAP,
	LPASS_CDC_TX_MACRO_AIF3_CAP,
	LPASS_CDC_TX_MACRO_MAX_DAIS
};

enum {
	LPASS_CDC_TX_MACRO_DEC0,
	LPASS_CDC_TX_MACRO_DEC1,
	LPASS_CDC_TX_MACRO_DEC2,
	LPASS_CDC_TX_MACRO_DEC3,
	LPASS_CDC_TX_MACRO_DEC4,
	LPASS_CDC_TX_MACRO_DEC5,
	LPASS_CDC_TX_MACRO_DEC6,
	LPASS_CDC_TX_MACRO_DEC7,
	LPASS_CDC_TX_MACRO_DEC_MAX,
};

enum {
	LPASS_CDC_TX_MACRO_CLK_DIV_2,
	LPASS_CDC_TX_MACRO_CLK_DIV_3,
	LPASS_CDC_TX_MACRO_CLK_DIV_4,
	LPASS_CDC_TX_MACRO_CLK_DIV_6,
	LPASS_CDC_TX_MACRO_CLK_DIV_8,
	LPASS_CDC_TX_MACRO_CLK_DIV_16,
};

enum {
	MSM_DMIC,
	SWR_MIC,
	ANC_FB_TUNE1
};

enum {
	TX_MCLK,
	VA_MCLK,
};

struct lpass_cdc_tx_macro_reg_mask_val {
	u16 reg;
	u8 mask;
	u8 val;
};

struct tx_mute_work {
	struct lpass_cdc_tx_macro_priv *tx_priv;
	u32 decimator;
	struct delayed_work dwork;
};

struct tx_dec_unmute_work {
	struct lpass_cdc_tx_macro_priv *tx_priv;
	int dai_id;
	struct delayed_work dwork;
};

struct hpf_work {
	struct lpass_cdc_tx_macro_priv *tx_priv;
	u8 decimator;
	u8 hpf_cut_off_freq;
	struct delayed_work dwork;
};

struct lpass_cdc_tx_macro_priv {
	struct device *dev;
	bool dec_active[NUM_DECIMATORS];
	int tx_mclk_users;
	bool dapm_mclk_enable;
	struct mutex mclk_lock;
	struct mutex wlock;
	/* Serializes exported headset requests and synchronous work cancellation. */
	struct mutex hs_lock;
	/* Protects stream ownership and headset mute register state. */
	struct mutex stream_lock;
	unsigned long stream_owned[LPASS_CDC_TX_MACRO_MAX_DAIS];
	unsigned long stream_pending[LPASS_CDC_TX_MACRO_MAX_DAIS];
	wait_queue_head_t hs_wait;
	atomic_t hs_calls;
	bool hs_closing;
	bool removing;
	bool pre_dev_up;
	bool failed_clock_owned;
	unsigned long hs_muted_mask;
	bool dmic_owned[NUM_DECIMATORS];
	unsigned int dmic_id[NUM_DECIMATORS];
	struct snd_soc_component *component;
	struct tx_dec_unmute_work tx_dec_unmute_work[LPASS_CDC_TX_MACRO_MAX_DAIS];
	struct hpf_work tx_hpf_work[NUM_DECIMATORS];
	struct tx_mute_work tx_mute_dwork[NUM_DECIMATORS];
	struct delayed_work tx_hs_unmute_dwork;
	u16 dmic_clk_div[MIC_PAIR_MAX];
	u16 reg_before_mute[NUM_DECIMATORS];
	u32 version;
	unsigned long active_ch_mask[LPASS_CDC_TX_MACRO_MAX_DAIS];
	unsigned long active_ch_cnt[LPASS_CDC_TX_MACRO_MAX_DAIS];
	char __iomem *tx_io_base;
	struct platform_device *pdev_child_devices
			[LPASS_CDC_TX_MACRO_CHILD_DEVICES_MAX];
	int child_count;
	bool bcs_enable;
	int dec_mode[NUM_DECIMATORS];
	int dec_ref_cnt[NUM_DECIMATORS];
	int bcs_ch;
	bool bcs_clk_en;
	bool hs_slow_insert_complete;
	int pcm_rate[NUM_DECIMATORS];
	bool swr_dmic_enable;
	bool swr_dmic_gain_disable;
	int wlock_holders;
};

static int lpass_cdc_tx_macro_wake_enable(struct lpass_cdc_tx_macro_priv *tx_priv,
					 bool enable)
{
	int ret = 0;

	mutex_lock(&tx_priv->wlock);
	if (enable) {
		if (tx_priv->wlock_holders == INT_MAX) {
			ret = -EOVERFLOW;
			goto out;
		}
		if (!tx_priv->wlock_holders++)
			pm_stay_awake(tx_priv->dev);
	} else {
		if (!tx_priv->wlock_holders) {
			ret = -EINVAL;
			goto out;
		}
		if (!--tx_priv->wlock_holders)
			pm_relax(tx_priv->dev);
	}
out:
	mutex_unlock(&tx_priv->wlock);
	return ret;
}

static void lpass_cdc_tx_macro_queue_work(struct lpass_cdc_tx_macro_priv *tx_priv,
					  struct delayed_work *work, int delay)
{
	if (lpass_cdc_tx_macro_wake_enable(tx_priv, true))
		return;
	if (!queue_delayed_work(system_freezable_wq, work,
				msecs_to_jiffies(max(delay, 0))))
		lpass_cdc_tx_macro_wake_enable(tx_priv, false);
}

static void lpass_cdc_tx_macro_cancel_work(struct lpass_cdc_tx_macro_priv *tx_priv)
{
	int i;

	for (i = 0; i < NUM_DECIMATORS; i++) {
		if (cancel_delayed_work_sync(&tx_priv->tx_hpf_work[i].dwork))
			lpass_cdc_tx_macro_wake_enable(tx_priv, false);
		if (cancel_delayed_work_sync(&tx_priv->tx_mute_dwork[i].dwork))
			lpass_cdc_tx_macro_wake_enable(tx_priv, false);
	}
	for (i = 0; i < LPASS_CDC_TX_MACRO_MAX_DAIS; i++)
		cancel_delayed_work_sync(&tx_priv->tx_dec_unmute_work[i].dwork);
	cancel_delayed_work_sync(&tx_priv->tx_hs_unmute_dwork);
}

static bool lpass_cdc_tx_macro_get_data(struct snd_soc_component *component,
			      struct device **tx_dev,
			      struct lpass_cdc_tx_macro_priv **tx_priv,
			      const char *func_name)
{
	if (!component || !tx_dev || !tx_priv)
		return false;
	*tx_dev = lpass_cdc_get_device_ptr(component->dev, TX_MACRO);
	if (!(*tx_dev)) {
		dev_err_ratelimited(component->dev,
			"%s: null device for macro!\n", func_name);
		return false;
	}
	*tx_priv = dev_get_drvdata((*tx_dev));
	if (!(*tx_priv) || !(*tx_priv)->component) {
		dev_err_ratelimited(component->dev,
			"%s: priv is null for macro!\n", func_name);
		return false;
	}
	return true;
}

static int lpass_cdc_tx_macro_mclk_enable(struct lpass_cdc_tx_macro_priv *tx_priv,
					  bool enable)
{
	struct regmap *regmap = dev_get_regmap(tx_priv->dev->parent, NULL);
	unsigned int old_mclk = 0, old_fs = 0;
	bool voted = false, first;
	int ret = 0, cleanup;

	if (!regmap)
		return -ENODEV;
	mutex_lock(&tx_priv->mclk_lock);
	if ((enable && tx_priv->tx_mclk_users == INT_MAX) ||
	    (!enable && !tx_priv->tx_mclk_users)) {
		ret = enable ? -EOVERFLOW : -EINVAL;
		goto out;
	}
	if (enable || READ_ONCE(tx_priv->pre_dev_up)) {
		ret = pm_runtime_resume_and_get(tx_priv->dev);
		if (ret < 0)
			goto out;
		voted = true;
		if (!lpass_cdc_check_core_votes(tx_priv->dev)) {
			ret = -ENOTSYNC;
			goto vote;
		}
	}
	if (tx_priv->failed_clock_owned) {
		ret = lpass_cdc_clk_rsc_request_clock(tx_priv->dev, TX_CORE_CLK,
						      TX_CORE_CLK, false);
		if (ret)
			goto vote;
		tx_priv->failed_clock_owned = false;
	}
	first = !tx_priv->tx_mclk_users;
	if (enable) {
		ret = lpass_cdc_clk_rsc_request_clock(tx_priv->dev, TX_CORE_CLK,
						      TX_CORE_CLK, true);
		if (ret)
			goto vote;
		lpass_cdc_clk_rsc_fs_gen_request(tx_priv->dev, true);
		regcache_mark_dirty(regmap);
		ret = regcache_sync_region(regmap, TX_START_OFFSET, TX_MAX_OFFSET);
		if (ret)
			goto rollback_clock;
	}
	if (first || (!enable && tx_priv->tx_mclk_users == 1)) {
		ret = regmap_read(regmap, LPASS_CDC_TX_CLK_RST_CTRL_MCLK_CONTROL, &old_mclk);
		if (ret)
			goto rollback_clock;
		ret = regmap_read(regmap, LPASS_CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL, &old_fs);
		if (ret)
			goto rollback_clock;
		if (enable) {
			ret = regmap_update_bits(regmap,
						 LPASS_CDC_TX_CLK_RST_CTRL_MCLK_CONTROL, 1, 1);
			if (!ret)
				ret = regmap_update_bits(regmap,
							 LPASS_CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL,
							 1, 1);
		} else {
			ret = regmap_update_bits(regmap,
						 LPASS_CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL, 1, 0);
			if (!ret)
				ret = regmap_update_bits(regmap,
							 LPASS_CDC_TX_CLK_RST_CTRL_MCLK_CONTROL, 1,
							 0);
		}
		if (ret)
			goto rollback_regs;
	}
	if (!enable) {
		lpass_cdc_clk_rsc_fs_gen_request(tx_priv->dev, false);
		ret = lpass_cdc_clk_rsc_request_clock(tx_priv->dev, TX_CORE_CLK,
						      TX_CORE_CLK, false);
		if (ret) {
			lpass_cdc_clk_rsc_fs_gen_request(tx_priv->dev, true);
			if (tx_priv->tx_mclk_users == 1)
				goto rollback_regs;
			goto vote;
		}
	}
	tx_priv->tx_mclk_users += enable ? 1 : -1;
	goto vote;
rollback_regs:
	cleanup = regmap_update_bits(regmap, LPASS_CDC_TX_CLK_RST_CTRL_MCLK_CONTROL,
				     1, old_mclk);
	if (cleanup)
		dev_err(tx_priv->dev, "TX MCLK register rollback failed: %d\n", cleanup);
	cleanup = regmap_update_bits(regmap, LPASS_CDC_TX_CLK_RST_CTRL_FS_CNT_CONTROL,
				     1, old_fs);
	if (cleanup)
		dev_err(tx_priv->dev, "TX FS register rollback failed: %d\n", cleanup);
rollback_clock:
	if (enable) {
		lpass_cdc_clk_rsc_fs_gen_request(tx_priv->dev, false);
		cleanup = lpass_cdc_clk_rsc_request_clock(tx_priv->dev, TX_CORE_CLK,
							  TX_CORE_CLK, false);
		if (cleanup) {
			tx_priv->failed_clock_owned = true;
			dev_err(tx_priv->dev, "TX clock rollback failed: %d\n", cleanup);
		}
	}
vote:
	if (voted) {
		pm_runtime_mark_last_busy(tx_priv->dev);
		pm_runtime_put_autosuspend(tx_priv->dev);
	}
out:
	mutex_unlock(&tx_priv->mclk_lock);
	return ret;
}

static int __lpass_cdc_tx_macro_mclk_enable(struct snd_soc_component *component,
				  bool enable)
{
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	return lpass_cdc_tx_macro_mclk_enable(tx_priv, enable);
}

static int lpass_cdc_tx_macro_mclk_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(w->dapm);
	int ret = 0;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	dev_dbg(tx_dev, "%s: event = %d\n", __func__, event);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (tx_priv->dapm_mclk_enable)
			return -EBUSY;
		ret = lpass_cdc_tx_macro_mclk_enable(tx_priv, 1);
		if (ret)
			tx_priv->dapm_mclk_enable = false;
		else
			tx_priv->dapm_mclk_enable = true;
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (tx_priv->dapm_mclk_enable) {
			ret = lpass_cdc_tx_macro_mclk_enable(tx_priv, 0);
			if (!ret)
				tx_priv->dapm_mclk_enable = false;
		}
		break;
	default:
		dev_err_ratelimited(tx_priv->dev,
			"%s: invalid DAPM event %d\n", __func__, event);
		ret = -EINVAL;
	}
	return ret;
}

static int lpass_cdc_tx_macro_event_handler(struct snd_soc_component *component,
					   u16 event, u32 data)
{
	struct device *tx_dev;
	struct lpass_cdc_tx_macro_priv *tx_priv;
	int ret = 0;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;
	switch (event) {
	case LPASS_CDC_MACRO_EVT_SSR_DOWN:
		WRITE_ONCE(tx_priv->pre_dev_up, false);
		break;
	case LPASS_CDC_MACRO_EVT_PRE_SSR_UP:
	case LPASS_CDC_MACRO_EVT_SSR_UP:
		WRITE_ONCE(tx_priv->pre_dev_up, true);
		break;
	case LPASS_CDC_MACRO_EVT_CLK_RESET:
		ret = lpass_cdc_rsc_clk_reset(tx_dev, TX_CORE_CLK);
		break;
	case LPASS_CDC_MACRO_EVT_BCS_CLK_OFF:
		if (data > 1)
			return -EINVAL;
		if (tx_priv->bcs_clk_en)
			ret = snd_soc_component_update_bits(component,
							    LPASS_CDC_TX0_TX_PATH_SEC7, 0x40,
							    data << 6);
		if (ret >= 0) {
			tx_priv->hs_slow_insert_complete = data;
			ret = 0;
		}
		break;
	default:
		break;
	}
	return ret;
}

static bool is_amic_enabled(struct snd_soc_component *component, int decimator)
{
	u16 adc_mux_reg = 0;
	bool ret = false;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return ret;

	adc_mux_reg = LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
			LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * decimator;

	if (snd_soc_component_read(component, adc_mux_reg) & SWR_MIC) {
		if (!tx_priv->swr_dmic_enable)
			return true;
	}

	return ret;
}


/* swr-dmic   ADC Mux Sel
 *  0           0           MSM DMIC
 *  0           1           SWR AMIC
 *  1           0           INVALID
 *  1           1           SWR DMIC
 */
 /* This function checks if the decimator is fed from SWR MIC or VA MIC (DMIC) */
static bool is_msm_dmic_enabled(struct snd_soc_component *component, int decimator)
{
	u16 adc_mux_reg = 0;
	bool ret = false;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return ret;

	adc_mux_reg = LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
			LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * decimator;

	/* ADC_MUX_SEL is VA_MIC and swr_dmic_enable is 0, then dec is for msm-dmic use-case */
	if ((snd_soc_component_read(component, adc_mux_reg) & 0x3) == MSM_DMIC)
		return true;

	return ret;
}

static void lpass_cdc_tx_macro_update_bits(struct snd_soc_component *component,
					   unsigned int reg, unsigned int mask, unsigned int value,
					   int *status)
{
	int ret = snd_soc_component_update_bits(component, reg, mask, value);

	if (ret < 0 && !*status)
		*status = ret;
}

static void lpass_cdc_tx_macro_tx_hpf_corner_freq_callback(struct work_struct *work)
{
	int status = 0;
	struct delayed_work *hpf_delayed_work = NULL;
	struct hpf_work *hpf_work = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct snd_soc_component *component = NULL;
	u16 dec_cfg_reg = 0, hpf_gate_reg = 0;
	u8 hpf_cut_off_freq = 0;
	u16 adc_reg = 0, adc_n = 0;

	hpf_delayed_work = to_delayed_work(work);
	hpf_work = container_of(hpf_delayed_work, struct hpf_work, dwork);
	tx_priv = hpf_work->tx_priv;
	component = tx_priv->component;
	hpf_cut_off_freq = hpf_work->hpf_cut_off_freq;

	dec_cfg_reg = LPASS_CDC_TX0_TX_PATH_CFG0 +
			LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * hpf_work->decimator;
	hpf_gate_reg = LPASS_CDC_TX0_TX_PATH_SEC2 +
			LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * hpf_work->decimator;

	dev_dbg(component->dev, "%s: decimator %u hpf_cut_of_freq 0x%x\n",
		__func__, hpf_work->decimator, hpf_cut_off_freq);

	if (is_amic_enabled(component, hpf_work->decimator)) {
		adc_reg = LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG0 +
			LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * hpf_work->decimator;
		adc_n = snd_soc_component_read(component, adc_reg) &
				LPASS_CDC_TX_MACRO_SWR_MIC_MUX_SEL_MASK;
		/* analog mic clear TX hold */
		lpass_cdc_clear_amic_tx_hold(component->dev, adc_n);
		lpass_cdc_tx_macro_update_bits(component,
					       dec_cfg_reg, TX_HPF_CUT_OFF_FREQ_MASK,
					       hpf_cut_off_freq << 5, &status);
		lpass_cdc_tx_macro_update_bits(component, hpf_gate_reg,
					       0x03, 0x02, &status);
		/* Add delay between toggle hpf gate based on sample rate */
		switch (tx_priv->pcm_rate[hpf_work->decimator]) {
		case 0:
			usleep_range(125, 130);
			break;
		case 1:
			usleep_range(62, 65);
			break;
		case 3:
			usleep_range(31, 32);
			break;
		case 4:
			usleep_range(20, 40);
			break;
		case 5:
			usleep_range(10, 11);
			break;
		case 6:
			usleep_range(5, 6);
			break;
		default:
			usleep_range(125, 130);
		}
		lpass_cdc_tx_macro_update_bits(component, hpf_gate_reg,
					       0x03, 0x01, &status);
	} else {
		lpass_cdc_tx_macro_update_bits(component,
					       dec_cfg_reg, TX_HPF_CUT_OFF_FREQ_MASK,
					       hpf_cut_off_freq << 5, &status);
		lpass_cdc_tx_macro_update_bits(component, hpf_gate_reg,
					       0x02, 0x02, &status);
		/* Minimum 1 clk cycle delay is required as per HW spec */
		usleep_range(1000, 1010);
		lpass_cdc_tx_macro_update_bits(component, hpf_gate_reg,
					       0x02, 0x00, &status);
	}
	if (status)
		dev_err_ratelimited(tx_priv->dev, "delayed TX write failed: %d\n", status);
	lpass_cdc_tx_macro_wake_enable(tx_priv, 0);
}

static void lpass_cdc_tx_macro_mute_update_callback(struct work_struct *work)
{
	int status = 0;
	struct tx_mute_work *tx_mute_dwork = NULL;
	struct snd_soc_component *component = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct delayed_work *delayed_work = NULL;
	u16 tx_vol_ctl_reg = 0;
	u8 decimator = 0;

	delayed_work = to_delayed_work(work);
	tx_mute_dwork = container_of(delayed_work, struct tx_mute_work, dwork);
	tx_priv = tx_mute_dwork->tx_priv;
	component = tx_priv->component;
	decimator = tx_mute_dwork->decimator;

	tx_vol_ctl_reg =
		LPASS_CDC_TX0_TX_PATH_CTL +
			LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
	lpass_cdc_tx_macro_update_bits(component, tx_vol_ctl_reg, 0x10, 0x00, &status);
	dev_dbg(tx_priv->dev, "%s: decimator %u unmute\n",
		__func__, decimator);
	if (status)
		dev_err_ratelimited(tx_priv->dev, "delayed TX write failed: %d\n", status);
	lpass_cdc_tx_macro_wake_enable(tx_priv, 0);
}

static void mute_stream_dec_unmute(struct work_struct *work)
{
	struct tx_dec_unmute_work *job = container_of(to_delayed_work(work),
		struct tx_dec_unmute_work, dwork);
	struct lpass_cdc_tx_macro_priv *tx_priv = job->tx_priv;
	struct snd_soc_component *component = tx_priv->component;
	unsigned int decimator, reg, gain_reg;
	int dai_id = job->dai_id, ret;

	mutex_lock(&tx_priv->stream_lock);
	for_each_set_bit(decimator, &tx_priv->stream_pending[dai_id], NUM_DECIMATORS) {
		if (test_bit(decimator, &tx_priv->stream_owned[dai_id]))
			continue;
		if (tx_priv->dec_ref_cnt[decimator] == INT_MAX) {
			dev_err(tx_priv->dev, "DEC%u reference overflow\n", decimator);
			continue;
		}
		reg = LPASS_CDC_TX0_TX_PATH_CTL + LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
		ret = snd_soc_component_update_bits(component, reg, 0x10,
						    test_bit(decimator,
							     &tx_priv->hs_muted_mask) ? 0x10 : 0);
		if (ret < 0)
			goto failed;
		if (tx_priv->swr_dmic_gain_disable) {
			gain_reg = LPASS_CDC_TX0_TX_PATH_CFG1 +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
			ret = snd_soc_component_update_bits(component, gain_reg, 0x01, 0x00);
			if (ret < 0)
				goto failed;
		}
		tx_priv->dec_ref_cnt[decimator]++;
		set_bit(decimator, &tx_priv->stream_owned[dai_id]);
		continue;
failed:
		dev_err(tx_priv->dev, "DEC%u unmute failed: %d\n", decimator, ret);
		if (!tx_priv->dec_ref_cnt[decimator]) {
			ret = snd_soc_component_update_bits(component, reg, 0x10, 0x10);
			if (ret < 0)
				dev_err(tx_priv->dev,
					"DEC%u mute rollback failed: %d\n", decimator, ret);
		}
	}
	tx_priv->stream_pending[dai_id] = 0;
	mutex_unlock(&tx_priv->stream_lock);
}

static int lpass_cdc_tx_macro_put_dec_enum(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val = 0;
	int status = 0;
	u16 mic_sel_reg = 0;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	val = ucontrol->value.enumerated.item[0];
	if (val > e->items - 1)
		return -EINVAL;

	dev_dbg(component->dev, "%s: wname: %s, val: 0x%x\n", __func__,
		widget->name, val);

	switch (e->reg) {
	case LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG0:
		mic_sel_reg = LPASS_CDC_TX0_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX1_CFG0:
		mic_sel_reg = LPASS_CDC_TX1_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX2_CFG0:
		mic_sel_reg = LPASS_CDC_TX2_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX3_CFG0:
		mic_sel_reg = LPASS_CDC_TX3_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX4_CFG0:
		mic_sel_reg = LPASS_CDC_TX4_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX5_CFG0:
		mic_sel_reg = LPASS_CDC_TX5_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX6_CFG0:
		mic_sel_reg = LPASS_CDC_TX6_TX_PATH_CFG0;
		break;
	case LPASS_CDC_TX_INP_MUX_ADC_MUX7_CFG0:
		mic_sel_reg = LPASS_CDC_TX7_TX_PATH_CFG0;
		break;
	default:
		dev_err_ratelimited(component->dev, "%s: e->reg: 0x%x not expected\n",
			__func__, e->reg);
		return -EINVAL;
	}
	if (strnstr(widget->name, "SMIC", strlen(widget->name))) {
		if (val != 0) {
			if (!tx_priv->swr_dmic_enable) {
				lpass_cdc_tx_macro_update_bits(component,
							       mic_sel_reg,
							       1 << 7, 0x0 << 7, &status);
			} else {
				lpass_cdc_tx_macro_update_bits(component,
							       mic_sel_reg,
							       1 << 7, 0x1 << 7, &status);
				lpass_cdc_tx_macro_update_bits(component,
							       LPASS_CDC_VA_TOP_CSR_DMIC_CFG,
							       0x80, 0x00, &status);
			}
		}
	} else {
		/* DMIC selected */
		if (val != 0)
			lpass_cdc_tx_macro_update_bits(component, mic_sel_reg,
						       1 << 7, 1 << 7, &status);
	}

	if (status)
		return status;
	return snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
}

static int lpass_cdc_tx_macro_tx_mixer_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct soc_mixer_control *mixer =
		((struct soc_mixer_control *)kcontrol->private_value);
	u32 dai_id = widget->shift;
	u32 dec_id = mixer->shift;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev,
					 &tx_priv, __func__))
		return -EINVAL;

	if (!dai_id || dai_id >= LPASS_CDC_TX_MACRO_MAX_DAIS ||
	    dec_id >= NUM_DECIMATORS)
		return -EINVAL;
	if (test_bit(dec_id, &tx_priv->active_ch_mask[dai_id]))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;
	return 0;
}

static int lpass_cdc_tx_macro_tx_mixer_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget =
		snd_soc_dapm_kcontrol_widget(kcontrol);
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(widget->dapm);
	struct snd_soc_dapm_update *update = NULL;
	struct soc_mixer_control *mixer =
		((struct soc_mixer_control *)kcontrol->private_value);
	u32 dai_id = widget->shift;
	u32 dec_id = mixer->shift;
	u32 enable = ucontrol->value.integer.value[0];
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	unsigned long old_mask;
	unsigned long old_count;
	int ret;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev,
					 &tx_priv, __func__))
		return -EINVAL;

	if (!dai_id || dai_id >= LPASS_CDC_TX_MACRO_MAX_DAIS ||
	    dec_id >= NUM_DECIMATORS || enable > 1)
		return -EINVAL;
	old_mask = tx_priv->active_ch_mask[dai_id];
	old_count = tx_priv->active_ch_cnt[dai_id];
	if (!!test_bit(dec_id, &old_mask) == !!enable)
		return 0;
	if (enable) {
		set_bit(dec_id, &tx_priv->active_ch_mask[dai_id]);
		tx_priv->active_ch_cnt[dai_id]++;
	} else {
		clear_bit(dec_id, &tx_priv->active_ch_mask[dai_id]);
		tx_priv->active_ch_cnt[dai_id]--;
	}
	ret = snd_soc_dapm_mixer_update_power(widget->dapm, kcontrol, enable, update);
	if (ret < 0) {
		tx_priv->active_ch_mask[dai_id] = old_mask;
		tx_priv->active_ch_cnt[dai_id] = old_count;
		return ret;
	}
	return 1;
}

static inline int lpass_cdc_tx_macro_path_get(const char *wname,
				    unsigned int *path_num)
{
	int ret = 0;
	char *widget_name = NULL;
	char *w_name = NULL;
	char *path_num_char = NULL;
	char *path_name = NULL;

	widget_name = kstrndup(wname, 10, GFP_KERNEL);
	if (!widget_name)
		return -EINVAL;

	w_name = widget_name;

	path_name = strsep(&widget_name, " ");
	if (!path_name) {
		pr_err_ratelimited("%s: Invalid widget name = %s\n",
			__func__, widget_name);
		ret = -EINVAL;
		goto err;
	}
	path_num_char = strpbrk(path_name, "01234567");
	if (!path_num_char) {
		pr_err_ratelimited("%s: tx path index not found\n",
			__func__);
		ret = -EINVAL;
		goto err;
	}
	ret = kstrtouint(path_num_char, 10, path_num);
	if (ret < 0)
		pr_err_ratelimited("%s: Invalid tx path = %s\n",
			__func__, w_name);

err:
	kfree(w_name);
	return ret;
}

static void tx_macro_hs_unmute_dwork(struct work_struct *work)
{
	struct lpass_cdc_tx_macro_priv *tx_priv = container_of(to_delayed_work(work),
		struct lpass_cdc_tx_macro_priv, tx_hs_unmute_dwork);
	struct snd_soc_component *component = tx_priv->component;
	unsigned int decimator, reg;
	int ret;

	mutex_lock(&tx_priv->stream_lock);
	ret = pm_runtime_resume_and_get(tx_priv->dev);
	if (ret < 0) {
		dev_err(tx_priv->dev, "headset restore PM failed: %d\n", ret);
		goto out;
	}
	for_each_set_bit(decimator, &tx_priv->hs_muted_mask, NUM_DECIMATORS) {
		reg = LPASS_CDC_TX0_TX_PATH_CTL + LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
		ret = snd_soc_component_update_bits(component, reg, 0x10,
						    tx_priv->reg_before_mute[decimator]);
		if (ret < 0)
			dev_err(tx_priv->dev, "headset restore DEC%u failed: %d\n", decimator, ret);
		else
			clear_bit(decimator, &tx_priv->hs_muted_mask);
	}
	pm_runtime_mark_last_busy(tx_priv->dev);
	pm_runtime_put_autosuspend(tx_priv->dev);
out:
	mutex_unlock(&tx_priv->stream_lock);
}

void lpass_cdc_tx_macro_mute_hs(void)
{
	struct lpass_cdc_tx_macro_priv *tx_priv;
	struct snd_soc_component *component;
	unsigned int decimator, reg, value;
	struct regmap *regmap;
	int ret;

	mutex_lock(&tx_global_lock);
	tx_priv = g_lpass_cdc_tx_priv;
	if (!tx_priv || tx_priv->hs_closing) {
		mutex_unlock(&tx_global_lock);
		return;
	}
	atomic_inc(&tx_priv->hs_calls);
	mutex_unlock(&tx_global_lock);
	/* Serialize callers; the worker never takes this mutex. */
	mutex_lock(&tx_priv->hs_lock);
	cancel_delayed_work_sync(&tx_priv->tx_hs_unmute_dwork);
	mutex_lock(&tx_priv->stream_lock);
	component = tx_priv->component;
	regmap = dev_get_regmap(component->dev, NULL);
	if (!regmap)
		goto out;
	ret = pm_runtime_resume_and_get(tx_priv->dev);
	if (ret < 0) {
		dev_err(tx_priv->dev, "headset mute PM failed: %d\n", ret);
		goto out;
	}
	for_each_set_bit(decimator, &tx_priv->active_ch_mask[LPASS_CDC_TX_MACRO_AIF1_CAP],
			 NUM_DECIMATORS) {
		/* A repeated request extends mute without overwriting the saved state. */
		if (test_bit(decimator, &tx_priv->hs_muted_mask))
			continue;
		reg = LPASS_CDC_TX0_TX_PATH_CTL + LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
		ret = regmap_read(regmap, reg, &value);
		if (ret) {
			dev_err(tx_priv->dev, "headset mute read failed: %d\n", ret);
			continue;
		}
		tx_priv->reg_before_mute[decimator] = value & 0x10;
		/* Retain restoration ownership even if a write reports an error. */
		set_bit(decimator, &tx_priv->hs_muted_mask);
		ret = snd_soc_component_update_bits(component, reg, 0x10, 0x10);
		if (ret < 0)
			dev_err(tx_priv->dev, "headset mute write failed: %d\n", ret);
	}
	pm_runtime_mark_last_busy(tx_priv->dev);
	pm_runtime_put_autosuspend(tx_priv->dev);
out:
	if (tx_priv->hs_muted_mask)
		schedule_delayed_work(&tx_priv->tx_hs_unmute_dwork, msecs_to_jiffies(1200));
	mutex_unlock(&tx_priv->stream_lock);
	mutex_unlock(&tx_priv->hs_lock);
	if (atomic_dec_and_test(&tx_priv->hs_calls))
		wake_up_all(&tx_priv->hs_wait);
}
EXPORT_SYMBOL(lpass_cdc_tx_macro_mute_hs);

static int lpass_cdc_tx_macro_dec_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int ret = 0;
	unsigned int path = 0;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	ret = lpass_cdc_tx_macro_path_get(kcontrol->id.name, &path);
	if (ret)
		return ret;
	if (path >= NUM_DECIMATORS)
		return -EINVAL;

	ucontrol->value.integer.value[0] = tx_priv->dec_mode[path];

	return 0;
}

static int lpass_cdc_tx_macro_dec_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value = ucontrol->value.integer.value[0];
	int ret = 0;
	unsigned int path = 0;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	ret = lpass_cdc_tx_macro_path_get(kcontrol->id.name, &path);
	if (ret)
		return ret;
	if (path >= NUM_DECIMATORS)
		return -EINVAL;

	if (value < 0 || value > 2)
		return -EINVAL;
	tx_priv->dec_mode[path] = value;

	return 0;
}

static int lpass_cdc_tx_macro_bcs_ch_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	ucontrol->value.enumerated.item[0] = tx_priv->bcs_ch;

	return 0;
}

static int lpass_cdc_tx_macro_bcs_ch_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value = ucontrol->value.enumerated.item[0];

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (value < 0 || value > 11)
		return -EINVAL;
	tx_priv->bcs_ch = value;

	return 0;
}

static int lpass_cdc_tx_macro_swr_dmic_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
	return -EINVAL;

	ucontrol->value.integer.value[0] = tx_priv->swr_dmic_enable;

	return 0;
}

static int lpass_cdc_tx_macro_swr_dmic_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value = ucontrol->value.integer.value[0];

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (value < 0 || value > 1)
		return -EINVAL;
	tx_priv->swr_dmic_enable = value;

	return 0;
}


static int lpass_cdc_tx_macro_get_dec_gain_bypass(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
	return -EINVAL;

	ucontrol->value.integer.value[0] = tx_priv->swr_dmic_gain_disable;

	return 0;
}

static int lpass_cdc_tx_macro_put_dec_gain_bypass(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value = ucontrol->value.integer.value[0];

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (value < 0 || value > 1)
		return -EINVAL;
	tx_priv->swr_dmic_gain_disable = value;

	return 0;
}

static int lpass_cdc_tx_macro_get_bcs(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
	return -EINVAL;

	ucontrol->value.integer.value[0] = tx_priv->bcs_enable;

	return 0;
}

static int lpass_cdc_tx_macro_set_bcs(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
			snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value = ucontrol->value.integer.value[0];

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (value < 0 || value > 1)
		return -EINVAL;
	tx_priv->bcs_enable = value;

	return 0;
}

static const char * const bcs_ch_sel_mux_text[] = {
	"SWR_MIC0", "SWR_MIC1", "SWR_MIC2", "SWR_MIC3",
	"SWR_MIC4", "SWR_MIC5", "SWR_MIC6", "SWR_MIC7",
	"SWR_MIC8", "SWR_MIC9", "SWR_MIC10", "SWR_MIC11",
};

static const struct soc_enum bcs_ch_sel_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(bcs_ch_sel_mux_text),
			    bcs_ch_sel_mux_text);

static int lpass_cdc_tx_macro_get_bcs_ch_sel(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value = 0;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	value = (snd_soc_component_read(component,
			LPASS_CDC_VA_TOP_CSR_SWR_CTRL)) & 0x0F;

	ucontrol->value.integer.value[0] = value;
	return 0;
}

static int lpass_cdc_tx_macro_put_bcs_ch_sel(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
				snd_soc_kcontrol_component(kcontrol);
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	struct device *tx_dev = NULL;
	int value, ret;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] >= ARRAY_SIZE(bcs_ch_sel_mux_text))
		return -EINVAL;

	value = ucontrol->value.integer.value[0];
	ret = snd_soc_component_update_bits(component,
					    LPASS_CDC_VA_TOP_CSR_SWR_CTRL, 0x0F, value);

	return ret;
}

static int lpass_cdc_tx_macro_enable_dmic(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event, u16 adc_mux0_cfg)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct lpass_cdc_tx_macro_priv *tx_priv;
	struct device *tx_dev;
	struct regmap *regmap;
	unsigned int mux, decimator = w->shift;
	int ret;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__) ||
	    decimator >= NUM_DECIMATORS)
		return -EINVAL;
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (tx_priv->dmic_owned[decimator])
			return -EBUSY;
		regmap = dev_get_regmap(component->dev, NULL);
		if (!regmap)
			return -ENODEV;
		ret = regmap_read(regmap, adc_mux0_cfg, &mux);
		if (ret)
			return ret;
		mux = (mux >> 4) & 0xf;
		if (!mux || mux > 8)
			return -EINVAL;
		ret = lpass_cdc_dmic_clk_enable(component, mux - 1, DMIC_TX, true);
		if (!ret) {
			tx_priv->dmic_id[decimator] = mux - 1;
			tx_priv->dmic_owned[decimator] = true;
		}
		return ret;
	case SND_SOC_DAPM_POST_PMD:
		if (!tx_priv->dmic_owned[decimator])
			return 0;
		ret = lpass_cdc_dmic_clk_enable(component,
						tx_priv->dmic_id[decimator], DMIC_TX, false);
		if (!ret)
			tx_priv->dmic_owned[decimator] = false;
		return ret;
	default:
		return 0;
	}
}

static int lpass_cdc_tx_macro_enable_dec(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
				snd_soc_dapm_to_component(w->dapm);
	unsigned int decimator = 0;
	u16 tx_vol_ctl_reg = 0;
	u16 dec_cfg_reg = 0;
	u16 dec_gain_reg = 0;
	u16 hpf_gate_reg = 0;
	u16 tx_gain_ctl_reg = 0;
	u16 tx_fs_reg = 0;
	u8 hpf_cut_off_freq = 0, swr_micn = 0;
	u16 adc_mux_reg = 0;
	u16 adc_mux0_reg = 0;
	u16 dmic_clk_reg = 0, dmic_div;
	int hpf_delay = LPASS_CDC_TX_MACRO_DMIC_HPF_DELAY_MS;
	int unmute_delay = LPASS_CDC_TX_MACRO_DMIC_UNMUTE_DELAY_MS;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	u32 mic_pair = 0;
	int status = 0, ret, cleanup;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	decimator = w->shift;
	if (decimator >= NUM_DECIMATORS)
		return -EINVAL;

	dev_dbg(component->dev, "%s(): widget = %s decimator = %u\n", __func__,
			w->name, decimator);

	tx_vol_ctl_reg = LPASS_CDC_TX0_TX_PATH_CTL +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
	hpf_gate_reg = LPASS_CDC_TX0_TX_PATH_SEC2 +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
	dec_cfg_reg = LPASS_CDC_TX0_TX_PATH_CFG0 +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
	dec_gain_reg = LPASS_CDC_TX0_TX_PATH_CFG1 +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
	tx_gain_ctl_reg = LPASS_CDC_TX0_TX_VOL_CTL +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
	adc_mux_reg = LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
			LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * decimator;
	adc_mux0_reg = LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG0 +
			LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * decimator;
	tx_fs_reg = LPASS_CDC_TX0_TX_PATH_CTL +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;

	tx_priv->pcm_rate[decimator] = (snd_soc_component_read(component,
				     tx_fs_reg) & 0x0F);

	if (is_msm_dmic_enabled(component, decimator) || tx_priv->dmic_owned[decimator]) {
		ret = lpass_cdc_tx_macro_enable_dmic(w, kcontrol, event, adc_mux0_reg);
		if (ret && event == SND_SOC_DAPM_PRE_PMU)
			return ret;
		status = ret;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		lpass_cdc_tx_macro_update_bits(component,
					       dec_cfg_reg, 0x06, tx_priv->dec_mode[decimator] <<
					       LPASS_CDC_TX_MACRO_ADC_MODE_CFG0_SHIFT, &status);
		break;
	case SND_SOC_DAPM_POST_PMU:
		/* if SWR DMIC is used, set SWR_MICn clk div */
		if (tx_priv->swr_dmic_enable) {
			swr_micn = snd_soc_component_read(component, adc_mux0_reg) & 0xf;
			if (swr_micn > 0 && swr_micn <= 4) {
				dmic_clk_reg = LPASS_CDC_TX_TOP_CSR_SWR_MIC0_CTL;
				mic_pair = MIC_PAIR01;
			} else if (swr_micn > 4 && swr_micn <= 7) {
				dmic_clk_reg = LPASS_CDC_TX_TOP_CSR_SWR_MIC1_CTL;
				mic_pair = MIC_PAIR23;
			} else if (swr_micn > 7 && swr_micn <= 11) {
				dmic_clk_reg = LPASS_CDC_TX_TOP_CSR_SWR_MIC2_CTL;
				mic_pair = MIC_PAIR45;
			}

			if (dmic_clk_reg) {
				dmic_div = tx_priv->dmic_clk_div[mic_pair] << 1;
				lpass_cdc_tx_macro_update_bits(component, dmic_clk_reg,
							       0x0E, dmic_div, &status);
			}
		}
		lpass_cdc_tx_macro_update_bits(component,
					       tx_vol_ctl_reg, 0x20, 0x20, &status);
		if (!is_amic_enabled(component, decimator)) {
			lpass_cdc_tx_macro_update_bits(component,
						       hpf_gate_reg, 0x01, 0x00, &status);
			/*
		 	 * Minimum 1 clk cycle delay is required as per HW spec
		 	 */
			usleep_range(1000, 1010);
		}
		hpf_cut_off_freq = (
			snd_soc_component_read(component, dec_cfg_reg) &
				TX_HPF_CUT_OFF_FREQ_MASK) >> 5;

		tx_priv->tx_hpf_work[decimator].hpf_cut_off_freq =
						hpf_cut_off_freq;

		if (hpf_cut_off_freq != CF_MIN_3DB_150HZ)
			lpass_cdc_tx_macro_update_bits(component, dec_cfg_reg,
						       TX_HPF_CUT_OFF_FREQ_MASK,
						       CF_MIN_3DB_150HZ << 5, &status);

		if (is_amic_enabled(component, decimator)) {
			hpf_delay = LPASS_CDC_TX_MACRO_AMIC_HPF_DELAY_MS;
			unmute_delay = LPASS_CDC_TX_MACRO_AMIC_UNMUTE_DELAY_MS;
		}
		if (tx_priv->tx_hpf_work[decimator].hpf_cut_off_freq !=
							CF_MIN_3DB_150HZ) {
			lpass_cdc_tx_macro_update_bits(component,
						       hpf_gate_reg, 0x03, 0x02, &status);
			if (!is_amic_enabled(component, decimator))
				lpass_cdc_tx_macro_update_bits(component,
							       hpf_gate_reg, 0x03, 0x00, &status);
			lpass_cdc_tx_macro_update_bits(component,
						       hpf_gate_reg, 0x03, 0x01, &status);
			/*
			 * 6ms delay is required as per HW spec
			 */
			usleep_range(6000, 6010);
		}
		/* apply gain after decimator is enabled */
		ret = snd_soc_component_write(component, tx_gain_ctl_reg,
					      snd_soc_component_read(component, tx_gain_ctl_reg));
		if (ret < 0 && !status)
			status = ret;
		if (tx_priv->bcs_enable) {
			lpass_cdc_tx_macro_update_bits(component,
						       LPASS_CDC_VA_TOP_CSR_SWR_CTRL, 0x0F,
						       tx_priv->bcs_ch, &status);

			lpass_cdc_tx_macro_update_bits(component, dec_cfg_reg,
						       0x01, 0x01, &status);
			tx_priv->bcs_clk_en = true;
			if (tx_priv->hs_slow_insert_complete)
				lpass_cdc_tx_macro_update_bits(component,
							       LPASS_CDC_TX0_TX_PATH_SEC7, 0x40,
							       0x40, &status);
		}
		if (!status && hpf_cut_off_freq != CF_MIN_3DB_150HZ)
			lpass_cdc_tx_macro_queue_work(tx_priv,
						      &tx_priv->tx_hpf_work[decimator].dwork,
						      hpf_delay);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		hpf_cut_off_freq =
			tx_priv->tx_hpf_work[decimator].hpf_cut_off_freq;
		lpass_cdc_tx_macro_update_bits(component,
					       tx_vol_ctl_reg, 0x10, 0x10, &status);
		if (cancel_delayed_work_sync(
		    &tx_priv->tx_hpf_work[decimator].dwork)) {
			if (hpf_cut_off_freq != CF_MIN_3DB_150HZ) {
				lpass_cdc_tx_macro_update_bits(component,
							       dec_cfg_reg,
							       TX_HPF_CUT_OFF_FREQ_MASK,
							       hpf_cut_off_freq << 5, &status);
				if (is_amic_enabled(component, decimator))
					lpass_cdc_tx_macro_update_bits(component,
								       hpf_gate_reg,
								       0x03, 0x02, &status);
				else
					lpass_cdc_tx_macro_update_bits(component,
								       hpf_gate_reg,
								       0x03, 0x03, &status);

				/*
				 * Minimum 1 clk cycle delay is required
				 * as per HW spec
				 */
				usleep_range(1000, 1010);
				lpass_cdc_tx_macro_update_bits(component,
							       hpf_gate_reg,
							       0x03, 0x01, &status);
			}
			lpass_cdc_tx_macro_wake_enable(tx_priv, 0);
		}

		if (snd_soc_component_read(component, adc_mux_reg)
						& SWR_MIC)
			lpass_cdc_tx_macro_update_bits(component,
						       LPASS_CDC_TX_TOP_CSR_SWR_CTRL,
						       0x01, 0x00, &status);

		if (tx_priv->swr_dmic_gain_disable)
			lpass_cdc_tx_macro_update_bits(component, dec_gain_reg,
						       0x01, 0x01, &status);

		break;
	case SND_SOC_DAPM_POST_PMD:
		lpass_cdc_tx_macro_update_bits(component, tx_vol_ctl_reg,
					       0x20, 0x00, &status);
		lpass_cdc_tx_macro_update_bits(component, tx_vol_ctl_reg,
					       0x40, 0x40, &status);
		lpass_cdc_tx_macro_update_bits(component, tx_vol_ctl_reg,
					       0x40, 0x00, &status);
		lpass_cdc_tx_macro_update_bits(component,
					       dec_cfg_reg, 0x06, 0x00, &status);
		lpass_cdc_tx_macro_update_bits(component, tx_vol_ctl_reg,
					       0x10, 0x00, &status);
		lpass_cdc_tx_macro_update_bits(component, tx_fs_reg,
					       0x0F, 0x04, &status);
		if (tx_priv->bcs_enable) {
			lpass_cdc_tx_macro_update_bits(component, dec_cfg_reg,
						       0x01, 0x00, &status);
			lpass_cdc_tx_macro_update_bits(component,
						       LPASS_CDC_TX0_TX_PATH_SEC7, 0x40, 0x00,
						       &status);
			tx_priv->bcs_clk_en = false;
			lpass_cdc_tx_macro_update_bits(component,
						       LPASS_CDC_VA_TOP_CSR_SWR_CTRL, 0x0F,
						       0x00, &status);
		}
		break;
	}
	if (status && (event == SND_SOC_DAPM_PRE_PMU || event == SND_SOC_DAPM_POST_PMU) &&
	    tx_priv->dmic_owned[decimator]) {
		cleanup = lpass_cdc_tx_macro_enable_dmic(w, kcontrol,
							 SND_SOC_DAPM_POST_PMD, adc_mux0_reg);
		if (cleanup)
			dev_err(tx_dev, "TX DMIC rollback failed: %d\n", cleanup);
	}
	return status;
}

static int lpass_cdc_tx_macro_enable_micbias(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	return 0;
}

/* Cutoff frequency for high pass filter */
static const char * const cf_text[] = {
	"CF_NEG_3DB_4HZ", "CF_NEG_3DB_75HZ", "CF_NEG_3DB_150HZ"
};

static SOC_ENUM_SINGLE_DECL(cf_dec0_enum, LPASS_CDC_TX0_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec1_enum, LPASS_CDC_TX1_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec2_enum, LPASS_CDC_TX2_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec3_enum, LPASS_CDC_TX3_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec4_enum, LPASS_CDC_TX4_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec5_enum, LPASS_CDC_TX5_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec6_enum, LPASS_CDC_TX6_TX_PATH_CFG0, 5,
							cf_text);
static SOC_ENUM_SINGLE_DECL(cf_dec7_enum, LPASS_CDC_TX7_TX_PATH_CFG0, 5,
							cf_text);

static int lpass_cdc_tx_macro_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	int tx_fs_rate = -EINVAL, ret;
	struct snd_soc_component *component = dai->component;
	u32 decimator = 0;
	u16 tx_vol_ctl_reg = 0;
	u32 sample_rate = 0;
	u16 tx_fs_reg = 0;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	pr_debug("%s: dai_name = %s DAI-ID %x rate %d num_ch %d\n", __func__,
		 dai->name, dai->id, params_rate(params),
		 params_channels(params));

	if (dai->id <= 0 || dai->id >= LPASS_CDC_TX_MACRO_MAX_DAIS)
		return -EINVAL;
	sample_rate = params_rate(params);
	switch (sample_rate) {
	case 8000:
		tx_fs_rate = 0;
		break;
	case 16000:
		tx_fs_rate = 1;
		break;
	case 32000:
		tx_fs_rate = 3;
		break;
	case 48000:
		tx_fs_rate = 4;
		break;
	case 96000:
		tx_fs_rate = 5;
		break;
	case 192000:
		tx_fs_rate = 6;
		break;
	case 384000:
		tx_fs_rate = 7;
		break;
	default:
		dev_err_ratelimited(component->dev, "%s: Invalid TX sample rate: %d\n",
			__func__, params_rate(params));
		return -EINVAL;
	}
	for_each_set_bit(decimator, &tx_priv->active_ch_mask[dai->id],
			 LPASS_CDC_TX_MACRO_DEC_MAX) {
		if (decimator < NUM_DECIMATORS) {
			tx_fs_reg = LPASS_CDC_TX0_TX_PATH_CTL +
				    LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
			tx_vol_ctl_reg = LPASS_CDC_TX0_TX_PATH_CTL +
						LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
			dev_dbg(component->dev, "%s: set DEC%u rate to %u\n",
				__func__, decimator, sample_rate);
			ret = snd_soc_component_update_bits(component, tx_fs_reg, 0x0F, tx_fs_rate);
			if (ret < 0)
				return ret;
			/* Enable TX PGA Mute */
			ret = snd_soc_component_update_bits(component, tx_vol_ctl_reg, 0x10, 0x10);
			if (ret < 0)
				return ret;
		} else {
			dev_err_ratelimited(component->dev,
				"%s: ERROR: Invalid decimator: %d\n",
				__func__, decimator);
			return -EINVAL;
		}
	}
	return 0;
}

static int lpass_cdc_tx_macro_get_channel_map(const struct snd_soc_dai *dai,
					      unsigned int *tx_num, unsigned int *tx_slot,
					      unsigned int *rx_num, unsigned int *rx_slot)
{
	struct snd_soc_component *component = dai->component;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (!tx_num || !tx_slot)
		return -EINVAL;
	switch (dai->id) {
	case LPASS_CDC_TX_MACRO_AIF1_CAP:
	case LPASS_CDC_TX_MACRO_AIF2_CAP:
	case LPASS_CDC_TX_MACRO_AIF3_CAP:
		*tx_slot = tx_priv->active_ch_mask[dai->id];
		*tx_num = tx_priv->active_ch_cnt[dai->id];
		break;
	default:
		dev_err_ratelimited(tx_dev, "%s: Invalid AIF\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int lpass_cdc_tx_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct lpass_cdc_tx_macro_priv *tx_priv;
	struct device *tx_dev;
	unsigned int decimator, reg, adc;
	unsigned long pending = 0;
	int ret, status = 0;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__) ||
	    dai->id <= 0 || dai->id >= LPASS_CDC_TX_MACRO_MAX_DAIS)
		return -EINVAL;
	cancel_delayed_work_sync(&tx_priv->tx_dec_unmute_work[dai->id].dwork);
	mutex_lock(&tx_priv->stream_lock);
	tx_priv->stream_pending[dai->id] = 0;
	if (mute) {
		for_each_set_bit(decimator, &tx_priv->stream_owned[dai->id], NUM_DECIMATORS) {
			reg = LPASS_CDC_TX0_TX_PATH_CTL +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
			if (tx_priv->dec_ref_cnt[decimator] == 1) {
				ret = snd_soc_component_update_bits(component, reg, 0x10, 0x10);
				if (ret < 0) {
					status = ret;
					continue;
				}
				clear_bit(decimator, &tx_priv->hs_muted_mask);
			}
			tx_priv->dec_ref_cnt[decimator]--;
			clear_bit(decimator, &tx_priv->stream_owned[dai->id]);
		}
	} else {
		for_each_set_bit(decimator, &tx_priv->active_ch_mask[dai->id], NUM_DECIMATORS) {
			if (test_bit(decimator, &tx_priv->stream_owned[dai->id]))
				continue;
			adc = LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG1 +
				LPASS_CDC_TX_MACRO_ADC_MUX_CFG_OFFSET * decimator;
			if ((snd_soc_component_read(component, adc) & 0x3) &&
			    !tx_priv->swr_dmic_enable)
				continue;
			reg = LPASS_CDC_TX0_TX_PATH_CTL +
				LPASS_CDC_TX_MACRO_TX_PATH_OFFSET * decimator;
			if (!is_msm_dmic_enabled(component, decimator)) {
				ret = snd_soc_component_update_bits(component, reg, 0x40, 0x40);
				usleep_range(2000, 2100);
				if (ret >= 0)
					ret = snd_soc_component_update_bits(component,
									    reg, 0x40, 0);
				if (ret < 0) {
					status = ret;
					continue;
				}
			}
			set_bit(decimator, &pending);
		}
		tx_priv->stream_pending[dai->id] = pending;
		tx_priv->tx_dec_unmute_work[dai->id].dai_id = dai->id;
		if (pending)
			queue_delayed_work(system_freezable_wq,
					   &tx_priv->tx_dec_unmute_work[dai->id].dwork,
				msecs_to_jiffies(LPASS_CDC_TX_MACRO_DEC_UNMUTE_DELAY_MS));
	}
	mutex_unlock(&tx_priv->stream_lock);
	return status;
}

static struct snd_soc_dai_ops lpass_cdc_tx_macro_dai_ops = {
	.hw_params = lpass_cdc_tx_macro_hw_params,
	.get_channel_map = lpass_cdc_tx_macro_get_channel_map,
	.mute_stream = lpass_cdc_tx_mute_stream,
};

static struct snd_soc_dai_driver lpass_cdc_tx_macro_dai[] = {
	{
		.name = "tx_macro_tx1",
		.id = LPASS_CDC_TX_MACRO_AIF1_CAP,
		.capture = {
			.stream_name = "TX_AIF1 Capture",
			.rates = LPASS_CDC_TX_MACRO_RATES,
			.formats = LPASS_CDC_TX_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &lpass_cdc_tx_macro_dai_ops,
	},
	{
		.name = "tx_macro_tx2",
		.id = LPASS_CDC_TX_MACRO_AIF2_CAP,
		.capture = {
			.stream_name = "TX_AIF2 Capture",
			.rates = LPASS_CDC_TX_MACRO_RATES,
			.formats = LPASS_CDC_TX_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &lpass_cdc_tx_macro_dai_ops,
	},
	{
		.name = "tx_macro_tx3",
		.id = LPASS_CDC_TX_MACRO_AIF3_CAP,
		.capture = {
			.stream_name = "TX_AIF3 Capture",
			.rates = LPASS_CDC_TX_MACRO_RATES,
			.formats = LPASS_CDC_TX_MACRO_FORMATS,
			.rate_max = 192000,
			.rate_min = 8000,
			.channels_min = 1,
			.channels_max = 8,
		},
		.ops = &lpass_cdc_tx_macro_dai_ops,
	},
};

#define STRING(name) #name
#define LPASS_CDC_TX_MACRO_DAPM_ENUM(name, reg, offset, text) \
static SOC_ENUM_SINGLE_DECL(name##_enum, reg, offset, text); \
static const struct snd_kcontrol_new name##_mux = \
		SOC_DAPM_ENUM(STRING(name), name##_enum)

#define LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(name, reg, offset, text, getname, putname) \
static SOC_ENUM_SINGLE_DECL(name##_enum, reg, offset, text); \
static const struct snd_kcontrol_new name##_mux = \
		SOC_DAPM_ENUM_EXT(STRING(name), name##_enum, getname, putname)

#define LPASS_CDC_TX_MACRO_DAPM_MUX(name, shift, kctl) \
		SND_SOC_DAPM_MUX(name, SND_SOC_NOPM, shift, 0, &kctl##_mux)

static const char * const adc_mux_text[] = {
	"MSM_DMIC", "SWR_MIC", "ANC_FB_TUNE1"
};

LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec0, LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec1, LPASS_CDC_TX_INP_MUX_ADC_MUX1_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec2, LPASS_CDC_TX_INP_MUX_ADC_MUX2_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec3, LPASS_CDC_TX_INP_MUX_ADC_MUX3_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec4, LPASS_CDC_TX_INP_MUX_ADC_MUX4_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec5, LPASS_CDC_TX_INP_MUX_ADC_MUX5_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec6, LPASS_CDC_TX_INP_MUX_ADC_MUX6_CFG1,
		   0, adc_mux_text);
LPASS_CDC_TX_MACRO_DAPM_ENUM(tx_dec7, LPASS_CDC_TX_INP_MUX_ADC_MUX7_CFG1,
		   0, adc_mux_text);


static const char * const dmic_mux_text[] = {
	"ZERO", "DMIC0", "DMIC1", "DMIC2", "DMIC3",
	"DMIC4", "DMIC5", "DMIC6", "DMIC7"
};

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic0, LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic1, LPASS_CDC_TX_INP_MUX_ADC_MUX1_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic2, LPASS_CDC_TX_INP_MUX_ADC_MUX2_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic3, LPASS_CDC_TX_INP_MUX_ADC_MUX3_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic4, LPASS_CDC_TX_INP_MUX_ADC_MUX4_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic5, LPASS_CDC_TX_INP_MUX_ADC_MUX5_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic6, LPASS_CDC_TX_INP_MUX_ADC_MUX6_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_dmic7, LPASS_CDC_TX_INP_MUX_ADC_MUX7_CFG0,
			4, dmic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

static const char * const smic_mux_text[] = {
	"ZERO", "SWR_MIC0", "SWR_MIC1", "SWR_MIC2", "SWR_MIC3",
	"SWR_MIC4", "SWR_MIC5", "SWR_MIC6", "SWR_MIC7",
	"SWR_MIC8", "SWR_MIC9", "SWR_MIC10", "SWR_MIC11"
};

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic0, LPASS_CDC_TX_INP_MUX_ADC_MUX0_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic1, LPASS_CDC_TX_INP_MUX_ADC_MUX1_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic2, LPASS_CDC_TX_INP_MUX_ADC_MUX2_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic3, LPASS_CDC_TX_INP_MUX_ADC_MUX3_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic4, LPASS_CDC_TX_INP_MUX_ADC_MUX4_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic5, LPASS_CDC_TX_INP_MUX_ADC_MUX5_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic6, LPASS_CDC_TX_INP_MUX_ADC_MUX6_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

LPASS_CDC_TX_MACRO_DAPM_ENUM_EXT(tx_smic7, LPASS_CDC_TX_INP_MUX_ADC_MUX7_CFG0,
			0, smic_mux_text, snd_soc_dapm_get_enum_double,
			lpass_cdc_tx_macro_put_dec_enum);

static const char * const dec_mode_mux_text[] = {
	"ADC_DEFAULT", "ADC_LOW_PWR", "ADC_HIGH_PERF",
};

static const struct soc_enum dec_mode_mux_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(dec_mode_mux_text),
			    dec_mode_mux_text);

static const char * const bcs_ch_enum_text[] = {
	"CH0", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8", "CH9",
	"CH10", "CH11",
};

static const struct soc_enum bcs_ch_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(bcs_ch_enum_text),
			    bcs_ch_enum_text);

static const struct snd_kcontrol_new tx_aif1_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC0, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC1, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC2, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC3, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC4, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC5, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC6, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC7, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
};

static const struct snd_kcontrol_new tx_aif2_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC0, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC1, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC2, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC3, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC4, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC5, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC6, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC7, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
};

static const struct snd_kcontrol_new tx_aif3_cap_mixer[] = {
	SOC_SINGLE_EXT("DEC0", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC0, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC1", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC1, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC2", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC2, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC3", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC3, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC4", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC4, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC5", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC5, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC6", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC6, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
	SOC_SINGLE_EXT("DEC7", SND_SOC_NOPM, LPASS_CDC_TX_MACRO_DEC7, 1, 0,
			lpass_cdc_tx_macro_tx_mixer_get, lpass_cdc_tx_macro_tx_mixer_put),
};

static const struct snd_soc_dapm_widget lpass_cdc_tx_macro_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("TX_AIF1 CAP", "TX_AIF1 Capture", 0,
		SND_SOC_NOPM, LPASS_CDC_TX_MACRO_AIF1_CAP, 0),

	SND_SOC_DAPM_AIF_OUT("TX_AIF2 CAP", "TX_AIF2 Capture", 0,
		SND_SOC_NOPM, LPASS_CDC_TX_MACRO_AIF2_CAP, 0),

	SND_SOC_DAPM_AIF_OUT("TX_AIF3 CAP", "TX_AIF3 Capture", 0,
		SND_SOC_NOPM, LPASS_CDC_TX_MACRO_AIF3_CAP, 0),

	SND_SOC_DAPM_MIXER("TX_AIF1_CAP Mixer", SND_SOC_NOPM,
		LPASS_CDC_TX_MACRO_AIF1_CAP, 0,
		tx_aif1_cap_mixer, ARRAY_SIZE(tx_aif1_cap_mixer)),

	SND_SOC_DAPM_MIXER("TX_AIF2_CAP Mixer", SND_SOC_NOPM,
		LPASS_CDC_TX_MACRO_AIF2_CAP, 0,
		tx_aif2_cap_mixer, ARRAY_SIZE(tx_aif2_cap_mixer)),

	SND_SOC_DAPM_MIXER("TX_AIF3_CAP Mixer", SND_SOC_NOPM,
		LPASS_CDC_TX_MACRO_AIF3_CAP, 0,
		tx_aif3_cap_mixer, ARRAY_SIZE(tx_aif3_cap_mixer)),

	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX0", 0, tx_dmic0),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX1", 0, tx_dmic1),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX2", 0, tx_dmic2),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX3", 0, tx_dmic3),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX4", 0, tx_dmic4),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX5", 0, tx_dmic5),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX6", 0, tx_dmic6),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX DMIC MUX7", 0, tx_dmic7),

	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX0", 0, tx_smic0),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX1", 0, tx_smic1),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX2", 0, tx_smic2),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX3", 0, tx_smic3),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX4", 0, tx_smic4),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX5", 0, tx_smic5),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX6", 0, tx_smic6),
	LPASS_CDC_TX_MACRO_DAPM_MUX("TX SMIC MUX7", 0, tx_smic7),

	SND_SOC_DAPM_SUPPLY("TX MIC BIAS1", SND_SOC_NOPM, 0, 0,
		lpass_cdc_tx_macro_enable_micbias,
		SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_ADC("TX DMIC0", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC1", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC2", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC3", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC4", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC5", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC6", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("TX DMIC7", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_INPUT("TX SWR_INPUT"),

	SND_SOC_DAPM_MUX_E("TX DEC0 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC0, 0,
			   &tx_dec0_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC1 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC1, 0,
			   &tx_dec1_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC2 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC2, 0,
			   &tx_dec2_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC3 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC3, 0,
			   &tx_dec3_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC4 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC4, 0,
			   &tx_dec4_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC5 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC5, 0,
			   &tx_dec5_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC6 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC6, 0,
			   &tx_dec6_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MUX_E("TX DEC7 MUX", SND_SOC_NOPM,
			   LPASS_CDC_TX_MACRO_DEC7, 0,
			   &tx_dec7_mux, lpass_cdc_tx_macro_enable_dec,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("TX_MCLK", 0, SND_SOC_NOPM, 0, 0,
	lpass_cdc_tx_macro_mclk_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route tx_audio_map[] = {
	{"TX_AIF1 CAP", NULL, "TX_MCLK"},
	{"TX_AIF2 CAP", NULL, "TX_MCLK"},
	{"TX_AIF3 CAP", NULL, "TX_MCLK"},

	{"TX_AIF1 CAP", NULL, "TX_AIF1_CAP Mixer"},
	{"TX_AIF2 CAP", NULL, "TX_AIF2_CAP Mixer"},
	{"TX_AIF3 CAP", NULL, "TX_AIF3_CAP Mixer"},

	{"TX_AIF1_CAP Mixer", "DEC0", "TX DEC0 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC1", "TX DEC1 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC2", "TX DEC2 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC3", "TX DEC3 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC4", "TX DEC4 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC5", "TX DEC5 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC6", "TX DEC6 MUX"},
	{"TX_AIF1_CAP Mixer", "DEC7", "TX DEC7 MUX"},

	{"TX_AIF2_CAP Mixer", "DEC0", "TX DEC0 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC1", "TX DEC1 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC2", "TX DEC2 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC3", "TX DEC3 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC4", "TX DEC4 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC5", "TX DEC5 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC6", "TX DEC6 MUX"},
	{"TX_AIF2_CAP Mixer", "DEC7", "TX DEC7 MUX"},

	{"TX_AIF3_CAP Mixer", "DEC0", "TX DEC0 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC1", "TX DEC1 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC2", "TX DEC2 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC3", "TX DEC3 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC4", "TX DEC4 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC5", "TX DEC5 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC6", "TX DEC6 MUX"},
	{"TX_AIF3_CAP Mixer", "DEC7", "TX DEC7 MUX"},

	{"TX DEC0 MUX", NULL, "TX_MCLK"},
	{"TX DEC1 MUX", NULL, "TX_MCLK"},
	{"TX DEC2 MUX", NULL, "TX_MCLK"},
	{"TX DEC3 MUX", NULL, "TX_MCLK"},
	{"TX DEC4 MUX", NULL, "TX_MCLK"},
	{"TX DEC5 MUX", NULL, "TX_MCLK"},
	{"TX DEC6 MUX", NULL, "TX_MCLK"},
	{"TX DEC7 MUX", NULL, "TX_MCLK"},

	{"TX DEC0 MUX", "MSM_DMIC", "TX DMIC MUX0"},
	{"TX DMIC MUX0", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX0", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX0", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX0", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX0", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX0", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX0", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX0", "DMIC7", "TX DMIC7"},

	{"TX DEC0 MUX", "SWR_MIC", "TX SMIC MUX0"},
	{"TX SMIC MUX0", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX0", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC1 MUX", "MSM_DMIC", "TX DMIC MUX1"},
	{"TX DMIC MUX1", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX1", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX1", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX1", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX1", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX1", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX1", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX1", "DMIC7", "TX DMIC7"},

	{"TX DEC1 MUX", "SWR_MIC", "TX SMIC MUX1"},
	{"TX SMIC MUX1", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX1", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC2 MUX", "MSM_DMIC", "TX DMIC MUX2"},
	{"TX DMIC MUX2", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX2", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX2", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX2", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX2", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX2", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX2", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX2", "DMIC7", "TX DMIC7"},

	{"TX DEC2 MUX", "SWR_MIC", "TX SMIC MUX2"},
	{"TX SMIC MUX2", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX2", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC3 MUX", "MSM_DMIC", "TX DMIC MUX3"},
	{"TX DMIC MUX3", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX3", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX3", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX3", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX3", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX3", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX3", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX3", "DMIC7", "TX DMIC7"},

	{"TX DEC3 MUX", "SWR_MIC", "TX SMIC MUX3"},
	{"TX SMIC MUX3", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX3", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC4 MUX", "MSM_DMIC", "TX DMIC MUX4"},
	{"TX DMIC MUX4", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX4", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX4", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX4", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX4", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX4", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX4", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX4", "DMIC7", "TX DMIC7"},

	{"TX DEC4 MUX", "SWR_MIC", "TX SMIC MUX4"},
	{"TX SMIC MUX4", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX4", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC5 MUX", "MSM_DMIC", "TX DMIC MUX5"},
	{"TX DMIC MUX5", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX5", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX5", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX5", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX5", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX5", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX5", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX5", "DMIC7", "TX DMIC7"},

	{"TX DEC5 MUX", "SWR_MIC", "TX SMIC MUX5"},
	{"TX SMIC MUX5", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX5", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC6 MUX", "MSM_DMIC", "TX DMIC MUX6"},
	{"TX DMIC MUX6", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX6", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX6", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX6", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX6", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX6", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX6", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX6", "DMIC7", "TX DMIC7"},

	{"TX DEC6 MUX", "SWR_MIC", "TX SMIC MUX6"},
	{"TX SMIC MUX6", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX6", "SWR_MIC11", "TX SWR_INPUT"},

	{"TX DEC7 MUX", "MSM_DMIC", "TX DMIC MUX7"},
	{"TX DMIC MUX7", "DMIC0", "TX DMIC0"},
	{"TX DMIC MUX7", "DMIC1", "TX DMIC1"},
	{"TX DMIC MUX7", "DMIC2", "TX DMIC2"},
	{"TX DMIC MUX7", "DMIC3", "TX DMIC3"},
	{"TX DMIC MUX7", "DMIC4", "TX DMIC4"},
	{"TX DMIC MUX7", "DMIC5", "TX DMIC5"},
	{"TX DMIC MUX7", "DMIC6", "TX DMIC6"},
	{"TX DMIC MUX7", "DMIC7", "TX DMIC7"},

	{"TX DEC7 MUX", "SWR_MIC", "TX SMIC MUX7"},
	{"TX SMIC MUX7", "SWR_MIC0", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC1", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC2", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC3", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC4", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC5", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC6", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC7", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC8", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC9", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC10", "TX SWR_INPUT"},
	{"TX SMIC MUX7", "SWR_MIC11", "TX SWR_INPUT"},
};

static const struct snd_kcontrol_new lpass_cdc_tx_macro_snd_controls[] = {
	SOC_SINGLE_S8_TLV("TX_DEC0 Volume",
			  LPASS_CDC_TX0_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC1 Volume",
			  LPASS_CDC_TX1_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC2 Volume",
			  LPASS_CDC_TX2_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC3 Volume",
			  LPASS_CDC_TX3_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC4 Volume",
			  LPASS_CDC_TX4_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC5 Volume",
			  LPASS_CDC_TX5_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC6 Volume",
			  LPASS_CDC_TX6_TX_VOL_CTL,
			  -84, 40, digital_gain),
	SOC_SINGLE_S8_TLV("TX_DEC7 Volume",
			  LPASS_CDC_TX7_TX_VOL_CTL,
			  -84, 40, digital_gain),

	SOC_ENUM_EXT("DEC0 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC1 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC2 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC3 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC4 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC5 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC6 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),

	SOC_ENUM_EXT("DEC7 MODE", dec_mode_mux_enum,
			lpass_cdc_tx_macro_dec_mode_get, lpass_cdc_tx_macro_dec_mode_put),
	SOC_ENUM("TX0 HPF cut off", cf_dec0_enum),

	SOC_ENUM("TX1 HPF cut off", cf_dec1_enum),

	SOC_ENUM("TX2 HPF cut off", cf_dec2_enum),

	SOC_ENUM("TX3 HPF cut off", cf_dec3_enum),

	SOC_ENUM("TX4 HPF cut off", cf_dec4_enum),

	SOC_ENUM("TX5 HPF cut off", cf_dec5_enum),

	SOC_ENUM("TX6 HPF cut off", cf_dec6_enum),

	SOC_ENUM("TX7 HPF cut off", cf_dec7_enum),

	SOC_SINGLE_EXT("DEC0_BCS Switch", SND_SOC_NOPM, 0, 1, 0,
		       lpass_cdc_tx_macro_get_bcs, lpass_cdc_tx_macro_set_bcs),

	SOC_SINGLE_EXT("TX_SWR_DMIC Enable", SND_SOC_NOPM, 0, 1, 0,
			lpass_cdc_tx_macro_swr_dmic_get, lpass_cdc_tx_macro_swr_dmic_put),

	SOC_ENUM_EXT("BCS Channel", bcs_ch_enum,
		     lpass_cdc_tx_macro_bcs_ch_get, lpass_cdc_tx_macro_bcs_ch_put),

	SOC_ENUM_EXT("BCS CH_SEL", bcs_ch_sel_mux_enum,
		       lpass_cdc_tx_macro_get_bcs_ch_sel, lpass_cdc_tx_macro_put_bcs_ch_sel),

	SOC_SINGLE_EXT("DEC_GAIN_BYPASS Enable", SND_SOC_NOPM, 0, 1, 0,
		     lpass_cdc_tx_macro_get_dec_gain_bypass, lpass_cdc_tx_macro_put_dec_gain_bypass),
};

static int lpass_cdc_tx_macro_clk_div_get(struct snd_soc_component *component, u32 mic_pair)
{
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	if (mic_pair >= MIC_PAIR_MAX)
		return -EINVAL;

	if (mic_pair >= MIC_PAIR_MAX)
		return -EINVAL;
	return (int)tx_priv->dmic_clk_div[mic_pair];
}

static void lpass_cdc_tx_macro_update_clk_div_factor(u32 div_factor,
				      struct lpass_cdc_tx_macro_priv *tx_priv,
				      u32 mic_pair)
{

	dev_dbg(tx_priv->dev, "%s: div_factor = %u, mic_pair %d\n",
		__func__, div_factor, mic_pair);

	if (mic_pair >= MIC_PAIR_MAX)
		return;

	switch (div_factor) {
	case 2:
		tx_priv->dmic_clk_div[mic_pair] = LPASS_CDC_TX_MACRO_CLK_DIV_2;
		break;
	case 3:
		tx_priv->dmic_clk_div[mic_pair] = LPASS_CDC_TX_MACRO_CLK_DIV_3;
		break;
	case 4:
		tx_priv->dmic_clk_div[mic_pair] = LPASS_CDC_TX_MACRO_CLK_DIV_4;
		break;
	case 6:
		tx_priv->dmic_clk_div[mic_pair] = LPASS_CDC_TX_MACRO_CLK_DIV_6;
		break;
	case 8:
		tx_priv->dmic_clk_div[mic_pair] = LPASS_CDC_TX_MACRO_CLK_DIV_8;
		break;
	case 16:
		tx_priv->dmic_clk_div[mic_pair] = LPASS_CDC_TX_MACRO_CLK_DIV_16;
		break;
	default:
		/* Any other DIV factor is invalid */
		dev_err(tx_priv->dev, "%s: Invalid div factor %d, for mic_pair %d\n",
			 __func__, div_factor, mic_pair);
	}
}

static const struct lpass_cdc_tx_macro_reg_mask_val
				lpass_cdc_tx_macro_reg_init[] = {
	{LPASS_CDC_TX0_TX_PATH_SEC7, 0x3F, 0x0A},
};

static int lpass_cdc_tx_macro_component_cleanup(struct lpass_cdc_tx_macro_priv *tx_priv)
{
	int ret, status = 0, i;

	for (i = 0; i < NUM_DECIMATORS; i++) {
		if (!tx_priv->dmic_owned[i])
			continue;
		ret = lpass_cdc_dmic_clk_enable(tx_priv->component,
						tx_priv->dmic_id[i], DMIC_TX, false);
		if (ret) {
			status = ret;
			dev_err(tx_priv->dev, "TX DMIC %d cleanup failed: %d\n", i, ret);
		} else {
			tx_priv->dmic_owned[i] = false;
		}
	}
	if (tx_priv->dapm_mclk_enable) {
		ret = lpass_cdc_tx_macro_mclk_enable(tx_priv, false);
		if (ret)
			status = ret;
		else
			tx_priv->dapm_mclk_enable = false;
	}
	/* All stream users and workers have left the component at this point. */
	memset(tx_priv->stream_pending, 0, sizeof(tx_priv->stream_pending));
	memset(tx_priv->stream_owned, 0, sizeof(tx_priv->stream_owned));
	memset(tx_priv->dec_ref_cnt, 0, sizeof(tx_priv->dec_ref_cnt));
	tx_priv->hs_muted_mask = 0;
	return status;
}

static int lpass_cdc_tx_macro_init(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm =
			snd_soc_component_get_dapm(component);
	int ret = 0, i = 0;
	struct device *tx_dev = NULL;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	tx_dev = lpass_cdc_get_device_ptr(component->dev, TX_MACRO);
	if (!tx_dev) {
		dev_err(component->dev,
			"%s: null device for macro!\n", __func__);
		return -EINVAL;
	}
	tx_priv = dev_get_drvdata(tx_dev);
	if (!tx_priv) {
		dev_err(component->dev,
			"%s: priv is null for macro!\n", __func__);
		return -EINVAL;
	}
	tx_priv->component = component;
	tx_priv->version = lpass_cdc_get_version(tx_dev);
	ret = snd_soc_dapm_new_controls(dapm, lpass_cdc_tx_macro_dapm_widgets,
				ARRAY_SIZE(lpass_cdc_tx_macro_dapm_widgets));
	if (ret < 0) {
		dev_err(tx_dev, "%s: Failed to add controls\n",
			__func__);
		goto fail;
	}

	ret = snd_soc_dapm_add_routes(dapm, tx_audio_map,
				ARRAY_SIZE(tx_audio_map));
	if (ret < 0) {
		dev_err(tx_dev, "%s: Failed to add routes\n",
			__func__);
		goto fail;
	}

	ret = snd_soc_dapm_new_widgets(dapm->card);
	if (ret < 0) {
		dev_err(tx_dev, "%s: Failed to add widgets\n", __func__);
		goto fail;
	}

	ret = snd_soc_add_component_controls(component,
			lpass_cdc_tx_macro_snd_controls,
			ARRAY_SIZE(lpass_cdc_tx_macro_snd_controls));
	if (ret < 0) {
		dev_err(tx_dev, "%s: Failed to add snd_ctls\n",
			__func__);
		goto fail;
	}

	snd_soc_dapm_ignore_suspend(dapm, "TX_AIF1 Capture");
	snd_soc_dapm_ignore_suspend(dapm, "TX_AIF2 Capture");
	snd_soc_dapm_ignore_suspend(dapm, "TX_AIF3 Capture");
	snd_soc_dapm_ignore_suspend(dapm, "TX SWR_INPUT");
	snd_soc_dapm_sync(dapm);

	for (i = 0; i < ARRAY_SIZE(lpass_cdc_tx_macro_reg_init); i++) {
		ret = snd_soc_component_update_bits(component,
						    lpass_cdc_tx_macro_reg_init[i].reg,
						    lpass_cdc_tx_macro_reg_init[i].mask,
						    lpass_cdc_tx_macro_reg_init[i].val);
		if (ret < 0)
			goto fail;
	}

	mutex_lock(&tx_global_lock);
	if (g_lpass_cdc_tx_priv && g_lpass_cdc_tx_priv != tx_priv) {
		mutex_unlock(&tx_global_lock);
		ret = -EBUSY;
		goto fail;
	}
	tx_priv->hs_closing = false;
	g_lpass_cdc_tx_priv = tx_priv;
	mutex_unlock(&tx_global_lock);
	return 0;
fail:
	lpass_cdc_tx_macro_cancel_work(tx_priv);
	lpass_cdc_tx_macro_component_cleanup(tx_priv);
	tx_priv->component = NULL;
	return ret;
}

static int lpass_cdc_tx_macro_release_clocks(struct lpass_cdc_tx_macro_priv *tx_priv)
{
	int ret, status = 0, i;

	for (i = 0; i < NUM_DECIMATORS; i++) {
		if (!tx_priv->dmic_owned[i])
			continue;
		ret = lpass_cdc_dmic_clk_enable(tx_priv->component,
						tx_priv->dmic_id[i], DMIC_TX, false);
		if (ret)
			status = ret;
		else
			tx_priv->dmic_owned[i] = false;
	}
	while (tx_priv->tx_mclk_users) {
		ret = lpass_cdc_tx_macro_mclk_enable(tx_priv, false);
		if (ret) {
			status = ret;
			break;
		}
	}
	if (tx_priv->failed_clock_owned) {
		ret = lpass_cdc_clk_rsc_request_clock(tx_priv->dev, TX_CORE_CLK,
						      TX_CORE_CLK, false);
		if (ret)
			status = ret;
		else
			tx_priv->failed_clock_owned = false;
	}
	if (status)
		dev_err(tx_priv->dev, "TX cleanup failed %d: clock users %d, rollback %d\n",
			status, tx_priv->tx_mclk_users, tx_priv->failed_clock_owned);
	return status;
}

static void lpass_cdc_tx_macro_disable_pm(struct lpass_cdc_tx_macro_priv *tx_priv)
{
	int ret;

	pm_runtime_disable(tx_priv->dev);
	if (!pm_runtime_status_suspended(tx_priv->dev)) {
		ret = lpass_cdc_runtime_suspend(tx_priv->dev);
		if (!ret)
			pm_runtime_set_suspended(tx_priv->dev);
		else
			dev_err(tx_priv->dev, "final TX suspend failed: %d\n", ret);
	}
}

static void lpass_cdc_tx_macro_close_headset(struct lpass_cdc_tx_macro_priv *tx_priv)
{
	mutex_lock(&tx_global_lock);
	tx_priv->hs_closing = true;
	if (g_lpass_cdc_tx_priv == tx_priv)
		g_lpass_cdc_tx_priv = NULL;
	mutex_unlock(&tx_global_lock);
	wait_event(tx_priv->hs_wait, !atomic_read(&tx_priv->hs_calls));
}

static int lpass_cdc_tx_macro_deinit(struct snd_soc_component *component)
{
	struct device *tx_dev = NULL;
	int ret, cleanup;
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;

	if (!lpass_cdc_tx_macro_get_data(component, &tx_dev, &tx_priv, __func__))
		return -EINVAL;

	lpass_cdc_tx_macro_close_headset(tx_priv);
	lpass_cdc_tx_macro_cancel_work(tx_priv);
	ret = lpass_cdc_tx_macro_component_cleanup(tx_priv);
	if (tx_priv->removing) {
		cleanup = lpass_cdc_tx_macro_release_clocks(tx_priv);
		if (!ret)
			ret = cleanup;
	}
	tx_priv->component = NULL;
	return ret;
}

static void lpass_cdc_tx_macro_init_ops(struct macro_ops *ops,
			       char __iomem *tx_io_base)
{
	memset(ops, 0, sizeof(struct macro_ops));
	ops->init = lpass_cdc_tx_macro_init;
	ops->exit = lpass_cdc_tx_macro_deinit;
	ops->io_base = tx_io_base;
	ops->dai_ptr = lpass_cdc_tx_macro_dai;
	ops->num_dais = ARRAY_SIZE(lpass_cdc_tx_macro_dai);
	ops->event_handler = lpass_cdc_tx_macro_event_handler;
	ops->clk_div_get = lpass_cdc_tx_macro_clk_div_get;
	ops->clk_enable = __lpass_cdc_tx_macro_mclk_enable;
}

static int lpass_cdc_tx_macro_parse_divs(struct lpass_cdc_tx_macro_priv *tx_priv,
					 const char *name)
{
	u32 factors[MIC_PAIR_MAX];
	int i, ret;

	if (!of_property_present(tx_priv->dev->of_node, name))
		return 0;
	if (of_property_count_u32_elems(tx_priv->dev->of_node, name) != MIC_PAIR_MAX)
		return -EINVAL;
	ret = of_property_read_u32_array(tx_priv->dev->of_node, name, factors,
					 MIC_PAIR_MAX);
	if (ret)
		return ret;
	for (i = 0; i < MIC_PAIR_MAX; i++) {
		if (factors[i] != 2 && factors[i] != 3 && factors[i] != 4 &&
		    factors[i] != 6 && factors[i] != 8 && factors[i] != 16)
			return -EINVAL;
	}
	for (i = 0; i < MIC_PAIR_MAX; i++)
		lpass_cdc_tx_macro_update_clk_div_factor(factors[i], tx_priv, i);
	return 0;
}

static int lpass_cdc_tx_macro_probe(struct platform_device *pdev)
{
	struct macro_ops ops = {0};
	struct lpass_cdc_tx_macro_priv *tx_priv = NULL;
	u32 tx_base_addr = 0;
	char __iomem *tx_io_base = NULL;
	int ret = 0, i, dai_idx;

	const char *dmic_clk_div_factor = "qcom,tx-dmic-clk-div-factor";

	if (!lpass_cdc_is_va_macro_registered(&pdev->dev)) {
		dev_err(&pdev->dev,
			"%s: va-macro not registered yet, defer\n", __func__);
		return -EPROBE_DEFER;
	}

	tx_priv = devm_kzalloc(&pdev->dev, sizeof(struct lpass_cdc_tx_macro_priv),
			    GFP_KERNEL);
	if (!tx_priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, tx_priv);

	tx_priv->dev = &pdev->dev;
	ret = of_property_read_u32(pdev->dev.of_node, "reg",
				   &tx_base_addr);
	if (ret) {
		dev_err(&pdev->dev, "%s: could not find %s entry in dt\n",
			__func__, "reg");
		return ret;
	}
	dev_set_drvdata(&pdev->dev, tx_priv);

	tx_io_base = devm_ioremap(&pdev->dev,
				   tx_base_addr, LPASS_CDC_TX_MACRO_MAX_OFFSET);
	if (!tx_io_base) {
		dev_err(&pdev->dev, "%s: ioremap failed\n", __func__);
		return -ENOMEM;
	}
	tx_priv->tx_io_base = tx_io_base;
	tx_priv->swr_dmic_enable = false;
	tx_priv->swr_dmic_gain_disable = false;
	tx_priv->wlock_holders = 0;

	for (i = 0; i < MIC_PAIR_MAX; i++)
		tx_priv->dmic_clk_div[i] = LPASS_CDC_TX_MACRO_CLK_DIV_2;

	ret = lpass_cdc_tx_macro_parse_divs(tx_priv, dmic_clk_div_factor);
	if (ret)
		return ret;

	mutex_init(&tx_priv->mclk_lock);
	mutex_init(&tx_priv->wlock);
	mutex_init(&tx_priv->hs_lock);
	mutex_init(&tx_priv->stream_lock);
	init_waitqueue_head(&tx_priv->hs_wait);
	atomic_set(&tx_priv->hs_calls, 0);
	tx_priv->pre_dev_up = true;
	for (i = 0; i < NUM_DECIMATORS; i++) {
		tx_priv->tx_hpf_work[i].tx_priv = tx_priv;
		tx_priv->tx_hpf_work[i].decimator = i;
		INIT_DELAYED_WORK(&tx_priv->tx_hpf_work[i].dwork,
				  lpass_cdc_tx_macro_tx_hpf_corner_freq_callback);
	}

	for (i = 0; i < NUM_DECIMATORS; i++) {
		tx_priv->tx_mute_dwork[i].tx_priv = tx_priv;
		tx_priv->tx_mute_dwork[i].decimator = i;
		INIT_DELAYED_WORK(&tx_priv->tx_mute_dwork[i].dwork,
				  lpass_cdc_tx_macro_mute_update_callback);
	}
	INIT_DELAYED_WORK(&tx_priv->tx_hs_unmute_dwork, tx_macro_hs_unmute_dwork);

	for (dai_idx = 0; dai_idx < LPASS_CDC_TX_MACRO_MAX_DAIS; ++dai_idx) {
		tx_priv->tx_dec_unmute_work[dai_idx].tx_priv = tx_priv;
		INIT_DELAYED_WORK(&tx_priv->tx_dec_unmute_work[dai_idx].dwork,
				  mute_stream_dec_unmute);
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, AUTO_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_suspend_ignore_children(&pdev->dev, true);
	pm_runtime_enable(&pdev->dev);
	lpass_cdc_tx_macro_init_ops(&ops, tx_io_base);
	ops.clk_id_req = TX_CORE_CLK;
	ops.default_clk_id = TX_CORE_CLK;
	ret = lpass_cdc_register_macro(&pdev->dev, TX_MACRO, &ops);
	if (ret) {
		dev_err(&pdev->dev,
			"%s: register macro failed\n", __func__);
		goto err_reg_macro;
	}


	return 0;
err_reg_macro:
	lpass_cdc_tx_macro_close_headset(tx_priv);
	lpass_cdc_tx_macro_cancel_work(tx_priv);
	lpass_cdc_tx_macro_release_clocks(tx_priv);
	lpass_cdc_tx_macro_disable_pm(tx_priv);
	return ret;
}

static void lpass_cdc_tx_macro_remove(struct platform_device *pdev)
{
	struct lpass_cdc_tx_macro_priv *tx_priv = platform_get_drvdata(pdev);

	tx_priv->removing = true;
	lpass_cdc_tx_macro_close_headset(tx_priv);
	if (!tx_priv->component)
		lpass_cdc_tx_macro_release_clocks(tx_priv);
	lpass_cdc_unregister_macro(&pdev->dev, TX_MACRO);
	lpass_cdc_tx_macro_cancel_work(tx_priv);
	lpass_cdc_tx_macro_disable_pm(tx_priv);
}


static const struct of_device_id lpass_cdc_tx_macro_dt_match[] = {
	{.compatible = "qcom,lpass-cdc-tx-macro"},
	{}
};

static const struct dev_pm_ops lpass_cdc_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		pm_runtime_force_suspend,
		pm_runtime_force_resume
	)
	SET_RUNTIME_PM_OPS(
		lpass_cdc_runtime_suspend,
		lpass_cdc_runtime_resume,
		NULL
	)
};

static struct platform_driver lpass_cdc_tx_macro_driver = {
	.driver = {
		.name = "lpass_cdc_tx_macro",
		.owner = THIS_MODULE,
		.pm = &lpass_cdc_dev_pm_ops,
		.of_match_table = lpass_cdc_tx_macro_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = lpass_cdc_tx_macro_probe,
	.remove = lpass_cdc_tx_macro_remove,
};

module_platform_driver(lpass_cdc_tx_macro_driver);

MODULE_DESCRIPTION("TX macro driver");
MODULE_LICENSE("GPL v2");
