// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <soc/snd_event.h>
#include <linux/pm_runtime.h>
#include <soc/swr-common.h>
#include <dsp/digital-cdc-rsc-mgr.h>
#include "lpass-cdc.h"
#include "internal.h"
#include "lpass-cdc-clk-rsc.h"
#include <linux/qti-regmap-debugfs.h>
#include <linux/proc_fs.h>
#include <typec_analog_acc.h>

#define DRV_NAME "lpass-cdc"

#define LPASS_CDC_VERSION_ENTRY_SIZE 32
#define LPASS_CDC_STRING_LEN 80

#define REGDUMP_PRINT_LEN 8
#define REGDUMP_PRINT_STRIDE 4

static const struct snd_soc_component_driver lpass_cdc;

/* pm runtime auto suspend timer in msecs */
#define LPASS_CDC_AUTO_SUSPEND_DELAY          100 /* delay in msec */

static bool lpass_cdc_is_valid_codec_dev(struct device *dev);

/* Readers may call other CDC helpers; never hold this lock over callbacks.
 * Registration/removal must not run from a callback being drained.
 */
bool lpass_cdc_op_get(struct lpass_cdc_priv *priv, bool component)
{
	bool available;

	if (!priv)
		return false;
	mutex_lock(&priv->op_lock);
	available = !priv->quiescing && (!component || priv->component_ready);
	if (available) {
		atomic_inc(&priv->active_ops);
		if (component)
			atomic_inc(&priv->component_ops);
	}
	mutex_unlock(&priv->op_lock);
	return available;
}

void lpass_cdc_op_put(struct lpass_cdc_priv *priv, bool component)
{
	if (component && atomic_dec_and_test(&priv->component_ops))
		wake_up_all(&priv->op_wait);
	if (atomic_dec_and_test(&priv->active_ops))
		wake_up_all(&priv->op_wait);
}

static void lpass_cdc_quiesce(struct lpass_cdc_priv *priv)
{
	mutex_lock(&priv->op_lock);
	priv->quiescing = true;
	mutex_unlock(&priv->op_lock);
	wait_event(priv->op_wait, !atomic_read(&priv->active_ops));
}

static void lpass_cdc_reopen(struct lpass_cdc_priv *priv)
{
	mutex_lock(&priv->op_lock);
	priv->quiescing = false;
	mutex_unlock(&priv->op_lock);
}

static void lpass_cdc_component_drain(struct lpass_cdc_priv *priv)
{
	mutex_lock(&priv->op_lock);
	priv->component_ready = false;
	mutex_unlock(&priv->op_lock);
	wait_event(priv->op_wait, !atomic_read(&priv->component_ops));
}

static void lpass_cdc_unregister_component(struct lpass_cdc_priv *priv)
{
	lpass_cdc_component_drain(priv);
	if (priv->component_registered) {
		snd_soc_unregister_component(priv->dev);
		priv->component_registered = false;
	}
	kfree(priv->lpass_cdc_dais);
	priv->lpass_cdc_dais = NULL;
}

static int lpass_cdc_set_port_map_active(struct snd_soc_component *component,
					 u32 size, void *data)
{
	struct lpass_cdc_priv *priv = NULL;
	struct swr_mstr_port_map *map = NULL;
	u32 idx;
	int ret;

	if (!component || (size == 0) || !data)
		return -EINVAL;

	priv = snd_soc_component_get_drvdata(component);
	if (!priv)
		return -EINVAL;

	if (!lpass_cdc_is_valid_codec_dev(priv->dev)) {
		dev_err_ratelimited(priv->dev, "%s: invalid codec\n", __func__);
		return -EINVAL;
	}
	map = (struct swr_mstr_port_map *)data;

	for (idx = 0; idx < size; idx++)
		if (map[idx].id >= MAX_MACRO)
			return -EINVAL;
	for (idx = 0; idx < size; idx++) {
		if (priv->macro_params[map->id].set_port_map) {
			ret = priv->macro_params[map->id].set_port_map(component,
						map->uc,
						SWR_MSTR_PORT_LEN,
						map->swr_port_params);
			if (ret)
				return ret;
		}
		map += 1;
	}

	return 0;
}

int lpass_cdc_set_port_map(struct snd_soc_component *component,
			   u32 size, void *data)
{
	struct lpass_cdc_priv *priv = component ? snd_soc_component_get_drvdata(component) : NULL;
	int ret;

	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	ret = lpass_cdc_set_port_map_active(component, size, data);
					    lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_set_port_map);

static void lpass_cdc_ahb_write_device(char __iomem *io_base,
				    u16 reg, u8 value)
{
	u32 temp = (u32)(value) & 0x000000FF;

	iowrite32(temp, io_base + reg);
}

static void lpass_cdc_ahb_read_device(char __iomem *io_base,
				   u16 reg, u8 *value)
{
	u32 temp;

	temp = ioread32(io_base + reg);
	*value = (u8)temp;
}

static int __lpass_cdc_reg_read(struct lpass_cdc_priv *priv,
			     u16 macro_id, u16 reg, u8 *val)
{
	int ret = 0;

	mutex_lock(&priv->clk_lock);
	if (!priv->dev_up) {
		dev_dbg_ratelimited(priv->dev,
			"%s: SSR in progress, exit\n", __func__);
		ret = -EINVAL;
		goto ssr_err;
	}

	if (priv->macro_params[VA_MACRO].dev) {
		ret = pm_runtime_resume_and_get(priv->macro_params[VA_MACRO].dev);
		if (ret < 0)
			goto ssr_err;
		mutex_lock(&priv->vote_lock);
		if (((priv->lpass_core_hw_vote && !priv->core_hw_vote_count) ||
			(priv->lpass_audio_hw_vote && !priv->core_audio_vote_count))) {
			ret = -ENOTSYNC;
			goto vote_err;
		}
	}
	lpass_cdc_ahb_read_device(
		priv->macro_params[macro_id].io_base, reg, val);

vote_err:
	if (priv->macro_params[VA_MACRO].dev) {
		mutex_unlock(&priv->vote_lock);
		pm_runtime_mark_last_busy(priv->macro_params[VA_MACRO].dev);
		pm_runtime_put_autosuspend(priv->macro_params[VA_MACRO].dev);
	}
ssr_err:
	mutex_unlock(&priv->clk_lock);
	return ret;
}

static int __lpass_cdc_reg_write(struct lpass_cdc_priv *priv,
			      u16 macro_id, u16 reg, u8 val)
{
	int ret = 0;

	mutex_lock(&priv->clk_lock);
	if (!priv->dev_up) {
		dev_dbg_ratelimited(priv->dev,
			"%s: SSR in progress, exit\n", __func__);
		ret = -EINVAL;
		goto ssr_err;
	}
	if (priv->macro_params[VA_MACRO].dev) {
		ret = pm_runtime_resume_and_get(priv->macro_params[VA_MACRO].dev);
		if (ret < 0)
			goto ssr_err;
		mutex_lock(&priv->vote_lock);
		if (((priv->lpass_core_hw_vote && !priv->core_hw_vote_count) ||
			(priv->lpass_audio_hw_vote && !priv->core_audio_vote_count))) {
			ret = -ENOTSYNC;
			goto vote_err;
		}
	}
	lpass_cdc_ahb_write_device(
		priv->macro_params[macro_id].io_base, reg, val);

vote_err:
	if (priv->macro_params[VA_MACRO].dev) {
		mutex_unlock(&priv->vote_lock);
		pm_runtime_mark_last_busy(priv->macro_params[VA_MACRO].dev);
		pm_runtime_put_autosuspend(priv->macro_params[VA_MACRO].dev);
	}
ssr_err:
	mutex_unlock(&priv->clk_lock);
	return ret;
}

static int lpass_cdc_update_wcd_event_active(void *handle, u16 event, u32 data)
{
	struct lpass_cdc_priv *priv = (struct lpass_cdc_priv *)handle;

	if (!priv) {
		pr_err_ratelimited("%s:Invalid lpass_cdc priv handle\n", __func__);
		return -EINVAL;
	}

	switch (event) {
	case WCD_LPASS_CDC_EVT_RX_MUTE:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_RX_MUTE, data);
		break;
	case WCD_LPASS_CDC_EVT_IMPED_TRUE:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_IMPED_TRUE, data);
		break;
	case WCD_LPASS_CDC_EVT_IMPED_FALSE:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_IMPED_FALSE, data);
		break;
	case WCD_LPASS_CDC_EVT_RX_COMPANDER_SOFT_RST:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_RX_COMPANDER_SOFT_RST, data);
		break;
	case WCD_LPASS_CDC_EVT_BCS_CLK_OFF:
		if (priv->macro_params[TX_MACRO].event_handler)
			return priv->macro_params[TX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_BCS_CLK_OFF, data);
		break;
	case WCD_LPASS_CDC_EVT_RX_PA_GAIN_UPDATE:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_RX_PA_GAIN_UPDATE,
				data);
		break;
	case WCD_LPASS_CDC_EVT_HPHL_HD2_ENABLE:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_HPHL_HD2_ENABLE, data);
		break;
	case WCD_LPASS_CDC_EVT_HPHR_HD2_ENABLE:
		if (priv->macro_params[RX_MACRO].event_handler)
			return priv->macro_params[RX_MACRO].event_handler(priv->component,
				LPASS_CDC_MACRO_EVT_HPHR_HD2_ENABLE, data);
		break;
	default:
		dev_err_ratelimited(priv->dev, "%s: Invalid event %d trigger from wcd\n",
			__func__, event);
		return -EINVAL;
	}
	return 0;
}

static int lpass_cdc_update_wcd_event(void *handle, u16 event, u32 data)
{
	struct lpass_cdc_priv *priv = handle;

	int ret;

	if (!lpass_cdc_op_get(priv, true))
		return -ENODEV;
	ret = lpass_cdc_update_wcd_event_active(handle, event, data);
	lpass_cdc_op_put(priv, true);
	return ret;
}

static int lpass_cdc_register_notifier(void *handle,
					struct notifier_block *nblock,
					bool enable)
{
	struct lpass_cdc_priv *priv = (struct lpass_cdc_priv *)handle;

	if (!priv || !nblock) {
		pr_err_ratelimited("%s: lpass_cdc priv or nblock is null\n", __func__);
		return -EINVAL;
	}
	if (enable)
		return blocking_notifier_chain_register(&priv->notifier,
							nblock);

	return blocking_notifier_chain_unregister(&priv->notifier,
						  nblock);
}

static void lpass_cdc_notifier_call(struct lpass_cdc_priv *priv,
				     u32 data)
{
	if (!lpass_cdc_op_get(priv, false))
		return;
	blocking_notifier_call_chain(&priv->notifier, data, priv->wcd_dev);
	lpass_cdc_op_put(priv, false);
}

static bool lpass_cdc_is_valid_child_dev(struct device *dev)
{
	if (dev && dev->parent &&
	    of_device_is_compatible(dev->parent->of_node, "qcom,lpass-cdc"))
		return true;

	return false;
}

static bool lpass_cdc_is_valid_codec_dev(struct device *dev)
{
	if (dev && of_device_is_compatible(dev->of_node, "qcom,lpass-cdc"))
		return true;

	return false;
}

/**
 * lpass_cdc_clear_amic_tx_hold - clears AMIC register on analog codec
 *
 * @dev: lpass_cdc device ptr.
 *
 */
void lpass_cdc_clear_amic_tx_hold(struct device *dev, u16 adc_n)
{
	struct lpass_cdc_priv *priv;
	u16 event;
	u16 amic = 0;

	if (!dev) {
		pr_err("%s: dev is null\n", __func__);
		return;
	}

	if (!lpass_cdc_is_valid_codec_dev(dev)) {
		pr_err("%s: invalid codec\n", __func__);
		return;
	}
	priv = dev_get_drvdata(dev);
	if (!priv) {
		dev_err(dev, "%s: priv is null\n", __func__);
		return;
	}
	event = LPASS_CDC_WCD_EVT_TX_CH_HOLD_CLEAR;
	if (adc_n == LPASS_CDC_ADC0)
		amic = 0x1;
	else if (adc_n == LPASS_CDC_ADC1)
		amic = 0x2;
	else if (adc_n == LPASS_CDC_ADC2)
		amic = 0x2;
	else if (adc_n == LPASS_CDC_ADC3)
		amic = 0x3;
	else
		return;

	lpass_cdc_notifier_call(priv, (amic << 0x10 | event));
}
EXPORT_SYMBOL(lpass_cdc_clear_amic_tx_hold);

/**
 * lpass_cdc_get_device_ptr - Get child or macro device ptr
 *
 * @dev: lpass_cdc device ptr.
 * @macro_id: ID of macro calling this API.
 *
 * Returns a borrowed pointer. The caller must serialize its use against
 * provider removal; a device reference alone does not retain driver devres.
 * Internal clock operations use the parent operation drain instead.
 * Returns dev ptr on success or NULL on error.
 */
struct device *lpass_cdc_get_device_ptr(struct device *dev, u16 macro_id)
{
	struct lpass_cdc_priv *priv;

	if (!dev) {
		pr_err_ratelimited("%s: dev is null\n", __func__);
		return NULL;
	}

	if (!lpass_cdc_is_valid_codec_dev(dev)) {
		pr_err_ratelimited("%s: invalid codec\n", __func__);
		return NULL;
	}
	priv = dev_get_drvdata(dev);
	if (!priv || (macro_id >= MAX_MACRO)) {
		dev_err_ratelimited(dev, "%s: priv is null or invalid macro\n", __func__);
		return NULL;
	}

	return READ_ONCE(priv->macro_params[macro_id].dev);
}
EXPORT_SYMBOL(lpass_cdc_get_device_ptr);

/**
 * lpass_cdc_get_rsc_clk_device_ptr - Get rsc clk device ptr
 *
 * @dev: lpass_cdc device ptr.
 *
 * Returns a borrowed pointer. The caller must serialize its use against
 * provider removal; a device reference alone does not retain driver devres.
 * Internal clock operations use the parent operation drain instead.
 * Returns dev ptr on success or NULL on error.
 */
struct device *lpass_cdc_get_rsc_clk_device_ptr(struct device *dev)
{
	struct lpass_cdc_priv *priv;

	if (!dev) {
		pr_err_ratelimited("%s: dev is null\n", __func__);
		return NULL;
	}

	if (!lpass_cdc_is_valid_codec_dev(dev)) {
		pr_err_ratelimited("%s: invalid codec\n", __func__);
		return NULL;
	}
	priv = dev_get_drvdata(dev);
	if (!priv) {
		dev_err_ratelimited(dev, "%s: priv is null\n", __func__);
		return NULL;
	}

	return READ_ONCE(priv->clk_dev);
}
EXPORT_SYMBOL(lpass_cdc_get_rsc_clk_device_ptr);

static int lpass_cdc_copy_dais_from_macro(struct lpass_cdc_priv *priv)
{
	struct snd_soc_dai_driver *dais, *next;
	int i;

	dais = kcalloc(priv->num_dais, sizeof(*dais), GFP_KERNEL);
	if (!dais && priv->num_dais)
		return -ENOMEM;
	next = dais;
	for (i = 0; i < MAX_MACRO; i++) {
		if (!priv->macro_params[i].num_dais)
			continue;
		memcpy(next, priv->macro_params[i].dai_ptr,
		       priv->macro_params[i].num_dais * sizeof(*next));
		next += priv->macro_params[i].num_dais;
	}
	kfree(priv->lpass_cdc_dais);
	priv->lpass_cdc_dais = dais;
	return 0;
}

/**
 * lpass_cdc_register_res_clk - Registers rsc clk driver to lpass_cdc
 *
 * @dev: rsc clk device ptr.
 * @rsc_clk_cb: event handler callback for notifications like SSR
 *
 * Returns 0 on success or -EINVAL on error.
 */
int lpass_cdc_register_res_clk(struct device *dev, rsc_clk_cb_t rsc_clk_cb)
{
	struct lpass_cdc_priv *priv;

	if (!dev || !rsc_clk_cb) {
		pr_err_ratelimited("%s: dev or rsc_clk_cb is null\n", __func__);
		return -EINVAL;
	}
	if (!lpass_cdc_is_valid_child_dev(dev)) {
		dev_err_ratelimited(dev, "%s: child device :%pK not added yet\n",
			__func__, dev);
		return -EINVAL;
	}
	priv = dev_get_drvdata(dev->parent);
	if (!priv) {
		dev_err_ratelimited(dev, "%s: priv is null\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&priv->macro_lock);
	if (priv->removing || priv->clk_dev) {
		mutex_unlock(&priv->macro_lock);
		return -EBUSY;
	}
	lpass_cdc_quiesce(priv);
	priv->clk_dev = dev;
	priv->rsc_clk_cb = rsc_clk_cb;
	lpass_cdc_reopen(priv);
	mutex_unlock(&priv->macro_lock);

	return 0;
}
EXPORT_SYMBOL(lpass_cdc_register_res_clk);

/**
 * lpass_cdc_unregister_res_clk - Unregisters rsc clk driver from lpass_cdc
 *
 * @dev: resource clk device ptr.
 */
void lpass_cdc_unregister_res_clk(struct device *dev)
{
	struct lpass_cdc_priv *priv;

	if (!dev) {
		pr_err_ratelimited("%s: dev is NULL\n", __func__);
		return;
	}
	if (!lpass_cdc_is_valid_child_dev(dev)) {
		dev_err_ratelimited(dev, "%s: child device :%pK not added\n",
			__func__, dev);
		return;
	}
	priv = dev_get_drvdata(dev->parent);
	if (!priv) {
		dev_err_ratelimited(dev, "%s: priv is null\n", __func__);
		return;
	}

	mutex_lock(&priv->macro_lock);
	if (priv->clk_dev == dev) {
		lpass_cdc_unregister_component(priv);
		lpass_cdc_quiesce(priv);
		priv->clk_dev = NULL;
		priv->rsc_clk_cb = NULL;
		lpass_cdc_reopen(priv);
	}
	mutex_unlock(&priv->macro_lock);
}
EXPORT_SYMBOL(lpass_cdc_unregister_res_clk);

static int lpass_cdc_dmic_clk_div_get(struct snd_soc_component *component,
				      u32 mode, u32 mic_pair)
{
	struct lpass_cdc_priv *priv = snd_soc_component_get_drvdata(component);
	int macro = mode ? VA_MACRO : TX_MACRO;

	if (priv->macro_params[macro].clk_div_get)
		return priv->macro_params[macro].clk_div_get(component, mic_pair);
	return 1;
}

static int lpass_cdc_dmic_clk_enable_active(struct snd_soc_component *component,
					    u32 dmic, u32 tx_mode, bool enable)
{
	struct lpass_cdc_priv *priv = snd_soc_component_get_drvdata(component);
	s32 *tx[] = { &priv->dmic_0_1_tx_clk_cnt, &priv->dmic_2_3_tx_clk_cnt,
		     &priv->dmic_4_5_tx_clk_cnt, &priv->dmic_6_7_tx_clk_cnt };
	s32 *va[] = { &priv->dmic_0_1_va_clk_cnt, &priv->dmic_2_3_va_clk_cnt,
		     &priv->dmic_4_5_va_clk_cnt, &priv->dmic_6_7_va_clk_cnt };
	u8 *div[] = { &priv->dmic_0_1_clk_div, &priv->dmic_2_3_clk_div,
		      &priv->dmic_4_5_clk_div, &priv->dmic_6_7_clk_div };
	const u16 regs[] = { LPASS_CDC_VA_TOP_CSR_DMIC0_CTL,
		LPASS_CDC_VA_TOP_CSR_DMIC1_CTL, LPASS_CDC_VA_TOP_CSR_DMIC2_CTL,
		LPASS_CDC_VA_TOP_CSR_DMIC3_CTL };
	u16 cfg = LPASS_CDC_VA_TOP_CSR_DMIC_CFG, reg;
	unsigned int old_cfg, old_ctl, pair, mask;
	s32 *count, total;
	int next_div, ret;

	if (dmic > 7 || tx_mode > 1)
		return -EINVAL;
	pair = dmic / 2;
	/* A macro callback can itself call CDC helpers. */
	next_div = lpass_cdc_dmic_clk_div_get(component,
					      enable ? tx_mode : !tx_mode, pair);
	if (next_div < 0)
		return next_div;
	if (next_div > 7)
		return -EINVAL;
	reg = regs[pair];
	mask = BIT(pair);
	count = tx_mode ? va[pair] : tx[pair];
	mutex_lock(&priv->dmic_lock);
	if (*va[pair] > INT_MAX - *tx[pair]) {
		ret = -EOVERFLOW;
		goto out;
	}
	total = *va[pair] + *tx[pair];
	if ((enable && total == INT_MAX) || (!enable && !*count)) {
		ret = enable ? -EOVERFLOW : -EINVAL;
		goto out;
	}
	if (enable && total && *div[pair] < next_div)
		next_div = *div[pair];
	if (!enable) {
		if (total == 1)
			next_div = 0;
		else if (*count > 1)
			next_div = *div[pair];
	}
	ret = regmap_read(priv->regmap, cfg, &old_cfg);
	if (ret)
		goto out;
	ret = regmap_read(priv->regmap, reg, &old_ctl);
	if (ret)
		goto out;
	if (enable && !total) {
		ret = regmap_update_bits(priv->regmap, cfg, 0x80, 0);
		if (!ret)
			ret = regmap_update_bits(priv->regmap, reg, 0x0e, next_div << 1);
		if (!ret)
			ret = regmap_update_bits(priv->regmap, reg, 1, 1);
	} else if (!enable && total == 1) {
		ret = regmap_update_bits(priv->regmap, reg, 1, 0);
		if (!ret)
			ret = regmap_update_bits(priv->regmap, reg, 0x0e, 0);
	} else if (*div[pair] != next_div) {
		ret = regmap_update_bits(priv->regmap, cfg, mask, mask);
		if (!ret)
			ret = regmap_update_bits(priv->regmap, reg, 0x0e, next_div << 1);
		if (!ret)
			ret = regmap_update_bits(priv->regmap, cfg, mask, 0);
	}
	if (ret) {
		if (regmap_update_bits(priv->regmap, reg, 0x0f, old_ctl))
			dev_err_ratelimited(priv->dev, "DMIC control rollback failed\n");
		if (regmap_update_bits(priv->regmap, cfg, mask | 0x80, old_cfg))
			dev_err_ratelimited(priv->dev, "DMIC config rollback failed\n");
		goto out;
	}
	*count += enable ? 1 : -1;
	*div[pair] = next_div;
out:
	mutex_unlock(&priv->dmic_lock);
	return ret;
}

int lpass_cdc_dmic_clk_enable(struct snd_soc_component *component,
			      u32 dmic, u32 tx_mode, bool enable)
{
	struct lpass_cdc_priv *priv = component ? snd_soc_component_get_drvdata(component) : NULL;
	int ret;

	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	ret = lpass_cdc_dmic_clk_enable_active(component, dmic, tx_mode, enable);
	lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_dmic_clk_enable);

bool lpass_cdc_is_va_macro_registered(struct device *dev)
{
	struct lpass_cdc_priv *priv;

	if (!dev) {
		pr_err_ratelimited("%s: dev is null\n", __func__);
		return false;
	}
	if (!lpass_cdc_is_valid_child_dev(dev)) {
		dev_err_ratelimited(dev, "%s: child device calling is not added yet\n",
			__func__);
		return false;
	}
	priv = dev_get_drvdata(dev->parent);
	if (!priv) {
		dev_err_ratelimited(dev, "%s: priv is null\n", __func__);
		return false;
	}
	return READ_ONCE(priv->macros_supported[VA_MACRO]);
}
EXPORT_SYMBOL(lpass_cdc_is_va_macro_registered);

/**
 * lpass_cdc_register_macro - Registers macro to lpass_cdc
 *
 * @dev: macro device ptr.
 * @macro_id: ID of macro calling this API.
 * @ops: macro params to register.
 *
 * Returns 0 on success or -EINVAL on error.
 */
int lpass_cdc_register_macro(struct device *dev, u16 macro_id,
			   struct macro_ops *ops)
{
	struct lpass_cdc_priv *priv;
	int ret = 0;

	if (!lpass_cdc_is_valid_child_dev(dev) || !ops || macro_id >= MAX_MACRO ||
	    (ops->num_dais && !ops->dai_ptr) || !ops->io_base)
		return -EINVAL;
	priv = dev_get_drvdata(dev->parent);
	if (!priv)
		return -ENODEV;
	mutex_lock(&priv->macro_lock);
	if (priv->removing || priv->macros_supported[macro_id]) {
		ret = -EBUSY;
		goto unlock;
	}
	if (priv->num_macros_registered >= priv->num_macros ||
	    ops->num_dais > U16_MAX - priv->num_dais) {
		ret = -EOVERFLOW;
		goto unlock;
	}
	lpass_cdc_quiesce(priv);
	priv->macro_params[macro_id] = *ops;
	priv->macro_params[macro_id].dev = dev;
	priv->num_dais += ops->num_dais;
	priv->num_macros_registered++;
	priv->macros_supported[macro_id] = true;
	priv->current_mclk_mux_macro[macro_id] = TX_MACRO;
	lpass_cdc_reopen(priv);

	if (priv->num_macros_registered != priv->num_macros)
		goto unlock;
	ret = lpass_cdc_copy_dais_from_macro(priv);
	if (ret)
		goto rollback;
	if (!priv->macros_supported[TX_MACRO]) {
		priv->current_mclk_mux_macro[WSA_MACRO] = WSA_MACRO;
		priv->current_mclk_mux_macro[VA_MACRO] = VA_MACRO;
	}
	ret = snd_soc_register_component(priv->dev, &lpass_cdc,
					 priv->lpass_cdc_dais, priv->num_dais);
	if (!ret) {
		priv->component_registered = true;
		goto unlock;
	}
rollback:
	lpass_cdc_quiesce(priv);
	priv->num_dais -= ops->num_dais;
	priv->num_macros_registered--;
	priv->macros_supported[macro_id] = false;
	memset(&priv->macro_params[macro_id], 0, sizeof(*ops));
	kfree(priv->lpass_cdc_dais);
	priv->lpass_cdc_dais = NULL;
	lpass_cdc_reopen(priv);
unlock:
	mutex_unlock(&priv->macro_lock);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_register_macro);

/**
 * lpass_cdc_unregister_macro - De-Register macro from lpass_cdc
 *
 * @dev: macro device ptr.
 * @macro_id: ID of macro calling this API.
 *
 */
void lpass_cdc_unregister_macro(struct device *dev, u16 macro_id)
{
	struct lpass_cdc_priv *priv;

	if (!lpass_cdc_is_valid_child_dev(dev) || macro_id >= MAX_MACRO)
		return;
	priv = dev_get_drvdata(dev->parent);
	if (!priv)
		return;
	mutex_lock(&priv->macro_lock);
	if (priv->macro_params[macro_id].dev != dev)
		goto unlock;
	/* Component and macro exits still need every callback and clock. */
	lpass_cdc_unregister_component(priv);
	lpass_cdc_quiesce(priv);
	priv->num_dais -= priv->macro_params[macro_id].num_dais;
	priv->num_macros_registered--;
	priv->macros_supported[macro_id] = false;
	memset(&priv->macro_params[macro_id], 0, sizeof(struct macro_ops));
	lpass_cdc_reopen(priv);
unlock:
	mutex_unlock(&priv->macro_lock);
}
EXPORT_SYMBOL(lpass_cdc_unregister_macro);

void lpass_cdc_notify_wcd_rx_clk(struct device *dev, bool is_native_on)
{
	struct lpass_cdc_priv *priv;
	u32 val;

	if (!dev) {
		pr_err_ratelimited("%s: dev is null\n", __func__);
		return;
	}
	if (!lpass_cdc_is_valid_child_dev(dev)) {
		dev_err_ratelimited(dev, "%s: not a valid child dev\n",
			__func__);
		return;
	}
	priv = dev_get_drvdata(dev->parent);
	if (!priv) {
		dev_err_ratelimited(dev, "%s: priv is null\n", __func__);
		return;
	}
	if (is_native_on)
		val = 0x2;  /* 11.2896M */
	else
		val = 0x0; /* 9.6M */

	lpass_cdc_notifier_call(priv,
		((val << 16) | LPASS_CDC_WCD_EVT_CLK_NOTIFY));

}
EXPORT_SYMBOL(lpass_cdc_notify_wcd_rx_clk);

void lpass_cdc_wsa_pa_on(struct device *dev, bool adie_lb)
{
	struct lpass_cdc_priv *priv;

	if (!dev) {
		pr_err_ratelimited("%s: dev is null\n", __func__);
		return;
	}
	if (!lpass_cdc_is_valid_child_dev(dev)) {
		dev_err_ratelimited(dev, "%s: not a valid child dev\n",
			__func__);
		return;
	}
	priv = dev_get_drvdata(dev->parent);
	if (!priv) {
		dev_err_ratelimited(dev, "%s: priv is null\n", __func__);
		return;
	}
	if (adie_lb)
		lpass_cdc_notifier_call(priv,
			LPASS_CDC_WCD_EVT_PA_ON_POST_FSCLK_ADIE_LB);
	else
		lpass_cdc_notifier_call(priv,
			LPASS_CDC_WCD_EVT_PA_ON_POST_FSCLK);
}
EXPORT_SYMBOL(lpass_cdc_wsa_pa_on);

int lpass_cdc_get_version(struct device *dev)
{
	struct lpass_cdc_priv *priv;

	if (!dev) {
		pr_err("%s: dev is null\n", __func__);
		return -EINVAL;
	}
	if (!lpass_cdc_is_valid_child_dev(dev)) {
		dev_err(dev, "%s: child device for macro not added yet\n",
			__func__);
		return -EINVAL;
	}
	priv = dev_get_drvdata(dev->parent);
	if (!priv) {
		dev_err(dev, "%s: priv is null\n", __func__);
		return -EINVAL;
	}
	return priv->version;
}
EXPORT_SYMBOL(lpass_cdc_get_version);

static ssize_t lpass_cdc_version_read(struct snd_info_entry *entry,
				   void *file_private_data,
				   struct file *file,
				   char __user *buf, size_t count,
				   loff_t pos)
{
	struct lpass_cdc_priv *priv;
	char buffer[LPASS_CDC_VERSION_ENTRY_SIZE];
	int len = 0;

	priv = (struct lpass_cdc_priv *) entry->private_data;
	if (!priv) {
		pr_err_ratelimited("%s: lpass_cdc priv is null\n", __func__);
		return -EINVAL;
	}

	switch (priv->version) {
	case LPASS_CDC_VERSION_1_0:
		len = snprintf(buffer, sizeof(buffer), "LPASS-CDC_1_0\n");
		break;
	case LPASS_CDC_VERSION_1_1:
		len = snprintf(buffer, sizeof(buffer), "LPASS-CDC_1_1\n");
		break;
	case LPASS_CDC_VERSION_1_2:
		len = snprintf(buffer, sizeof(buffer), "LPASS-CDC_1_2\n");
		break;
	case LPASS_CDC_VERSION_2_1:
		len = snprintf(buffer, sizeof(buffer), "LPASS-CDC_2_1\n");
		break;
	case LPASS_CDC_VERSION_2_5:
		len = snprintf(buffer, sizeof(buffer), "LPASS-CDC_2_5\n");
		break;
	case LPASS_CDC_VERSION_2_6:
		len = snprintf(buffer, sizeof(buffer), "LPASS-CDC_2_6\n");
		break;
	default:
		len = snprintf(buffer, sizeof(buffer), "VER_UNDEFINED\n");
	}

	return simple_read_from_buffer(buf, count, &pos, buffer, len);
}

static int lpass_cdc_macro_event(struct lpass_cdc_priv *priv, u16 event)
{
	int i, ret, first = 0;

	for (i = 0; i < MAX_MACRO; i++) {
		if (!priv->macro_params[i].event_handler)
			continue;
		ret = priv->macro_params[i].event_handler(priv->component, event, 0);
		if (ret && !first)
			first = ret;
		if (ret && event != LPASS_CDC_MACRO_EVT_SSR_DOWN)
			break;
	}
	return first;
}

static void lpass_cdc_ssr_down(struct lpass_cdc_priv *priv)
{
	int ret;

	regcache_cache_only(priv->regmap, true);
	mutex_lock(&priv->clk_lock);
	priv->dev_up = false;
	mutex_unlock(&priv->clk_lock);
	mutex_lock(&priv->vote_lock);
	priv->pre_dev_up = false;
	mutex_unlock(&priv->vote_lock);
	if (priv->rsc_clk_cb) {
		ret = priv->rsc_clk_cb(priv->clk_dev, LPASS_CDC_MACRO_EVT_SSR_DOWN);
		if (ret)
			dev_err_ratelimited(priv->dev, "clock SSR down failed: %d\n", ret);
	}
	ret = lpass_cdc_macro_event(priv, LPASS_CDC_MACRO_EVT_SSR_DOWN);
	if (ret)
		dev_err_ratelimited(priv->dev, "macro SSR down failed: %d\n", ret);
	lpass_cdc_notifier_call(priv, LPASS_CDC_WCD_EVT_SSR_DOWN);
}

static int lpass_cdc_ssr_enable_active(struct device *dev, void *data)
{
	struct lpass_cdc_priv *priv = data;
	int ret;

	if (priv->initial_boot) {
		priv->initial_boot = false;
		return 0;
	}
	if (!priv->rsc_clk_cb)
		return -ENODEV;
	ret = priv->rsc_clk_cb(priv->clk_dev, LPASS_CDC_MACRO_EVT_SSR_UP);
	if (ret)
		goto fail;
	ret = lpass_cdc_macro_event(priv, LPASS_CDC_MACRO_EVT_CLK_RESET);
	if (ret)
		goto fail;
	mutex_lock(&priv->vote_lock);
	priv->pre_dev_up = true;
	mutex_unlock(&priv->vote_lock);
	ret = priv->rsc_clk_cb(priv->clk_dev, LPASS_CDC_MACRO_EVT_SSR_GFMUX_UP);
	if (ret)
		goto fail;
	ret = lpass_cdc_macro_event(priv, LPASS_CDC_MACRO_EVT_PRE_SSR_UP);
	if (ret)
		goto fail;
	regcache_cache_only(priv->regmap, false);
	mutex_lock(&priv->clk_lock);
	priv->dev_up = true;
	mutex_unlock(&priv->clk_lock);
	regcache_mark_dirty(priv->regmap);
	ret = lpass_cdc_clk_rsc_all_clocks(priv->clk_dev, true);
	if (ret)
		goto fail;
	ret = regcache_sync(priv->regmap);
	/* Ensure the last native register write has reached the device. */
	usleep_range(100, 110);
	lpass_cdc_clk_rsc_all_clocks(priv->clk_dev, false);
	if (ret)
		goto fail;
	ret = lpass_cdc_macro_event(priv, LPASS_CDC_MACRO_EVT_SSR_UP);
	if (ret)
		goto fail;
	lpass_cdc_notifier_call(priv, LPASS_CDC_WCD_EVT_SSR_UP);
	return 0;
fail:
	lpass_cdc_ssr_down(priv);
	return ret;
}

static int lpass_cdc_ssr_enable(struct device *dev, void *data)
{
	struct lpass_cdc_priv *priv = data;

	int ret;

	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	ret = lpass_cdc_ssr_enable_active(dev, data);
	lpass_cdc_op_put(priv, false);
	return ret;
}

static void lpass_cdc_ssr_disable_active(struct device *dev, void *data)
{
	lpass_cdc_ssr_down(data);
}

static void lpass_cdc_ssr_disable(struct device *dev, void *data)
{
	struct lpass_cdc_priv *priv = data;

	if (!lpass_cdc_op_get(priv, false))
		return;
	lpass_cdc_ssr_disable_active(dev, data);
	lpass_cdc_op_put(priv, false);
}

static struct snd_info_entry_ops lpass_cdc_info_ops = {
	.read = lpass_cdc_version_read,
};

static const struct snd_event_ops lpass_cdc_ssr_ops = {
	.enable = lpass_cdc_ssr_enable,
	.disable = lpass_cdc_ssr_disable,
};

/*
 * lpass_cdc_info_create_codec_entry - creates lpass_cdc module
 * @codec_root: The parent directory
 * @component: Codec component instance
 *
 * Creates lpass_cdc module and version entry under the given
 * parent directory.
 *
 * Return: 0 on success or negative error code on failure.
 */
int lpass_cdc_info_create_codec_entry(struct snd_info_entry *codec_root,
				   struct snd_soc_component *component)
{
	struct snd_info_entry *version_entry;
	struct lpass_cdc_priv *priv;
	struct snd_soc_card *card;

	if (!codec_root || !component)
		return -EINVAL;

	priv = snd_soc_component_get_drvdata(component);
	if (priv->entry) {
		dev_dbg(priv->dev,
			"%s:lpass_cdc module already created\n", __func__);
		return 0;
	}
	card = component->card;
	priv->entry = snd_info_create_module_entry(codec_root->module,
					     "lpass-cdc", codec_root);
	if (!priv->entry) {
		dev_dbg(component->dev, "%s: failed to create lpass_cdc entry\n",
			__func__);
		return -ENOMEM;
	}
	priv->entry->mode = S_IFDIR | 0555;
	if (snd_info_register(priv->entry) < 0) {
		snd_info_free_entry(priv->entry);
		priv->entry = NULL;
		return -ENOMEM;
	}

	version_entry = snd_info_create_card_entry(card->snd_card,
						   "version",
						   priv->entry);
	if (!version_entry) {
		dev_err_ratelimited(component->dev, "%s: failed to create lpass_cdc version entry\n",
			__func__);
		snd_info_free_entry(priv->entry);
		priv->entry = NULL;
		return -ENOMEM;
	}

	version_entry->private_data = priv;
	version_entry->size = LPASS_CDC_VERSION_ENTRY_SIZE;
	version_entry->content = SNDRV_INFO_CONTENT_DATA;
	version_entry->c.ops = &lpass_cdc_info_ops;

	if (snd_info_register(version_entry) < 0) {
		snd_info_free_entry(version_entry);
		snd_info_free_entry(priv->entry);
		priv->entry = NULL;
		return -ENOMEM;
	}
	priv->version_entry = version_entry;

	return 0;
}
EXPORT_SYMBOL(lpass_cdc_info_create_codec_entry);

/**
 * lpass_cdc_register_wake_irq - Register wake irq of Tx macro
 *
 * @component: codec component ptr.
 * @ipc_wakeup: bool to identify ipc_wakeup to be used or HW interrupt line.
 *
 * Return: 0 on success or negative error code on failure.
 */
static int lpass_cdc_register_wake_irq_active(struct snd_soc_component *component,
					      u32 ipc_wakeup)
{
	struct lpass_cdc_priv *priv = NULL;

	if (!component)
		return -EINVAL;

	priv = snd_soc_component_get_drvdata(component);
	if (!priv)
		return -EINVAL;

	if (!lpass_cdc_is_valid_codec_dev(priv->dev)) {
		dev_err_ratelimited(component->dev, "%s: invalid codec\n", __func__);
		return -EINVAL;
	}

	if (priv->macro_params[VA_MACRO].reg_wake_irq)
		return priv->macro_params[VA_MACRO].reg_wake_irq(component, ipc_wakeup);

	return 0;
}

int lpass_cdc_register_wake_irq(struct snd_soc_component *component,
				u32 ipc_wakeup)
{
	struct lpass_cdc_priv *priv = component ? snd_soc_component_get_drvdata(component) : NULL;
	int ret;

	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	ret = lpass_cdc_register_wake_irq_active(component, ipc_wakeup);
						 lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_register_wake_irq);

/**
 * lpass_cdc_tx_mclk_enable - Enable/Disable TX Macro mclk
 *
 * @component: pointer to codec component instance.
 * @enable: set true to enable, otherwise false.
 *
 * Returns 0 on success or -EINVAL on error.
 */
static int lpass_cdc_tx_mclk_enable_active(struct snd_soc_component *component,
					   bool enable)
{
	struct lpass_cdc_priv *priv = NULL;
	int ret = 0;

	if (!component)
		return -EINVAL;

	priv = snd_soc_component_get_drvdata(component);
	if (!priv)
		return -EINVAL;

	if (!lpass_cdc_is_valid_codec_dev(priv->dev)) {
		dev_err_ratelimited(component->dev, "%s: invalid codec\n", __func__);
		return -EINVAL;
	}

	if (priv->macro_params[TX_MACRO].clk_enable)
		ret = priv->macro_params[TX_MACRO].clk_enable(component,
								enable);

	return ret;
}

int lpass_cdc_tx_mclk_enable(struct snd_soc_component *component, bool enable)
{
	struct lpass_cdc_priv *priv = component ? snd_soc_component_get_drvdata(component) : NULL;
	int ret;

	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	ret = lpass_cdc_tx_mclk_enable_active(component, enable);
					      lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_tx_mclk_enable);

/**
 * lpass_cdc_register_event_listener - Register/Deregister to event listener
 *
 * @component: pointer to codec component instance.
 * @enable: when set to 1 registers to event listener otherwise, derigisters
 *          from the event listener
 *
 * Returns 0 on success or -EINVAL on error.
 */
static int lpass_cdc_register_event_listener_active(struct snd_soc_component *component,
						    bool enable)
{
	struct lpass_cdc_priv *priv = NULL;
	int ret = 0;

	if (!component)
		return -EINVAL;

	priv = snd_soc_component_get_drvdata(component);
	if (!priv)
		return -EINVAL;

	if (!lpass_cdc_is_valid_codec_dev(priv->dev)) {
		dev_err_ratelimited(component->dev, "%s: invalid codec\n", __func__);
		return -EINVAL;
	}

	if (priv->macro_params[TX_MACRO].reg_evt_listener)
		ret = priv->macro_params[TX_MACRO].reg_evt_listener(component,
								    enable);

	return ret;
}

int lpass_cdc_register_event_listener(struct snd_soc_component *component,
				      bool enable)
{
	struct lpass_cdc_priv *priv = component ? snd_soc_component_get_drvdata(component) : NULL;
	int ret;

	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	ret = lpass_cdc_register_event_listener_active(component, enable);
						       lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_register_event_listener);

static void lpass_cdc_component_cleanup(struct lpass_cdc_priv *priv,
					struct snd_soc_component *component)
{
	int i;

	lpass_cdc_component_drain(priv);
	if (priv->typec_registered) {
		typec_analog_acc_exit(component);
		priv->typec_registered = false;
	}
	if (priv->snd_event_registered) {
		snd_event_client_deregister(priv->dev);
		priv->snd_event_registered = false;
	}
	if (priv->entry) {
		snd_info_free_entry(priv->entry);
		priv->entry = NULL;
		priv->version_entry = NULL;
	}
	for (i = MAX_MACRO - 1; i >= 0; i--) {
		if (priv->macro_initialized[i] && priv->macro_params[i].exit)
			priv->macro_params[i].exit(component);
		priv->macro_initialized[i] = false;
	}
	priv->component = NULL;
	snd_soc_component_exit_regmap(component);
}

static int lpass_cdc_soc_codec_probe(struct snd_soc_component *component)
{
	struct lpass_cdc_priv *priv = dev_get_drvdata(component->dev);
	int macro_idx, ret = 0;
	u8 core_id_0 = 0, core_id_1 = 0, core_id_2 = 0;

	snd_soc_component_init_regmap(component, priv->regmap);
	priv->component = component;

	if (!priv->version) {
		/*
		 * In order for the ADIE RTC to differentiate between targets
		 * version info is used.
		 * Assign 1.0 for target with only one macro
		 * Assign 1.1 for target with two macros
		 * Assign 1.2 for target with more than two macros
		 */
		if (priv->num_macros_registered == 1)
			priv->version = LPASS_CDC_VERSION_1_0;
		else if (priv->num_macros_registered == 2)
			priv->version = LPASS_CDC_VERSION_1_1;
		else if (priv->num_macros_registered > 2)
			priv->version = LPASS_CDC_VERSION_1_2;
	}

	/* Assign lpass_cdc version */
	core_id_0 = snd_soc_component_read(component,
					LPASS_CDC_VA_TOP_CSR_CORE_ID_0);
	core_id_1 = snd_soc_component_read(component,
					LPASS_CDC_VA_TOP_CSR_CORE_ID_1);
	core_id_2 = snd_soc_component_read(component,
					LPASS_CDC_VA_TOP_CSR_CORE_ID_2);
	if ((core_id_0 == 0x01) && (core_id_1 == 0x0F))
		priv->version = LPASS_CDC_VERSION_2_0;
	if ((core_id_0 == 0x02) && (core_id_1 == 0x0E))
		priv->version = LPASS_CDC_VERSION_2_1;
	if ((core_id_0 == 0x02) && (core_id_1 == 0x0F))
		priv->version = LPASS_CDC_VERSION_2_5;
	if ((core_id_0 == 0x02) && (core_id_1 == 0x0F) && (core_id_2 == 0x60 || core_id_2 == 0x61))
		priv->version = LPASS_CDC_VERSION_2_6;

	/* call init for supported macros */
	for (macro_idx = START_MACRO; macro_idx < MAX_MACRO; macro_idx++) {
		if (priv->macro_params[macro_idx].init) {
			ret = priv->macro_params[macro_idx].init(component);
			if (ret < 0) {
				dev_err(component->dev,
					"%s: init for macro %d failed\n",
					__func__, macro_idx);
				goto err;
			}
			priv->macro_initialized[macro_idx] = true;
		}
	}

	ret = snd_event_client_register(priv->dev, &lpass_cdc_ssr_ops, priv);
	if (ret)
		goto err;
	priv->snd_event_registered = true;
	ret = snd_event_notify(priv->dev, SND_EVENT_UP);
	if (ret)
		goto err;

	/* Preserve the native presence-based DT selection. */
	if (of_find_property(component->card->dev->of_node,
			     "qcom,unsupported-typec-analog-acc", NULL)) {
		ret = typec_analog_acc_init(component);
		if (ret)
			goto err;
		priv->typec_registered = true;
	}
	mutex_lock(&priv->op_lock);
	priv->component_ready = true;
	mutex_unlock(&priv->op_lock);
	return 0;
err:
	lpass_cdc_component_cleanup(priv, component);
	return ret;
}

static void lpass_cdc_soc_codec_remove(struct snd_soc_component *component)
{
	struct lpass_cdc_priv *priv = dev_get_drvdata(component->dev);

	lpass_cdc_component_cleanup(priv, component);
}

static const struct snd_soc_component_driver lpass_cdc = {
	.name = DRV_NAME,
	.probe = lpass_cdc_soc_codec_probe,
	.remove = lpass_cdc_soc_codec_remove,
};

static void lpass_cdc_remove_child_devices(struct lpass_cdc_priv *priv)
{
	struct platform_device *child;
	int phase, i;

	/* Drain split-codec consumers before macros, then their clock provider. */
	for (phase = 0; phase < 3; phase++) {
		for (i = priv->child_count - 1; i >= 0; i--) {
			child = priv->pdev_child_devices[i];
			if (!child || priv->child_phase[i] != phase)
				continue;
			if (priv->wcd_dev == &child->dev) {
				mutex_lock(&priv->macro_lock);
				lpass_cdc_quiesce(priv);
				priv->wcd_dev = NULL;
				lpass_cdc_reopen(priv);
				mutex_unlock(&priv->macro_lock);
			}
			platform_device_unregister(child);
			priv->pdev_child_devices[i] = NULL;
		}
	}
	priv->wcd_dev = NULL;
	priv->child_count = 0;
	priv->dev->platform_data = NULL;
}

static int lpass_cdc_add_child_devices(struct lpass_cdc_priv *priv)
{
	struct platform_device *child;
	struct device_node *node;
	int ret;

	priv->dev->platform_data = &priv->plat_data;
	for_each_available_child_of_node(priv->dev->of_node, node) {
		if (priv->child_count == LPASS_CDC_CHILD_DEVICES_MAX) {
			ret = -E2BIG;
			goto fail;
		}
		child = platform_device_alloc(node->name, PLATFORM_DEVID_NONE);
		if (!child) {
			ret = -ENOMEM;
			goto fail;
		}
		child->dev.parent = priv->dev;
		child->dev.of_node = of_node_get(node);
		if (of_property_read_bool(node, "qcom,split-codec")) {
			priv->child_phase[priv->child_count] = 0;
			priv->wcd_dev = &child->dev;
		} else if (of_device_is_compatible(node, "qcom,lpass-cdc-clk-rsc-mngr")) {
			priv->child_phase[priv->child_count] = 2;
		} else {
			priv->child_phase[priv->child_count] = 1;
		}
		ret = platform_device_add(child);
		if (ret) {
			if (priv->wcd_dev == &child->dev)
				priv->wcd_dev = NULL;
			platform_device_put(child);
			goto fail;
		}
		priv->pdev_child_devices[priv->child_count++] = child;
	}
	return 0;
fail:
	of_node_put(node);
	lpass_cdc_remove_child_devices(priv);
	return ret;
}

static int regdump_read(struct regmap *map, int base_reg, int end_reg,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	int i = 0, ret = 0;
	size_t pos = 0;
	char *buf;
	unsigned int reg_val = 0, reg_len = 0;
	unsigned int reg_val_len = 0, regdump_wr_len = 0;

	if (*ppos < 0 || *ppos > end_reg - base_reg)
		return 0;
	count = min_t(size_t, count, PAGE_SIZE);
	i = *ppos + base_reg;

	buf = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	reg_len = scnprintf(buf, count, "%x", end_reg);
	reg_val_len = 2 * DIV_ROUND_UP(REGDUMP_PRINT_LEN, 8);
	regdump_wr_len = reg_len + reg_val_len + 3;

	for (; i >= 0 && i <= end_reg; i += REGDUMP_PRINT_STRIDE) {
		/*
		 * Check not to overwrite the buffer, by ensuring we have
		 * space for writing the register data
		 *
		 * this is scalable if we use proc or any other fs interface
		 * as count would take the buf_size passed from userspace
		 */
		if ((pos + regdump_wr_len) >= count) {
			pr_debug("%s: Buffer full: stopping at register 0x%x\n", __func__, i);
			break;
		}

		scnprintf(buf+pos, count-pos, "%.*x: ", reg_len, i);
		pos += reg_len + 2;
		ret = regmap_read_bypassed(map, i, &reg_val);
		if (ret == 0)
			scnprintf(buf+pos, count-pos, "%.*x", reg_val_len, reg_val);
		else
			memset(buf+pos, 'X', reg_val_len);

		pos +=  reg_val_len;
		buf[pos++] = '\n';
	}

	ret = pos;
	if (copy_to_user(user_buf, buf, pos))
		ret = -EFAULT;
	else
		*ppos = i - base_reg;

	kfree(buf);
	return ret;
}

static ssize_t lpass_cdc_proc_read(struct file *filep, char __user *buf, size_t size, loff_t *ppos)
{
	ssize_t ret = 0;
	struct lpass_cdc_priv *priv = NULL;

	if (!size || !filep || !ppos || !buf || *ppos < 0)
		return -EINVAL;

	priv = pde_data(file_inode(filep));
	if (!priv)
		return -EINVAL;

	ret = regdump_read(priv->regmap, TX_START_OFFSET, WSA2_MAX_OFFSET, buf, size, ppos);
	return ret;
}

static const struct proc_ops lpass_cdc_proc_ops = {
	.proc_read = lpass_cdc_proc_read,
};

static int lpass_cdc_probe(struct platform_device *pdev)
{
	struct lpass_cdc_priv *priv;
	u32 num_macros = 0;
	int ret;
	struct clk *lpass_core_hw_vote = NULL;
	struct clk *lpass_audio_hw_vote = NULL;
	struct proc_dir_entry *cdc_proc_regdump_file = NULL;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct lpass_cdc_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	ret = of_property_read_u32(pdev->dev.of_node, "qcom,num-macros",
				   &num_macros);
	if (ret) {
		dev_err(&pdev->dev,
			"%s:num-macros property not found\n",
			__func__);
		return ret;
	}
	if (!num_macros || num_macros > MAX_MACRO)
		return -EINVAL;
	priv->num_macros = num_macros;

	ret = of_property_read_u32(pdev->dev.of_node,
				"qcom,lpass-cdc-version", &priv->version);
	if (ret) {
		dev_dbg(&pdev->dev, "%s:lpass_cdc version not specified\n",
			__func__);
		ret = 0;
	}

	BLOCKING_INIT_NOTIFIER_HEAD(&priv->notifier);
	priv->dev = &pdev->dev;
	priv->dev_up = true;
	priv->pre_dev_up = true;
	priv->initial_boot = true;

	priv->read_dev = __lpass_cdc_reg_read;
	priv->write_dev = __lpass_cdc_reg_write;

	priv->plat_data.handle = (void *)priv;
	priv->plat_data.update_wcd_event = lpass_cdc_update_wcd_event;
	priv->plat_data.register_notifier = lpass_cdc_register_notifier;

	priv->core_hw_vote_count = 0;
	priv->core_audio_vote_count = 0;

	dev_set_drvdata(&pdev->dev, priv);
	mutex_init(&priv->macro_lock);
	mutex_init(&priv->io_lock);
	mutex_init(&priv->clk_lock);
	mutex_init(&priv->vote_lock);
	mutex_init(&priv->op_lock);
	mutex_init(&priv->dmic_lock);
	init_waitqueue_head(&priv->op_wait);
	atomic_set(&priv->active_ops, 0);
	atomic_set(&priv->component_ops, 0);

	/* Register LPASS core hw vote */
	lpass_core_hw_vote = devm_clk_get_optional(&pdev->dev, "lpass_core_hw_vote");
	if (IS_ERR(lpass_core_hw_vote))
		return PTR_ERR(lpass_core_hw_vote);
	priv->lpass_core_hw_vote = lpass_core_hw_vote;

	/* Register LPASS audio hw vote */
	lpass_audio_hw_vote = devm_clk_get_optional(&pdev->dev, "lpass_audio_hw_vote");
	if (IS_ERR(lpass_audio_hw_vote))
		return PTR_ERR(lpass_audio_hw_vote);
	priv->lpass_audio_hw_vote = lpass_audio_hw_vote;
	priv->regmap = lpass_cdc_regmap_init(priv->dev,
					  &lpass_cdc_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	ret = lpass_cdc_add_child_devices(priv);
	if (ret)
		return ret;
	devm_regmap_qti_debugfs_register(priv->dev, priv->regmap);

	priv->lpass_cdc_proc_entry = proc_mkdir("lpass_cdc_reginfo", NULL);
	if (priv->lpass_cdc_proc_entry) {
		cdc_proc_regdump_file = proc_create_data("lpass_cdc_regdump", 0444,
				priv->lpass_cdc_proc_entry, &lpass_cdc_proc_ops, priv);
		if (!cdc_proc_regdump_file) {
			dev_err(&pdev->dev,
					"%s: error creating proc read file interface\n",
					__func__);
			proc_remove(priv->lpass_cdc_proc_entry);
			priv->lpass_cdc_proc_entry = NULL;
		}
	} else {
		dev_err(&pdev->dev, "%s: error creating proc dir interface\n", __func__);
	}

	return 0;
}

static void lpass_cdc_remove(struct platform_device *pdev)
{
	struct lpass_cdc_priv *priv = dev_get_drvdata(&pdev->dev);

	proc_remove(priv->lpass_cdc_proc_entry);
	mutex_lock(&priv->macro_lock);
	priv->removing = true;
	mutex_unlock(&priv->macro_lock);
	lpass_cdc_remove_child_devices(priv);
	mutex_lock(&priv->macro_lock);
	lpass_cdc_unregister_component(priv);
	lpass_cdc_quiesce(priv);
	if (priv->core_audio_vote_count)
		digital_cdc_rsc_mgr_hw_vote_disable(priv->lpass_audio_hw_vote, priv->dev);
	if (priv->core_hw_vote_count)
		digital_cdc_rsc_mgr_hw_vote_disable(priv->lpass_core_hw_vote, priv->dev);
	mutex_unlock(&priv->macro_lock);
}

#ifdef CONFIG_PM
int lpass_cdc_runtime_resume(struct device *dev)
{
	struct lpass_cdc_priv *priv;
	bool core = false, audio = false;
	int ret = 0;

	if (!lpass_cdc_is_valid_child_dev(dev))
		return -EINVAL;
	priv = dev_get_drvdata(dev->parent);
	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	mutex_lock(&priv->vote_lock);
	if (priv->core_hw_vote_count == INT_MAX ||
	    priv->core_audio_vote_count == INT_MAX ||
	    priv->core_clk_vote_count == INT_MAX) {
		ret = -EOVERFLOW;
		goto out;
	}
	if (priv->lpass_core_hw_vote && !priv->core_hw_vote_count) {
		ret = digital_cdc_rsc_mgr_hw_vote_enable(priv->lpass_core_hw_vote, dev);
		if (ret)
			goto out;
		core = true;
	}
	if (priv->lpass_audio_hw_vote && !priv->core_audio_vote_count) {
		ret = digital_cdc_rsc_mgr_hw_vote_enable(priv->lpass_audio_hw_vote, dev);
		if (ret)
			goto unwind;
		audio = true;
	}
	if (!priv->core_clk_vote_count) {
		ret = lpass_cdc_clk_rsc_request_clock(dev, TX_CORE_CLK, TX_CORE_CLK, true);
		if (ret)
			goto unwind;
	}
	if (priv->lpass_core_hw_vote)
		priv->core_hw_vote_count++;
	if (priv->lpass_audio_hw_vote)
		priv->core_audio_vote_count++;
	priv->core_clk_vote_count++;
	pm_runtime_set_autosuspend_delay(priv->dev, LPASS_CDC_AUTO_SUSPEND_DELAY);
	goto out;
unwind:
	if (audio)
		digital_cdc_rsc_mgr_hw_vote_disable(priv->lpass_audio_hw_vote, dev);
	if (core)
		digital_cdc_rsc_mgr_hw_vote_disable(priv->lpass_core_hw_vote, dev);
out:
	mutex_unlock(&priv->vote_lock);
	lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_runtime_resume);

int lpass_cdc_runtime_suspend(struct device *dev)
{
	struct lpass_cdc_priv *priv;
	int ret = 0;

	if (!lpass_cdc_is_valid_child_dev(dev))
		return -EINVAL;
	priv = dev_get_drvdata(dev->parent);
	if (!lpass_cdc_op_get(priv, false))
		return -ENODEV;
	mutex_lock(&priv->vote_lock);
	if (!priv->core_clk_vote_count ||
	    (priv->lpass_core_hw_vote && !priv->core_hw_vote_count) ||
	    (priv->lpass_audio_hw_vote && !priv->core_audio_vote_count)) {
		ret = -EINVAL;
		goto out;
	}
	if (priv->core_clk_vote_count == 1) {
		ret = lpass_cdc_clk_rsc_request_clock(dev, TX_CORE_CLK, TX_CORE_CLK, false);
		if (ret)
			goto out;
	}
	priv->core_clk_vote_count--;
	if (priv->lpass_audio_hw_vote && !--priv->core_audio_vote_count)
		digital_cdc_rsc_mgr_hw_vote_disable(priv->lpass_audio_hw_vote, dev);
	if (priv->lpass_core_hw_vote && !--priv->core_hw_vote_count)
		digital_cdc_rsc_mgr_hw_vote_disable(priv->lpass_core_hw_vote, dev);
out:
	mutex_unlock(&priv->vote_lock);
	lpass_cdc_op_put(priv, false);
	return ret;
}
EXPORT_SYMBOL(lpass_cdc_runtime_suspend);
#endif /* CONFIG_PM */

bool lpass_cdc_check_core_votes(struct device *dev)
{
	struct lpass_cdc_priv *priv = dev_get_drvdata(dev->parent);
	bool ret = true;
	mutex_lock(&priv->vote_lock);
	if (!priv->pre_dev_up ||
		(priv->lpass_core_hw_vote && !priv->core_hw_vote_count) ||
		(priv->lpass_audio_hw_vote && !priv->core_audio_vote_count))
		ret = false;
	mutex_unlock(&priv->vote_lock);

	return ret;
}
EXPORT_SYMBOL(lpass_cdc_check_core_votes);

static const struct of_device_id lpass_cdc_dt_match[] = {
	{.compatible = "qcom,lpass-cdc"},
	{}
};
MODULE_DEVICE_TABLE(of, lpass_cdc_dt_match);

static struct platform_driver lpass_cdc_drv = {
	.driver = {
		.name = "lpass-cdc",
		.owner = THIS_MODULE,
		.of_match_table = lpass_cdc_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = lpass_cdc_probe,
	.remove = lpass_cdc_remove,
};

static int lpass_cdc_drv_init(void)
{
	return platform_driver_register(&lpass_cdc_drv);
}

static void lpass_cdc_drv_exit(void)
{
	platform_driver_unregister(&lpass_cdc_drv);
}

static int __init lpass_cdc_init(void)
{
	int ret;

	ret = lpass_cdc_clk_rsc_mgr_init();
	if (ret)
		return ret;
	ret = lpass_cdc_drv_init();
	if (ret)
		lpass_cdc_clk_rsc_mgr_exit();
	return ret;
}
module_init(lpass_cdc_init);

static void __exit lpass_cdc_exit(void)
{
	lpass_cdc_drv_exit();
	lpass_cdc_clk_rsc_mgr_exit();
}
module_exit(lpass_cdc_exit);

MODULE_DESCRIPTION("LPASS Codec driver");
MODULE_LICENSE("GPL v2");
