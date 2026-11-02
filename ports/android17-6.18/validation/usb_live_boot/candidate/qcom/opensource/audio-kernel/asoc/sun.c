// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define DEBUG
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/input.h>
#include <soc/qcom/socinfo.h>
#include <linux/of_device.h>
#include <linux/soc/qcom/fsa4480-i2c.h>
#if IS_ENABLED(CONFIG_QCOM_WCD_USBSS_I2C)
#include <linux/soc/qcom/wcd939x-i2c.h>
#endif
#include <linux/pm_qos.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <soc/snd_event.h>
#include <dsp/audio_prm.h>
#include <soc/swr-common.h>
#include <soc/soundwire.h>
#include "device_event.h"
#include "asoc/msm-cdc-pinctrl.h"
#include "asoc/wcd-mbhc-v2.h"
#include "codecs/wcd939x/wcd939x-mbhc.h"
#include "codecs/wcd9378/wcd9378-mbhc.h"
#include "codecs/wsa884x/wsa884x.h"
#include "codecs/wsa883x/wsa883x.h"
#include "codecs/wcd939x/wcd939x.h"
#include "codecs/wcd9378/wcd9378.h"
#include "codecs/lpass-cdc/lpass-cdc.h"
#include <bindings/audio-codec-port-types.h>
#include "codecs/lpass-cdc/lpass-cdc-wsa-macro.h"
#include "sun-port-config.h"
#include "msm-audio-defs.h"
#include "msm_common.h"
#include "msm_dailink.h"
#include "hwid.h"

#define DRV_NAME "sun-asoc-snd"
#define __CHIPSET__ "SUN "
#define MSM_DAILINK_NAME(name) (__CHIPSET__#name)

#define WCD9XXX_MBHC_DEF_RLOADS     5
#define WCD9XXX_MBHC_DEF_BUTTONS    8
#define CODEC_EXT_CLK_RATE          9600000
#define DEV_NAME_STR_LEN            32
#define WCD_MBHC_HS_V_MAX           1700

#define WCN_CDC_SLIM_RX_CH_MAX 2
#define WCN_CDC_SLIM_TX_CH_MAX 2
#define WCN_CDC_SLIM_TX_CH_MAX_LITO 3

/* Number of WSAs */
#define MONO_SPEAKER    1
#define STEREO_SPEAKER  2
#define QUAD_SPEAKER    4


//add for smartpa compatible begin
#define PA_NAME_MAX  24
#define  SMARTPA_MAX_NUM  4
static char smartpa_num = 0;
static char smartpa_type[PA_NAME_MAX] = "none";
static DEFINE_MUTEX(smartpa_lock);
static DEFINE_MUTEX(machine_probe_lock);
static struct device *machine_device;
#if defined(CONFIG_TARGET_PRODUCT_YUPEI)
static int tdm_rx0_id = 0;
static int tdm_rx1_id = 0;
static int tdm_tx_id = 0;
#elif defined(CONFIG_TARGET_PRODUCT_PIANO)
static int tdm_rx0_id = 1;
static int tdm_rx1_id = 1;
static int tdm_tx_id = 1;
#elif defined(CONFIG_TARGET_PRODUCT_ANNIBALE)
static int tdm_rx0_id = 1;
static int tdm_rx1_id = 1;
static int tdm_tx_id = 1;
#else
static int tdm_rx0_id = 5;
static int tdm_rx1_id = 6;
static int tdm_tx_id = 5;
#endif

static const char *smartpa_cust_name[SMARTPA_MAX] = {
	[SMARTPA_NONE] = "none",
	[SMARTPA_FS19XX] = "fs19xx",
	[SMARTPA_SIA91XX] = "sia91xx",
	[SMARTPA_AW882XX] = "aw882xx",
};

static const char *tdm_rx_dailink_name[] = {
	LPASS_BE_PRI_TDM_RX_0,
	LPASS_BE_SEC_TDM_RX_0,
	LPASS_BE_TERT_TDM_RX_0,
	LPASS_BE_SEN_TDM_RX_0,
	LPASS_BE_QUAT_TDM_RX_0,
	LPASS_BE_QUIN_TDM_RX_0,//quin_tdm_tx_0
	LPASS_BE_QUIN_TDM_RX_0_VIRT,
	LPASS_BE_SEP_TDM_RX_0
};

static const char *tdm_tx_dailink_name[] = {
	LPASS_BE_PRI_TDM_TX_0,
	LPASS_BE_SEC_TDM_TX_0,
	LPASS_BE_TERT_TDM_TX_0,
	LPASS_BE_SEN_TDM_TX_0,
	LPASS_BE_QUAT_TDM_TX_0,
	LPASS_BE_QUIN_TDM_TX_0,//quin_tdm_tx_0
	LPASS_BE_SEP_TDM_TX_0
};

static unsigned int smartpa_count;
static struct snd_soc_dai_link_component smartpa_dails[SMARTPA_MAX_NUM];
static struct device *smartpa_devices[SMARTPA_MAX_NUM];
//add for smartpa compatibe end

enum {
	WCD937X_DEV_INDEX,
	WCD939X_DEV_INDEX,
	WCD9378_DEV_INDEX,
};

struct msm_asoc_mach_data {
	struct snd_info_entry *codec_root;
	struct msm_common_pdata *common_pdata;
	int usbc_en2_gpio; /* used by gpio driver API */
	struct device_node *dmic01_gpio_p; /* used by pinctrl API */
	struct device_node *dmic23_gpio_p; /* used by pinctrl API */
	struct device_node *dmic45_gpio_p; /* used by pinctrl API */
	struct device_node *dmic67_gpio_p; /* used by pinctrl API */
	struct device_node *dmic_enable_gpio; /* used by pinctrl API */
	struct pinctrl *usbc_en2_gpio_p; /* used by pinctrl API */
	bool is_afe_config_done;
	struct device_node *fsa_handle;
	struct device_node *wcd_usbss_handle;
	struct clk *lpass_audio_hw_vote;
	int core_audio_vote_count;
	u32 wsa_max_devs;
	int wcd_disabled;
	int (*get_dev_num)(struct snd_soc_component *);
	int backend_used;
	struct prm_earpa_hw_intf_config upd_config;
	bool dedicated_wsa2; /* used to define how wsa2 slave devices are used */
	int wcd_used;
	struct wcd_mbhc_config mbhc_cfg;
	struct snd_soc_component *mbhc_component;
	bool card_registered;
	bool ssr_registered;
	bool dmic_enabled;
	int dmic_gpio_count[4];
};

static struct snd_soc_card snd_soc_card_sun_msm;

static void *def_wcd_mbhc_cal(void);

static int msm_rx_tx_codec_init(struct snd_soc_pcm_runtime*);
static void msm_rx_tx_codec_exit(struct snd_soc_pcm_runtime *rtd);
static int msm_int_wsa_init(struct snd_soc_pcm_runtime*);
static int msm_int_wsa884x_init(struct snd_soc_pcm_runtime*);
static int msm_int_wsa883x_init(struct snd_soc_pcm_runtime*);
static int msm_int_wsa2_init(struct snd_soc_pcm_runtime *);
static int msm_int_wsa884x_2_init(struct snd_soc_pcm_runtime *);
static int msm_int_wsa883x_2_init(struct snd_soc_pcm_runtime *);

/*
 * Need to report LINEIN
 * if R/L channel impedance is larger than 5K ohm
 */
static const struct wcd_mbhc_config wcd_mbhc_defaults = {
	.read_fw_bin = false,
	.calibration = NULL,
	.detect_extn_cable = true,
	.mono_stero_detection = false,
	.swap_gnd_mic = NULL,
	.hs_ext_micbias = true,
	.key_code[0] = KEY_MEDIA,
	.key_code[1] = BTN_1,
	.key_code[2] = BTN_2,
	.key_code[3] = 0,
	.key_code[4] = 0,
	.key_code[5] = 0,
	.key_code[6] = 0,
	.key_code[7] = 0,
	.linein_th = 5000,
	.moisture_en = false,
	.mbhc_micbias = MIC_BIAS_2,
	.anc_micbias = MIC_BIAS_2,
	.enable_anc_mic_detect = false,
	.moisture_duty_cycle_en = true,
};

static int usbhs_direction_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct msm_asoc_mach_data *pdata;

	if (!component || !component->card)
		return -ENODEV;
	pdata = snd_soc_card_get_drvdata(component->card);
	if (!pdata)
		return -ENODEV;
	ucontrol->value.integer.value[0] = READ_ONCE(pdata->mbhc_cfg.flip_switch);
	return 0;
}

static const struct snd_kcontrol_new msm_common_snd_controls[] = {
	SOC_SINGLE_EXT("USB Headset Direction", 0, 0, UINT_MAX, 0,usbhs_direction_get, NULL),
};

static bool msm_usbc_swap_gnd_mic(struct snd_soc_component *component, bool active)
{
	int ret = 0;
	struct snd_soc_card *card = component->card;
	struct msm_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);

	if (!pdata->wcd_usbss_handle && !pdata->fsa_handle)
		return false;
	pdata->mbhc_cfg.flip_switch = true;
	if (pdata->fsa_handle) {
		ret = fsa4480_switch_event(pdata->fsa_handle, FSA_MIC_GND_SWAP);
	} else {
#if IS_ENABLED(CONFIG_QCOM_WCD_USBSS_I2C)
		if (pdata->mbhc_cfg.usbss_hsj_connect_enable)
			ret = wcd_usbss_switch_update(WCD_USBSS_GND_MIC_SWAP_HSJ,
							WCD_USBSS_CABLE_CONNECT);
		else if (pdata->mbhc_cfg.enable_usbc_analog)
			ret = wcd_usbss_switch_update(WCD_USBSS_GND_MIC_SWAP_AATC,
							WCD_USBSS_CABLE_CONNECT);
#endif //need check more
	}
	if (ret == 0)
		return true;
	else
		return false;
}

static void msm_parse_upd_configuration(struct platform_device *pdev,
					struct msm_asoc_mach_data *pdata)
{
	int ret = 0;
	u32 dt_values[2];

	if (!pdev || !pdata)
		return;

	ret = of_property_read_string(pdev->dev.of_node,
		"qcom,upd_backends_used", &pdata->upd_config.backend_used);
	if (ret) {
		pr_debug("%s:could not find %s entry in dt\n",
			__func__, "qcom,upd_backends_used");
		return;
	}

	ret = of_property_read_u32_array(pdev->dev.of_node,
			"qcom,upd_lpass_reg_addr", dt_values, MAX_EARPA_REG);
	if (ret) {
		pr_debug("%s: could not find %s entry in dt\n",
				__func__, "qcom,upd_lpass_reg_addr");
		return;
	} else {
		pdata->upd_config.ear_pa_hw_reg_cfg.lpass_cdc_rx0_rx_path_ctl_phy_addr =
									dt_values[0];
		pdata->upd_config.ear_pa_hw_reg_cfg.lpass_wr_fifo_reg_phy_addr =
								dt_values[1];
	}

	ret = of_property_read_u32(pdev->dev.of_node,
			"qcom,upd_ear_pa_reg_addr", &pdata->upd_config.ear_pa_pkd_reg_addr);
	if (ret) {
		pr_debug("%s: could not find %s entry in dt\n",
			__func__, "qcom,upd_ear_pa_reg_addr");
	}
}

static void msm_set_upd_config(struct snd_soc_pcm_runtime *rtd)
{
	int val1 = 0, val2 = 0, ret = 0;
	u8  dev_num = 0;
	struct snd_soc_component *component = NULL;
	struct msm_asoc_mach_data *pdata = NULL;
	char wcd_name[20];

	if (!rtd) {
		pr_err_ratelimited("%s: rtd is NULL\n", __func__);
		return;
	}

	pdata = snd_soc_card_get_drvdata(rtd->card);
	if (!pdata) {
		pr_err_ratelimited("%s: pdata is NULL\n", __func__);
		return;
	}

	if (!pdata->upd_config.ear_pa_hw_reg_cfg.lpass_cdc_rx0_rx_path_ctl_phy_addr ||
		!pdata->upd_config.ear_pa_hw_reg_cfg.lpass_wr_fifo_reg_phy_addr ||
                !pdata->upd_config.ear_pa_pkd_reg_addr) {
		pr_err_ratelimited("%s: upd static configuration is not set\n", __func__);
		return;
	}

	if (!strcmp(pdata->upd_config.backend_used, "wsa")) {
		if (pdata->wsa_max_devs > 0) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.1");
			if (!component) {
				pr_err("%s: %s component is NULL\n", __func__,
					"wsa-codec.1");
				return;
			}
		} else {
			pr_err("%s wsa_max_devs are NULL\n", __func__);
			return;
		}
	} else {
		if (pdata->wcd_used == WCD9378_DEV_INDEX)
			strscpy(wcd_name, WCD9378_DRV_NAME, sizeof(WCD9378_DRV_NAME));
		else
			strscpy(wcd_name, WCD939X_DRV_NAME, sizeof(WCD939X_DRV_NAME));
		component = snd_soc_rtdcom_lookup(rtd, wcd_name);
		if (!component) {
			pr_err("%s component is NULL\n", __func__);
			return;
		}
	}

	if (!strcmp(pdata->upd_config.backend_used, "wsa")) {
		if (strstr(rtd->card->name, "wsa883x"))
			pdata->get_dev_num = wsa883x_codec_get_dev_num;
		else
			pdata->get_dev_num = wsa884x_codec_get_dev_num;
	} else {
		if (pdata->wcd_used == WCD9378_DEV_INDEX) {
			pdata->get_dev_num = wcd9378_codec_get_dev_num;
		} else {
			pdata->get_dev_num = wcd939x_codec_get_dev_num;
		}
	}

	if (!pdata->get_dev_num) {
		pr_err("%s: get_dev_num is NULL\n", __func__);
		return;
	}

	dev_num = pdata->get_dev_num(component);
	if (dev_num < 0 || dev_num > 6) {
		pr_err_ratelimited("%s: invalid slave dev num : %d\n", __func__,
							dev_num);
		return;
	}

	pdata->upd_config.ear_pa_pkd_cfg.ear_pa_enable_pkd_reg_addr =
				pdata->upd_config.ear_pa_pkd_reg_addr & 0xFFFF;
	pdata->upd_config.ear_pa_pkd_cfg.ear_pa_disable_pkd_reg_addr =
				pdata->upd_config.ear_pa_pkd_reg_addr & 0xFFFF;

	val1 = val2 = 0;

	/* bits 16:19 carry command id */
	val1 |= 1 << 16;

	/* bits 20:23 carry swr device number */
	val1 |= dev_num << 20;

	/*
	 * bits 24:31 carry 8 bit data to disable or enable ear pa
	 * for wcd 7bit is global enable bit - 1 -enable. 0 - disable
	 * for wsa 0bit is global enable bit - 1 -enable, 0 - disable
	*/
	val2 = val1;

	if (!strcmp(pdata->upd_config.backend_used, "wsa"))
		val1 |= 1 << 24;
	else
		val1 |= 1 << 31;

	pdata->upd_config.ear_pa_pkd_cfg.ear_pa_enable_pkd_reg_addr |= val1;
	pdata->upd_config.ear_pa_pkd_cfg.ear_pa_disable_pkd_reg_addr |= val2;

	ret = audio_prm_set_cdc_earpa_duty_cycling_req(&pdata->upd_config, 1);
	if (ret < 0) {
		pr_err_ratelimited("%s: upd cdc duty cycling registration failed\n", __func__);
		return;
	}

	pr_debug("%s: upd cdc duty cycling registration done successfully!\n", __func__);
}

static struct snd_soc_ops msm_common_be_ops = {
	.hw_params = msm_common_snd_hw_params,
	.startup = msm_common_snd_startup,
	.shutdown = msm_common_snd_shutdown,
};

static int msm_dmic_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol, int event)
{
	struct msm_asoc_mach_data *pdata = NULL;
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int ret = 0;
	u32 dmic_idx;
	int *dmic_gpio_cnt;
	struct device_node *dmic_gpio;
	char  *wname;

	wname = strpbrk(w->name, "01234567");
	if (!wname) {
		dev_err_ratelimited(component->dev, "%s: widget not found\n", __func__);
		return -EINVAL;
	}

	ret = kstrtouint(wname, 10, &dmic_idx);
	if (ret < 0) {
		dev_err_ratelimited(component->dev, "%s: Invalid DMIC line on the codec\n",
			__func__);
		return -EINVAL;
	}

	pdata = snd_soc_card_get_drvdata(component->card);

	switch (dmic_idx) {
	case 0:
	case 1:
		dmic_gpio_cnt = &pdata->dmic_gpio_count[0];
		dmic_gpio = pdata->dmic01_gpio_p;
		break;
	case 2:
	case 3:
		dmic_gpio_cnt = &pdata->dmic_gpio_count[1];
		dmic_gpio = pdata->dmic23_gpio_p;
		break;
	case 4:
	case 5:
		dmic_gpio_cnt = &pdata->dmic_gpio_count[2];
		dmic_gpio = pdata->dmic45_gpio_p;
		break;
	case 6:
	case 7:
		dmic_gpio_cnt = &pdata->dmic_gpio_count[3];
		dmic_gpio = pdata->dmic67_gpio_p;
		break;
	default:
		dev_err_ratelimited(component->dev, "%s: Invalid DMIC Selection\n",
			__func__);
		return -EINVAL;
	}

	dev_dbg(component->dev, "%s: event %d DMIC%d dmic_gpio_cnt %d\n",
			__func__, event, dmic_idx, *dmic_gpio_cnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		(*dmic_gpio_cnt)++;
		if (*dmic_gpio_cnt == 1) {
			ret = msm_cdc_pinctrl_select_active_state(
						dmic_gpio);
			if (ret < 0) {
				pr_err_ratelimited("%s: gpio set cannot be activated %sd",
					__func__, "dmic_gpio");
				return ret;
			}
		}

		break;
	case SND_SOC_DAPM_POST_PMD:
		(*dmic_gpio_cnt)--;
		if (*dmic_gpio_cnt == 0) {
			ret = msm_cdc_pinctrl_select_sleep_state(
					dmic_gpio);
			if (ret < 0) {
				pr_err_ratelimited("%s: gpio set cannot be de-activated %sd",
					__func__, "dmic_gpio");
				return ret;
			}
		}
		break;
	default:
		pr_err_ratelimited("%s: invalid DAPM event %d\n", __func__, event);
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dapm_widget msm_int_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Analog Mic1", NULL),
	SND_SOC_DAPM_MIC("Analog Mic2", NULL),
	SND_SOC_DAPM_MIC("Analog Mic3", NULL),
	SND_SOC_DAPM_MIC("Analog Mic4", NULL),
	SND_SOC_DAPM_MIC("Analog Mic5", NULL),
	SND_SOC_DAPM_MIC("Digital Mic0", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic1", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic2", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic3", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic4", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic5", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic6", msm_dmic_event),
	SND_SOC_DAPM_MIC("Digital Mic7", msm_dmic_event),
};

#ifndef CONFIG_AUDIO_BTFM_PROXY
static int msm_wcn_init(struct snd_soc_pcm_runtime *rtd)
{
	unsigned int rx_ch[WCN_CDC_SLIM_RX_CH_MAX] = {157, 158};
	unsigned int tx_ch[WCN_CDC_SLIM_TX_CH_MAX]  = {159, 160};
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
    int ret = 0;

	ret = snd_soc_dai_set_channel_map(codec_dai, ARRAY_SIZE(tx_ch),
					   tx_ch, ARRAY_SIZE(rx_ch), rx_ch);
	if (ret)
		return ret;

	msm_common_dai_link_init(rtd);
    return ret;
}
#endif

static struct snd_info_entry *msm_snd_info_create_subdir(struct module *mod,
				const char *name,
				struct snd_info_entry *parent)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(mod, name, parent);
	if (!entry)
		return NULL;
	entry->mode = S_IFDIR | 0555;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return NULL;
	}
	return entry;
}

static void *def_wcd_mbhc_cal(void)
{
	void *wcd_mbhc_cal;
	struct wcd_mbhc_btn_detect_cfg *btn_cfg;
	u16 *btn_high;

	wcd_mbhc_cal = kzalloc(WCD_MBHC_CAL_SIZE(WCD_MBHC_DEF_BUTTONS,
				WCD9XXX_MBHC_DEF_RLOADS), GFP_KERNEL);
	if (!wcd_mbhc_cal)
		return NULL;

	WCD_MBHC_CAL_PLUG_TYPE_PTR(wcd_mbhc_cal)->v_hs_max = WCD_MBHC_HS_V_MAX;
	WCD_MBHC_CAL_BTN_DET_PTR(wcd_mbhc_cal)->num_btn = WCD_MBHC_DEF_BUTTONS;
	btn_cfg = WCD_MBHC_CAL_BTN_DET_PTR(wcd_mbhc_cal);
	btn_high = ((void *)&btn_cfg->_v_btn_low) +
		(sizeof(btn_cfg->_v_btn_low[0]) * btn_cfg->num_btn);

	btn_high[0] = 75;
	btn_high[1] = 260;
	btn_high[2] = 500;
	btn_high[3] = 500;
	btn_high[4] = 500;
	btn_high[5] = 500;
	btn_high[6] = 500;
	btn_high[7] = 500;

	return wcd_mbhc_cal;
}

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link msm_common_be_dai_links[] = {
	/* Proxy Tx BACK END DAI Link */
	{
		.name = LPASS_BE_RT_PROXY_PCM_TX,
		.stream_name = LPASS_BE_RT_PROXY_PCM_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(proxy_tx),
	},
	{
		.name = LPASS_BE_RT_PROXY_PCM_TX_2,
		.stream_name = LPASS_BE_RT_PROXY_PCM_TX_2,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(proxy_tx2),
	},
	/* Proxy Rx BACK END DAI Link */
	{
		.name = LPASS_BE_RT_PROXY_PCM_RX,
		.stream_name = LPASS_BE_RT_PROXY_PCM_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(proxy_rx),
	},
	{
		.name = LPASS_BE_USB_AUDIO_RX,
		.stream_name = LPASS_BE_USB_AUDIO_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(usb_audio_rx),
	},
	{
		.name = LPASS_BE_RT_PROXY_PCM_RX_2,
		.stream_name = LPASS_BE_RT_PROXY_PCM_RX_2,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(proxy_rx2),
	},
	{
		.name = LPASS_BE_USB_AUDIO_TX,
		.stream_name = LPASS_BE_USB_AUDIO_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(usb_audio_tx),
	},
	{
		.name = LPASS_BE_PCM_DUMMY_TX_0,
		.stream_name = LPASS_BE_PCM_DUMMY_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(pcm_dummy_tx0),
	},
	/* Dummy Dev Rx BACK END DAI Link */
	{
		.name = LPASS_BE_PCM_DUMMY_RX_0,
		.stream_name = LPASS_BE_PCM_DUMMY_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(pcm_dummy_rx0),
	},
	/* Dummy Dev Tx BACK END DAI Link */
	{
		.name = LPASS_BE_PCM_DUMMY_TX_1,
		.stream_name = LPASS_BE_PCM_DUMMY_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(pcm_dummy_tx1),
	},
};

static struct snd_soc_dai_link msm_swr_haptics_be_dai_links[] = {
	{
		.name = LPASS_BE_RX_CDC_DMA_RX_6,
		.stream_name = LPASS_BE_RX_CDC_DMA_RX_6,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(rx_dma_rx6),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_RX_4,
		.stream_name = LPASS_BE_WSA_CDC_DMA_RX_4,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_dma_rx4),
	},
};

#ifndef CONFIG_AUDIO_BTFM_PROXY
static struct snd_soc_dai_link msm_wcn_be_dai_links[] = {
	{
		.name = LPASS_BE_SLIMBUS_7_RX,
		.stream_name = LPASS_BE_SLIMBUS_7_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.init = &msm_wcn_init,
		.ops = &msm_common_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(slimbus_7_rx),
	},
	{
		.name = LPASS_BE_SLIMBUS_7_TX,
		.stream_name = LPASS_BE_SLIMBUS_7_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(slimbus_7_tx),
	},
};
#else
static struct snd_soc_dai_link msm_wcn_be_dai_links[] = {
	{
		.name = LPASS_BE_BTFM_PROXY_RX_0,
		.stream_name = LPASS_BE_BTFM_PROXY_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(btfm_0_rx),
	},
	{
		.name = LPASS_BE_BTFM_PROXY_TX_0,
		.stream_name = LPASS_BE_BTFM_PROXY_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(btfm_0_tx),
	},
};
static struct snd_soc_dai_link msm_wcn_be_dai_links_ext[] = {
	{
		.name = LPASS_BE_BTFM_PROXY_RX_1,
		.stream_name = LPASS_BE_BTFM_PROXY_RX_1,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		/* dai link has playback support */
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(btfm_1_rx),
	},
	{
		.name = LPASS_BE_BTFM_PROXY_TX_1,
		.stream_name = LPASS_BE_BTFM_PROXY_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(btfm_1_tx),
	},
};
#endif
static struct snd_soc_dai_link ext_disp_be_dai_link[] = {
	/* DISP PORT BACK END DAI Link for DP0 */
	{
		.name = LPASS_BE_DISPLAY_PORT_RX_0,
		.stream_name = LPASS_BE_DISPLAY_PORT_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(display_port),
	},
	/* DISP PORT BACK END DAI Link for DP1*/
	{
		.name = LPASS_BE_DISPLAY_PORT_RX_1,
		.stream_name = LPASS_BE_DISPLAY_PORT_RX_1,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(display_port1),
	},
};

static struct snd_soc_dai_link ext_hdmi_be_dai_link[] = {
	/* DISP PORT BACK END DAI Link for DP0 */
	{
		.name = LPASS_BE_HDMI_RX_0,
		.stream_name = LPASS_BE_HDMI_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(hdmi_rx),
	},
};

static struct snd_soc_dai_link msm_wsa_cdc_dma_be_dai_links[] = {
	/* WSA CDC DMA Backend DAI Links */
	{
		.name = LPASS_BE_WSA_CDC_DMA_RX_0,
		.stream_name = LPASS_BE_WSA_CDC_DMA_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_dma_rx0),
		.init = &msm_int_wsa_init,
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_RX_1,
		.stream_name = LPASS_BE_WSA_CDC_DMA_RX_1,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_dma_rx1),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_TX_1,
		.stream_name = LPASS_BE_WSA_CDC_DMA_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_dma_tx1),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_TX_0,
		.stream_name = LPASS_BE_WSA_CDC_DMA_TX_0,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		/* .no_host_mode = SND_SOC_DAI_LINK_NO_HOST, */
		SND_SOC_DAILINK_REG(vi_feedback),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_TX_2,
		.stream_name = LPASS_BE_WSA_CDC_DMA_TX_2,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		/* .no_host_mode = SND_SOC_DAI_LINK_NO_HOST, */
		SND_SOC_DAILINK_REG(cps_feedback),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_RX_0_VIRT,
		.stream_name = LPASS_BE_WSA_CDC_DMA_RX_0_VIRT,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_dma_rx0),
		.init = &msm_int_wsa_init,
	},
};

static struct snd_soc_dai_link msm_wsa2_cdc_dma_be_dai_links[] = {
	/* WSA2 CDC DMA Backend DAI Links */
	{
		.name = LPASS_BE_WSA2_CDC_DMA_RX_0,
		.stream_name = LPASS_BE_WSA2_CDC_DMA_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa2_dma_rx0),
		.init = &msm_int_wsa2_init,
	},
	{
		.name = LPASS_BE_WSA2_CDC_DMA_RX_1,
		.stream_name = LPASS_BE_WSA2_CDC_DMA_RX_1,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa2_dma_rx1),
	},
	{
		.name = LPASS_BE_WSA2_CDC_DMA_TX_1,
		.stream_name = LPASS_BE_WSA2_CDC_DMA_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa2_dma_tx1),
	},
	{
		.name = LPASS_BE_WSA2_CDC_DMA_TX_0,
		.stream_name = LPASS_BE_WSA2_CDC_DMA_TX_0,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		/* .no_host_mode = SND_SOC_DAI_LINK_NO_HOST, */
		SND_SOC_DAILINK_REG(wsa2_vi_feedback),
	},
	{
		.name = LPASS_BE_WSA2_CDC_DMA_TX_2,
		.stream_name = LPASS_BE_WSA2_CDC_DMA_TX_2,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		/* .no_host_mode = SND_SOC_DAI_LINK_NO_HOST, */
		SND_SOC_DAILINK_REG(wsa2_cps_feedback),
	},
};

static struct snd_soc_dai_link msm_wsa_wsa2_cdc_dma_be_dai_links[] = {
	/* WSA CDC DMA Backend DAI Links */
	{
		.name = LPASS_BE_WSA_CDC_DMA_RX_0,
		.stream_name = LPASS_BE_WSA_CDC_DMA_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_wsa2_dma_rx0),
		.init = &msm_int_wsa_init,
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_RX_1,
		.stream_name = LPASS_BE_WSA_CDC_DMA_RX_1,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_wsa2_dma_rx1),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_TX_1,
		.stream_name = LPASS_BE_WSA_CDC_DMA_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(wsa_wsa2_dma_tx1),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_TX_0,
		.stream_name = LPASS_BE_WSA_CDC_DMA_TX_0,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		/* .no_host_mode = SND_SOC_DAI_LINK_NO_HOST, */
		SND_SOC_DAILINK_REG(wsa_wsa2_vi_feedback),
	},
	{
		.name = LPASS_BE_WSA_CDC_DMA_TX_2,
		.stream_name = LPASS_BE_WSA_CDC_DMA_TX_2,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		/* .no_host_mode = SND_SOC_DAI_LINK_NO_HOST, */
		SND_SOC_DAILINK_REG(wsa_wsa2_cps_feedback),
	},
};

static struct snd_soc_dai_link msm_rx_cdc_dma_be_dai_links[] = {
	/* RX CDC DMA Backend DAI Links */
	{
		.name = LPASS_BE_RX_CDC_DMA_RX_0,
		.stream_name = LPASS_BE_RX_CDC_DMA_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(rx_dma_rx0),
		.init = &msm_rx_tx_codec_init,
	},
	{
		.name = LPASS_BE_RX_CDC_DMA_RX_1,
		.stream_name = LPASS_BE_RX_CDC_DMA_RX_1,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(rx_dma_rx1),
	},
	{
		.name = LPASS_BE_RX_CDC_DMA_RX_2,
		.stream_name = LPASS_BE_RX_CDC_DMA_RX_2,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(rx_dma_rx2),
	},
	{
		.name = LPASS_BE_RX_CDC_DMA_RX_3,
		.stream_name = LPASS_BE_RX_CDC_DMA_RX_3,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(rx_dma_rx3),
	},
	{
		.name = LPASS_BE_RX_CDC_DMA_RX_5,
		.stream_name = LPASS_BE_RX_CDC_DMA_RX_5,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(rx_dma_rx5),
	},
};

static struct snd_soc_dai_link msm_tx_cdc_dma_be_dai_links[] = {
	/* TX CDC DMA Backend DAI Links */
	{
		.name = LPASS_BE_TX_CDC_DMA_TX_4,
		.stream_name = LPASS_BE_TX_CDC_DMA_TX_4,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(tx_dma_tx4),
	},
};

static struct snd_soc_dai_link msm_cdc_tx_va_dma_be_dai_links[] = {
	{
		.name = LPASS_BE_TX_CDC_DMA_TX_3,
		.stream_name = LPASS_BE_TX_CDC_DMA_TX_3,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(tx_dma_tx3),
	},
	{
		.name = LPASS_BE_VA_CDC_DMA_TX_0,
		.stream_name = LPASS_BE_VA_CDC_DMA_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(va_dma_tx0),
	},
};

static struct snd_soc_dai_link msm_cdc_qmp_dma_be_dai_links[] = {
	{
		.name = LPASS_BE_TX_CDC_DMA_TX_3,
		.stream_name = LPASS_BE_TX_CDC_DMA_TX_3,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(tx_dma_qmp_normal),
	},
	{
		.name = LPASS_BE_VA_CDC_DMA_TX_0,
		.stream_name = LPASS_BE_VA_CDC_DMA_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(va_dma_qmp_normal),
	},
	{
		.name = LPASS_BE_VA_CDC_DMA_TX_1,
		.stream_name = LPASS_BE_VA_CDC_DMA_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(va_dma_qmp_lp),
	},
	{
		.name = LPASS_BE_TX_CDC_DMA_TX_4,
		.stream_name = LPASS_BE_TX_CDC_DMA_TX_4,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(tx_dma_qmp_tx4),
	},
};

static struct snd_soc_dai_link msm_va_cdc_dma_be_dai_links[] = {
	{
		.name = LPASS_BE_VA_CDC_DMA_TX_1,
		.stream_name = LPASS_BE_VA_CDC_DMA_TX_1,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(va_dma_tx1),
	},
	{
		.name = LPASS_BE_VA_CDC_DMA_TX_2,
		.stream_name = LPASS_BE_VA_CDC_DMA_TX_2,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.ops = &msm_common_be_ops,
		SND_SOC_DAILINK_REG(va_dma_tx2),
	},
};

/*
 * I2S interface pinctrl mapping
 * ------------------------------------
 * Primary	- pri_mi2s
 * Secondary	- lpi_i2s3
 * Tertiary	- tert_mi2s
 * Quaternary	- quat_mi2s (lpi_i2s0)
 * Quinary	- lpi_i2s1
 * Senary	- lpi_i2s2
 * ------------------------------------
 */
static struct snd_soc_dai_link msm_mi2s_dai_links[] = {
	{
		.name = LPASS_BE_PRI_MI2S_RX,
		.stream_name = LPASS_BE_PRI_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(pri_mi2s_rx),
	},
	{
		.name = LPASS_BE_PRI_MI2S_TX,
		.stream_name = LPASS_BE_PRI_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(pri_mi2s_tx),
	},
	{
		.name = LPASS_BE_SEC_MI2S_RX,
		.stream_name = LPASS_BE_SEC_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sec_mi2s_rx),
	},
	{
		.name = LPASS_BE_SEC_MI2S_TX,
		.stream_name = LPASS_BE_SEC_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(sec_mi2s_tx),
	},
	{
		.name = LPASS_BE_TERT_MI2S_RX,
		.stream_name = LPASS_BE_TERT_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(tert_mi2s_rx),
	},
	{
		.name = LPASS_BE_TERT_MI2S_TX,
		.stream_name = LPASS_BE_TERT_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(tert_mi2s_tx),
	},
	{
		.name = LPASS_BE_QUAT_MI2S_RX,
		.stream_name = LPASS_BE_QUAT_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(quat_mi2s_rx),
	},
	{
		.name = LPASS_BE_QUAT_MI2S_TX,
		.stream_name = LPASS_BE_QUAT_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(quat_mi2s_tx),
	},
	{
		.name = LPASS_BE_QUIN_MI2S_RX,
		.stream_name = LPASS_BE_QUIN_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(quin_mi2s_rx),
	},
	{
		.name = LPASS_BE_QUIN_MI2S_TX,
		.stream_name = LPASS_BE_QUIN_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(quin_mi2s_tx),
	},
	{
		.name = LPASS_BE_SEN_MI2S_RX,
		.stream_name = LPASS_BE_SEN_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sen_mi2s_rx),
	},
	{
		.name = LPASS_BE_SEN_MI2S_TX,
		.stream_name = LPASS_BE_SEN_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(sen_mi2s_tx),
	},
	{
		.name = LPASS_BE_SEP_MI2S_RX,
		.stream_name = LPASS_BE_SEP_MI2S_RX,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sep_mi2s_rx),
	},
	{
		.name = LPASS_BE_SEP_MI2S_TX,
		.stream_name = LPASS_BE_SEP_MI2S_TX,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(sep_mi2s_tx),
	},
};

static struct snd_soc_dai_link msm_tdm_dai_links[] = {
	{
		.name = LPASS_BE_PRI_TDM_RX_0,
		.stream_name = LPASS_BE_PRI_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(pri_tdm_rx_0),
	},
	{
		.name = LPASS_BE_PRI_TDM_RX_0_VIRT,
		.stream_name = LPASS_BE_PRI_TDM_RX_0_VIRT,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(pri_tdm_rx_0),
	},
	{
		.name = LPASS_BE_PRI_TDM_TX_0,
		.stream_name = LPASS_BE_PRI_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(pri_tdm_tx_0),
	},
	{
		.name = LPASS_BE_SEC_TDM_RX_0,
		.stream_name = LPASS_BE_SEC_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sec_tdm_rx_0),
	},
	{
		.name = LPASS_BE_SEC_TDM_TX_0,
		.stream_name = LPASS_BE_SEC_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(sec_tdm_tx_0),
	},
	{
		.name = LPASS_BE_TERT_TDM_RX_0,
		.stream_name = LPASS_BE_TERT_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(tert_tdm_rx_0),
	},
	{
		.name = LPASS_BE_SEC_TDM_RX_0_VIRT,
		.stream_name = LPASS_BE_SEC_TDM_RX_0_VIRT,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sec_tdm_rx_0),
	},
	{
		.name = LPASS_BE_TERT_TDM_TX_0,
		.stream_name = LPASS_BE_TERT_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(tert_tdm_tx_0),
	},
	{
		.name = LPASS_BE_QUAT_TDM_RX_0,
		.stream_name = LPASS_BE_QUAT_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(quat_tdm_rx_0),
	},
	{
		.name = LPASS_BE_QUAT_TDM_TX_0,
		.stream_name = LPASS_BE_QUAT_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(quat_tdm_tx_0),
	},
	{
		.name = LPASS_BE_QUIN_TDM_RX_0,
		.stream_name = LPASS_BE_QUIN_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(quin_tdm_rx_0),
	},
	{
		.name = LPASS_BE_QUIN_TDM_RX_0_VIRT,
		.stream_name = LPASS_BE_QUIN_TDM_RX_0_VIRT,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(quin_tdm_rx_0),
	},
	{
		.name = LPASS_BE_QUIN_TDM_TX_0,
		.stream_name = LPASS_BE_QUIN_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(quin_tdm_tx_0),
	},
	{
		.name = LPASS_BE_SEN_TDM_RX_0,
		.stream_name = LPASS_BE_SEN_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sen_tdm_rx_0),
	},
	{
		.name = LPASS_BE_SEN_TDM_TX_0,
		.stream_name = LPASS_BE_SEN_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(sen_tdm_tx_0),
	},
	{
		.name = LPASS_BE_SEP_TDM_RX_0,
		.stream_name = LPASS_BE_SEP_TDM_RX_0,
		.playback_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		SND_SOC_DAILINK_REG(sep_tdm_rx_0),
	},
	{
		.name = LPASS_BE_SEP_TDM_TX_0,
		.stream_name = LPASS_BE_SEP_TDM_TX_0,
		.capture_only = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ops = &msm_common_be_ops,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(sep_tdm_tx_0),
	},
};

static struct snd_soc_dai_link msm_sun_dai_links[
			ARRAY_SIZE(msm_wsa_cdc_dma_be_dai_links) +
			ARRAY_SIZE(msm_wsa2_cdc_dma_be_dai_links) +
			ARRAY_SIZE(msm_wsa_wsa2_cdc_dma_be_dai_links) +
			ARRAY_SIZE(msm_rx_cdc_dma_be_dai_links) +
			ARRAY_SIZE(msm_tx_cdc_dma_be_dai_links) +
			ARRAY_SIZE(msm_va_cdc_dma_be_dai_links) +
			ARRAY_SIZE(msm_cdc_tx_va_dma_be_dai_links) +
			ARRAY_SIZE(msm_cdc_qmp_dma_be_dai_links) +
			ARRAY_SIZE(ext_disp_be_dai_link) +
			ARRAY_SIZE(ext_hdmi_be_dai_link) +
			ARRAY_SIZE(msm_common_be_dai_links) +
			ARRAY_SIZE(msm_wcn_be_dai_links) +
			ARRAY_SIZE(msm_swr_haptics_be_dai_links) +
			ARRAY_SIZE(msm_mi2s_dai_links) +
			ARRAY_SIZE(msm_tdm_dai_links)];


static void msm_machine_put_node(void *data)
{
	of_node_put(data);
}

static int msm_machine_get_node(struct device *dev, const char *name,
				struct device_node **node)
{
	int ret;

	*node = of_parse_phandle(dev->of_node, name, 0);
	if (!*node)
		return 0;
	ret = devm_add_action_or_reset(dev, msm_machine_put_node, *node);
	if (ret)
		*node = NULL;
	return ret;
}

static int msm_populate_dai_link_component_of_node(
					struct snd_soc_card *card)
{
	int i, j, index, ret = 0;
	struct device *cdev = card->dev;
	struct snd_soc_dai_link *dai_link = card->dai_link;
	struct device_node *np = NULL;
	int codecs_enabled = 0;
	struct snd_soc_dai_link_component *codecs_comp = NULL;

	if (!cdev) {
		dev_err_ratelimited(cdev, "%s: Sound card device memory NULL\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < card->num_links; i++) {
		if (dai_link[i].init == NULL)
			dai_link[i].init = &msm_common_dai_link_init;

		/* populate codec_of_node for snd card dai links */
		if (dai_link[i].num_codecs > 0) {
			for (j = 0; j < dai_link[i].num_codecs; j++) {
				if (dai_link[i].codecs[j].of_node ||
						!dai_link[i].codecs[j].name)
					continue;

				index = of_property_match_string(cdev->of_node,
						"asoc-codec-names",
						dai_link[i].codecs[j].name);
				if (index < 0)
					continue;
				np = of_parse_phandle(cdev->of_node,
						      "asoc-codec",
						      index);
				if (!np) {
					dev_err_ratelimited(cdev,
						"%s: retrieving phandle for codec %s failed\n",
						__func__,
						dai_link[i].codecs[j].name);
					ret = -ENODEV;
					goto err;
				}
				ret = devm_add_action_or_reset(cdev, msm_machine_put_node, np);
				if (ret)
					goto err;
				dai_link[i].codecs[j].of_node = np;
				dai_link[i].codecs[j].name = NULL;
			}
		}
	}

	/* In multi-codec scenario, check if codecs are enabled for this platform */
	for (i = 0; i < card->num_links; i++) {
		codecs_enabled = 0;
		if (dai_link[i].num_codecs > 1) {
			for (j = 0; j < dai_link[i].num_codecs; j++) {
				if (!dai_link[i].codecs[j].of_node)
					continue;

				np = dai_link[i].codecs[j].of_node;
				if (!of_device_is_available(np)) {
				    dev_err_ratelimited(cdev, "%s: codec is disabled: %s\n",
								__func__,
								np->full_name);
							dai_link[i].codecs[j].of_node = NULL;
							continue;
				}

				codecs_enabled++;
			}
			if (codecs_enabled > 0 &&
				    codecs_enabled < dai_link[i].num_codecs) {
				codecs_comp = devm_kzalloc(cdev,
				    sizeof(struct snd_soc_dai_link_component)
				    * codecs_enabled, GFP_KERNEL);
				if (!codecs_comp) {
					dev_err_ratelimited(cdev,
						"%s: %s dailink codec component alloc failed\n",
						__func__, dai_link[i].name);
					ret = -ENOMEM;
					goto err;
				}
				index = 0;
				for (j = 0; j < dai_link[i].num_codecs; j++) {
					if(dai_link[i].codecs[j].of_node) {
						codecs_comp[index].of_node =
						  dai_link[i].codecs[j].of_node;
						codecs_comp[index].dai_name =
						  dai_link[i].codecs[j].dai_name;
						codecs_comp[index].name = NULL;
						index++;
					}
				}
				dai_link[i].codecs = codecs_comp;
				dai_link[i].num_codecs = codecs_enabled;
			}
		}
	}

err:
	return ret;
}

static int msm_audrx_stub_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static int msm_snd_stub_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	return 0;
}

static struct snd_soc_ops msm_stub_be_ops = {
	.hw_params = msm_snd_stub_hw_params,
};

struct snd_soc_card snd_soc_card_stub_msm = {
	.name		= "sun-stub-snd-card",
};

static struct snd_soc_dai_link msm_stub_be_dai_links[] = {
	/* Backend DAI Links */
	{
		.name = LPASS_BE_PRI_AUXPCM_RX,
		.stream_name = LPASS_BE_PRI_AUXPCM_RX,
		.playback_only = 1,
		.init = &msm_audrx_stub_init,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm_stub_be_ops,
		SND_SOC_DAILINK_REG(auxpcm_rx),
	},
	{
		.name = LPASS_BE_PRI_AUXPCM_TX,
		.stream_name = LPASS_BE_PRI_AUXPCM_TX,
		.capture_only = 1,
		.ignore_suspend = 1,
		.ops = &msm_stub_be_ops,
		SND_SOC_DAILINK_REG(auxpcm_tx),
	},
};

static struct snd_soc_dai_link msm_stub_dai_links[
			 ARRAY_SIZE(msm_stub_be_dai_links)];

static const struct of_device_id sun_asoc_machine_of_match[]  = {
	{ .compatible = "qcom,sun-asoc-snd",
	  .data = "codec"},
	{ .compatible = "qcom,sun-asoc-snd-stub",
	  .data = "stub_codec"},
	{},
};

static int msm_snd_card_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_component *component = NULL;
	struct snd_soc_pcm_runtime *rtd;
	struct msm_asoc_mach_data *pdata;
	int ret = 0;
	void *mbhc_calibration;
	char wcd_name[20];

	pdata = snd_soc_card_get_drvdata(card);
	if (!pdata)
		return -EINVAL;

	if (pdata->wcd_disabled)
		return 0;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	if (!rtd) {
		dev_err(card->dev,
			"%s: snd_soc_get_pcm_runtime for %s failed!\n",
			__func__, card->dai_link[0].name);
		return -EINVAL;
	}

	if (pdata->wcd_used == WCD9378_DEV_INDEX)
		strscpy(wcd_name, WCD9378_DRV_NAME, sizeof(WCD9378_DRV_NAME));
	else
		strscpy(wcd_name, WCD939X_DRV_NAME, sizeof(WCD939X_DRV_NAME));
	component = snd_soc_rtdcom_lookup(rtd, wcd_name);
	if (!component) {
		pr_err("%s component is NULL\n", __func__);
		return -EINVAL;
	}

	mbhc_calibration = def_wcd_mbhc_cal();
	if (!mbhc_calibration)
		return -ENOMEM;
	pdata->mbhc_cfg.calibration = mbhc_calibration;

#if IS_ENABLED(CONFIG_QCOM_WCD_USBSS_I2C)
	if (of_find_property(card->dev->of_node,
				"qcom,usbss-hsj-connect-enabled", NULL))
		pdata->mbhc_cfg.usbss_hsj_connect_enable = true;
#endif

	pdata->mbhc_component = component;
	switch (pdata->wcd_used) {
	case WCD9378_DEV_INDEX:
		ret = wcd9378_mbhc_hs_detect(component, &pdata->mbhc_cfg);
		break;
	case WCD939X_DEV_INDEX:
		ret = wcd939x_mbhc_hs_detect(component, &pdata->mbhc_cfg);
		break;
	default:
		ret = -EINVAL;
		goto err_hs_detect;
	}
	if (ret) {
		dev_err(component->dev, "%s: mbhc hs detect failed, err:%d\n",
			__func__, ret);
		goto err_hs_detect;
	}
	return 0;

err_hs_detect:
	msm_rx_tx_codec_exit(rtd);
	return ret;
}

static void msm_rx_tx_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct msm_asoc_mach_data *pdata = snd_soc_card_get_drvdata(rtd->card);

	if (!pdata)
		return;
	if (pdata->mbhc_component) {
		if (pdata->wcd_used == WCD9378_DEV_INDEX)
			wcd9378_mbhc_hs_detect_exit(pdata->mbhc_component);
		else if (pdata->wcd_used == WCD939X_DEV_INDEX)
			wcd939x_mbhc_hs_detect_exit(pdata->mbhc_component);
		pdata->mbhc_component = NULL;
	}
	kfree(pdata->mbhc_cfg.calibration);
	pdata->mbhc_cfg.calibration = NULL;
	pdata->codec_root = NULL;
	msm_common_snd_card_reset();
}

static int msm_append_dai_links(struct snd_soc_dai_link *dest, size_t capacity,
				unsigned int *count, const struct snd_soc_dai_link *src, size_t num)
{
	if (*count > capacity || num > capacity - *count)
		return -E2BIG;
	memcpy(dest + *count, src, num * sizeof(*src));
	*count += num;
	return 0;
}

/* Callers pass fixed arrays; ARRAY_SIZE does not evaluate source. */
#define MSM_ADD_LINKS(count, source) \
	msm_append_dai_links(msm_sun_dai_links, ARRAY_SIZE(msm_sun_dai_links), \
			     (count), (source), ARRAY_SIZE(source))

static struct snd_soc_card *populate_snd_card_dailinks(struct device *dev, int wsa_max_devs)
{
	struct snd_soc_card *card = NULL;
	struct snd_soc_dai_link *dailink = NULL;
	unsigned int total_links = 0;
	int rc = 0;
	u32 val = 0;
	const struct of_device_id *match;
	uint32_t platform_id, build_minor, hw_id;
	int i, j;

	match = of_match_node(sun_asoc_machine_of_match, dev->of_node);
	if (!match) {
		dev_err_ratelimited(dev, "%s: No DT match found for sound card\n",
			__func__);
		return NULL;
	}

	if (!strcmp(match->data, "codec")) {
		card = &snd_soc_card_sun_msm;

		/* late probe uses dai link at index '0' to get wcd component */
		memcpy(msm_sun_dai_links + total_links,
		       msm_rx_cdc_dma_be_dai_links,
		       sizeof(msm_rx_cdc_dma_be_dai_links));
		total_links +=
			ARRAY_SIZE(msm_rx_cdc_dma_be_dai_links);

		switch (wsa_max_devs) {
		case MONO_SPEAKER:
		case STEREO_SPEAKER:
			if (MSM_ADD_LINKS(&total_links, msm_wsa_cdc_dma_be_dai_links))
				return ERR_PTR(-E2BIG);
			break;
		case QUAD_SPEAKER:
			if (of_find_property(dev->of_node,
					"qcom,dedicated-wsa2", NULL)) {
				if (MSM_ADD_LINKS(&total_links, msm_wsa_cdc_dma_be_dai_links))
					return ERR_PTR(-E2BIG);

				if (MSM_ADD_LINKS(&total_links, msm_wsa2_cdc_dma_be_dai_links))
					return ERR_PTR(-E2BIG);
			} else {
				if (MSM_ADD_LINKS(&total_links, msm_wsa2_cdc_dma_be_dai_links))
					return ERR_PTR(-E2BIG);

				if (MSM_ADD_LINKS(&total_links, msm_wsa_wsa2_cdc_dma_be_dai_links))
					return ERR_PTR(-E2BIG);
			}
			break;
		default:
			dev_dbg(dev,
				"%s: Unexpected number of WSAs, wsa_max_devs: %d\n",
				__func__, wsa_max_devs);
			break;
		}

		if (MSM_ADD_LINKS(&total_links, msm_common_be_dai_links))
			return ERR_PTR(-E2BIG);

		rc = of_property_read_u32(dev->of_node,
				"qcom,mi2s-audio-intf", &val);
		if (!rc && val) {
			if (MSM_ADD_LINKS(&total_links, msm_mi2s_dai_links))
				return ERR_PTR(-E2BIG);
		}

		rc = of_property_read_u32(dev->of_node,
				"qcom,tdm-audio-intf", &val);
		if (!rc && val) {
			if (MSM_ADD_LINKS(&total_links, msm_tdm_dai_links))
				return ERR_PTR(-E2BIG);
		}

		rc = of_property_read_u32(dev->of_node,
					   "qcom,ext-disp-audio-rx", &val);
		if (!rc && val) {
			if (!socinfo_get_part_info(PART_DISPLAY)) {
				dev_dbg(dev, "%s(): ext disp audio support present\n",
					__func__);
				if (MSM_ADD_LINKS(&total_links, ext_disp_be_dai_link))
					return ERR_PTR(-E2BIG);
			}
		}

		rc = of_property_read_u32(dev->of_node,
					   "qcom,ext-hdmi-audio-rx", &val);
		if (!rc && val) {
			if (!socinfo_get_part_info(PART_DISPLAY)) {
				dev_dbg(dev, "%s(): ext hdmi audio support present\n",
					__func__);
				if (MSM_ADD_LINKS(&total_links, ext_hdmi_be_dai_link))
					return ERR_PTR(-E2BIG);
			}
		}

		rc = of_property_read_u32(dev->of_node, "qcom,wcn-bt", &val);
		if (!rc && val) {
			dev_dbg(dev, "%s(): WCN BT support present\n",
				__func__);
			if (MSM_ADD_LINKS(&total_links, msm_wcn_be_dai_links))
				return ERR_PTR(-E2BIG);
		}

		rc = of_property_read_u32(dev->of_node, "qcom,wcn-bt-ext", &val);
		if (!rc && val) {
			dev_dbg(dev, "%s(): WCN BT EXT support present\n",
				__func__);
			if (MSM_ADD_LINKS(&total_links, msm_wcn_be_dai_links_ext))
				return ERR_PTR(-E2BIG);
		}

		rc = of_property_read_u32(dev->of_node, "qcom,qmp-mic", &val);
		if (!rc && val) {
			dev_dbg(dev, "%s(): QMP MIC support present\n",
				__func__);
			if (MSM_ADD_LINKS(&total_links, msm_cdc_qmp_dma_be_dai_links))
				return ERR_PTR(-E2BIG);
		} else {
			if (MSM_ADD_LINKS(&total_links, msm_cdc_tx_va_dma_be_dai_links))
				return ERR_PTR(-E2BIG);
			if (MSM_ADD_LINKS(&total_links, msm_tx_cdc_dma_be_dai_links))
				return ERR_PTR(-E2BIG);
			if (MSM_ADD_LINKS(&total_links, msm_va_cdc_dma_be_dai_links))
				return ERR_PTR(-E2BIG);
		}

		if (of_find_property(dev->of_node, "swr-haptics-unsupported",
			NULL)) {
			dev_dbg(dev, "%s(): swr haptics support not present\n", __func__);
		} else {
			if (MSM_ADD_LINKS(&total_links, msm_swr_haptics_be_dai_links))
				return ERR_PTR(-E2BIG);

			if (wsa_max_devs != QUAD_SPEAKER) {
				if (of_find_property(dev->of_node, "qcom,dedicated-wsa2",
					NULL)) {
					if (MSM_ADD_LINKS(&total_links,
							  msm_wsa2_cdc_dma_be_dai_links))
						return ERR_PTR(-E2BIG);
				} else {
					dev_err_ratelimited(dev, "%s:Unable to active haptics path\n",
						__func__);
				}
			}
		}

		dailink = msm_sun_dai_links;
	} else if(!strcmp(match->data, "stub_codec")) {
		card = &snd_soc_card_stub_msm;

		memcpy(msm_stub_dai_links,
		       msm_stub_be_dai_links,
		       sizeof(msm_stub_be_dai_links));

		dailink = msm_stub_dai_links;
		total_links = ARRAY_SIZE(msm_stub_be_dai_links);
	}

	platform_id = get_hw_version_platform();
	build_minor = get_hw_version_minor();
	hw_id = get_hw_id_value();
	/* For o2 p0 */
	if ((platform_id == HARDWARE_PROJECT_O2) && (build_minor == 0) && (hw_id == 0)) {
		for (i = 0; i < total_links; i ++) {
			if (dailink[i].num_codecs > 0) {
				for (j = 0; j < dailink[i].num_codecs; j ++) {
					if (!dailink[i].codecs[j].name)
						continue;
					if (!strcmp(dailink[i].codecs[j].name, "cs35l43.0-0040")) {
						dailink[i].num_codecs -= 1;
						dev_dbg(dev, "%s(): codec dai : %s, num_codecs: %d \n",
							__func__, dailink[i].codecs[j].name, dailink[i].num_codecs);
						break;
					}
				}
			}
		}
	}

	if (card) {
		card = devm_kmemdup(dev, card, sizeof(*card), GFP_KERNEL);
		if (!card)
			return ERR_PTR(-ENOMEM);
		dailink = devm_kmemdup(dev, dailink, total_links * sizeof(*dailink), GFP_KERNEL);
		if (!dailink)
			return ERR_PTR(-ENOMEM);
		for (i = 0; i < total_links; i++) {
			struct snd_soc_dai_link *link = &dailink[i];

			if (link->num_cpus) {
				size_t size = link->num_cpus * sizeof(*link->cpus);

				link->cpus = devm_kmemdup(dev, link->cpus,
							  size, GFP_KERNEL);
				if (!link->cpus)
					return ERR_PTR(-ENOMEM);
			}
			if (link->num_platforms) {
				size_t size = link->num_platforms * sizeof(*link->platforms);

				link->platforms = devm_kmemdup(dev, link->platforms,
							       size, GFP_KERNEL);
				if (!link->platforms)
					return ERR_PTR(-ENOMEM);
			}
			if (link->num_codecs) {
				size_t size = link->num_codecs * sizeof(*link->codecs);

				link->codecs = devm_kmemdup(dev, link->codecs,
							    size, GFP_KERNEL);
				if (!link->codecs)
					return ERR_PTR(-ENOMEM);
			}
			if (link->init == msm_rx_tx_codec_init)
				link->exit = msm_rx_tx_codec_exit;
		}
		card->dai_link = dailink;
		card->num_links = total_links;
		if (!strcmp(match->data, "codec"))
			card->late_probe = msm_snd_card_late_probe;
	}

	return card;
}

static int msm_int_wsa883x_init(struct snd_soc_pcm_runtime *rtd)
{
	u8 spkleft_ports[WSA883X_MAX_SWR_PORTS] = {0, 1, 2, 3};
	u8 spkright_ports[WSA883X_MAX_SWR_PORTS] = {0, 1, 2, 3};
	u8 spkleft_port_types[WSA883X_MAX_SWR_PORTS] = {SPKR_L, SPKR_L_COMP,
		SPKR_L_BOOST, SPKR_L_VI};
	u8 spkright_port_types[WSA883X_MAX_SWR_PORTS] = {SPKR_R, SPKR_R_COMP,
		SPKR_R_BOOST, SPKR_R_VI};
	unsigned int ch_rate[WSA883X_MAX_SWR_PORTS] = {SWR_CLK_RATE_2P4MHZ, SWR_CLK_RATE_0P6MHZ,
		SWR_CLK_RATE_0P3MHZ, SWR_CLK_RATE_1P2MHZ};
	unsigned int ch_mask[WSA883X_MAX_SWR_PORTS] = {0x1, 0xF, 0x3, 0x3};
	struct snd_soc_component *component = NULL;
	struct msm_asoc_mach_data *pdata =
		snd_soc_card_get_drvdata(rtd->card);

	if (pdata->wsa_max_devs > 0) {
		component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.1");
		if (!component) {
			pr_err("%s: wsa-codec.1 component is NULL\n", __func__);
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.2");
			if (!component) {
				pr_err("%s: wsa-codec.2 component is NULL\n", __func__);
				return -EINVAL;
			}
			wsa883x_set_channel_map(component, &spkright_ports[0],
					WSA883X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkright_port_types[0]);

			wsa883x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		} else {

			wsa883x_set_channel_map(component, &spkleft_ports[0],
					WSA883X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkleft_port_types[0]);

			wsa883x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		}
	}

	/* If current platform has more than one WSA */
	if (pdata->wsa_max_devs > 1) {
		component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.2");
		if (!component) {
			pr_err("%s: wsa-codec.2 component is NULL\n", __func__);
			return -EINVAL;
		}

		wsa883x_set_channel_map(component, &spkright_ports[0],
				WSA883X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0], &spkright_port_types[0]);

		wsa883x_codec_info_create_codec_entry(pdata->codec_root,
				component);
	}

	if (!pdata->dedicated_wsa2) {
		if (pdata->wsa_max_devs > 2) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.3");
			if (!component) {
				pr_err("%s: wsa-codec.3 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa883x_set_channel_map(component, &spkleft_ports[0],
					WSA883X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkleft_port_types[0]);

			wsa883x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		}

		if (pdata->wsa_max_devs > 3) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.4");
			if (!component) {
				pr_err("%s: wsa-codec.4 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa883x_set_channel_map(component, &spkright_ports[0],
					WSA883X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkright_port_types[0]);

			wsa883x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		}
	}
	msm_common_dai_link_init(rtd);

	return 0;
}

static int msm_int_wsa884x_init(struct snd_soc_pcm_runtime *rtd)
{
	u8 spkleft_ports[WSA884X_MAX_SWR_PORTS] = {0, 1, 2, 3, 4, 5};
	u8 spkright_ports[WSA884X_MAX_SWR_PORTS] = {0, 1, 2, 3, 4, 5};
	u8 spkleft_port_types[WSA884X_MAX_SWR_PORTS] = {SPKR_L, SPKR_L_COMP,
						SPKR_L_BOOST, PBR, SPKR_L_VI, CPS};
	u8 spkright_port_types[WSA884X_MAX_SWR_PORTS] = {SPKR_R, SPKR_R_COMP,
						SPKR_R_BOOST, PBR, SPKR_R_VI, CPS};
	unsigned int ch_rate[WSA884X_MAX_SWR_PORTS] = {SWR_CLK_RATE_2P4MHZ, SWR_CLK_RATE_0P6MHZ,
							SWR_CLK_RATE_0P3MHZ, SWR_CLK_RATE_48KHZ,
							SWR_CLK_RATE_1P2MHZ, SWR_CLK_RATE_24KHZ};
	unsigned int ch_mask[WSA884X_MAX_SWR_PORTS] = {0x1, 0xF, 0x3, 0x1, 0x3, 0x3};


	struct snd_soc_component *component = NULL;
	struct msm_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(rtd->card);

	if (pdata->wsa_max_devs > 0) {
		component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.1");
		if (!component) {
			pr_err("%s: wsa-codec.1 component is NULL\n", __func__);
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.2");
			if (!component) {
				pr_err("%s: wsa-codec.2 component is NULL\n", __func__);
				return -EINVAL;
			}
			wsa884x_set_channel_map(component, &spkright_ports[0],
					WSA884X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkright_port_types[0]);

			wsa884x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		} else {
			wsa884x_set_channel_map(component, &spkleft_ports[0],
				WSA884X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0], &spkleft_port_types[0]);

			wsa884x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		}
	}

	/* If current platform has more than one WSA */
	if (pdata->wsa_max_devs > 1) {
		component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.2");
		if (!component) {
			pr_err("%s: wsa-codec.2 component is NULL\n", __func__);
			return -EINVAL;
		}

		wsa884x_set_channel_map(component, &spkright_ports[0],
			WSA884X_MAX_SWR_PORTS, &ch_mask[0],
			&ch_rate[0], &spkright_port_types[0]);

		wsa884x_codec_info_create_codec_entry(pdata->codec_root,
			component);
	}

	if (!pdata->dedicated_wsa2) {
		if (pdata->wsa_max_devs > 2) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.3");
			if (!component) {
				pr_err("%s: wsa-codec.3 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa884x_set_channel_map(component, &spkleft_ports[0],
				WSA884X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0], &spkleft_port_types[0]);

			wsa884x_codec_info_create_codec_entry(pdata->codec_root,
				component);
		}

		if (pdata->wsa_max_devs > 3) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.4");
			if (!component) {
				pr_err("%s: wsa-codec.4 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa884x_set_channel_map(component, &spkright_ports[0],
				WSA884X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0], &spkright_port_types[0]);

			wsa884x_codec_info_create_codec_entry(pdata->codec_root,
				component);
		}
	}

	msm_common_dai_link_init(rtd);

	return 0;
}

static int msm_int_wsa_init(struct snd_soc_pcm_runtime *rtd)
{
	if (strstr(rtd->card->name, "wsa883x"))
		return msm_int_wsa883x_init(rtd);

	return msm_int_wsa884x_init(rtd);
}

static int msm_int_wsa883x_2_init(struct snd_soc_pcm_runtime *rtd)
{
	u8 spkleft_ports[WSA883X_MAX_SWR_PORTS] = {0, 1, 2, 3};
	u8 spkright_ports[WSA883X_MAX_SWR_PORTS] = {0, 1, 2, 3};
	u8 spkleft_port_types[WSA883X_MAX_SWR_PORTS] = {SPKR_L, SPKR_L_COMP,
							SPKR_L_BOOST, SPKR_L_VI};
	u8 spkright_port_types[WSA883X_MAX_SWR_PORTS] = {SPKR_R, SPKR_R_COMP,
							SPKR_R_BOOST, SPKR_R_VI};
	unsigned int ch_rate[WSA883X_MAX_SWR_PORTS] = {SWR_CLK_RATE_2P4MHZ, SWR_CLK_RATE_0P6MHZ,
						SWR_CLK_RATE_0P3MHZ, SWR_CLK_RATE_1P2MHZ};
	unsigned int ch_mask[WSA883X_MAX_SWR_PORTS] = {0x1, 0xF, 0x3, 0x3};
	struct snd_soc_component *component = NULL;
	struct msm_asoc_mach_data *pdata =
			snd_soc_card_get_drvdata(rtd->card);

	if (pdata->dedicated_wsa2) {
		if (pdata->wsa_max_devs > 2) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.3");
			if (!component) {
				pr_err("%s: wsa-codec.3 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa883x_set_channel_map(component, &spkleft_ports[0],
					WSA883X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkleft_port_types[0]);

			wsa883x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		}

		if (pdata->wsa_max_devs > 3) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.4");
			if (!component) {
				pr_err("%s: wsa-codec.4 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa883x_set_channel_map(component, &spkright_ports[0],
					WSA883X_MAX_SWR_PORTS, &ch_mask[0],
					&ch_rate[0], &spkright_port_types[0]);

			wsa883x_codec_info_create_codec_entry(pdata->codec_root,
					component);
		}

		msm_common_dai_link_init(rtd);
	}

	return 0;
}

static int msm_int_wsa884x_2_init(struct snd_soc_pcm_runtime *rtd)
{
	u8 spkleft_ports[WSA884X_MAX_SWR_PORTS] = {0, 1, 2, 3, 4, 5};
	u8 spkright_ports[WSA884X_MAX_SWR_PORTS] = {0, 1, 2, 3, 4, 5};
	u8 spkleft_port_types[WSA884X_MAX_SWR_PORTS] = {SPKR_L, SPKR_L_COMP,
						SPKR_L_BOOST, PBR, SPKR_L_VI, CPS};
	u8 spkright_port_types[WSA884X_MAX_SWR_PORTS] = {SPKR_R, SPKR_R_COMP,
						SPKR_R_BOOST, PBR, SPKR_R_VI, CPS};
	unsigned int ch_rate[WSA884X_MAX_SWR_PORTS] = {SWR_CLK_RATE_2P4MHZ, SWR_CLK_RATE_0P6MHZ,
							SWR_CLK_RATE_0P3MHZ, SWR_CLK_RATE_48KHZ,
							SWR_CLK_RATE_1P2MHZ, SWR_CLK_RATE_24KHZ};
	unsigned int ch_mask[WSA884X_MAX_SWR_PORTS] = {0x1, 0xF, 0x3, 0x1, 0x3, 0x3};


	struct snd_soc_component *component = NULL;
	struct msm_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(rtd->card);

	if (pdata->dedicated_wsa2) {
		if (pdata->wsa_max_devs > 2) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.3");
			if (!component) {
				pr_err("%s: wsa-codec.3 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa884x_set_channel_map(component, &spkleft_ports[0],
				WSA884X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0], &spkleft_port_types[0]);

			wsa884x_codec_info_create_codec_entry(pdata->codec_root,
				component);
		}

		if (pdata->wsa_max_devs > 3) {
			component = snd_soc_rtdcom_lookup(rtd, "wsa-codec.4");
			if (!component) {
				pr_err("%s: wsa-codec.4 component is NULL\n", __func__);
				return -EINVAL;
			}

			wsa884x_set_channel_map(component, &spkright_ports[0],
				WSA884X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0], &spkright_port_types[0]);

			wsa884x_codec_info_create_codec_entry(pdata->codec_root,
				component);
		}

		msm_common_dai_link_init(rtd);
	}

	return 0;
}

static int msm_int_wsa2_init(struct snd_soc_pcm_runtime *rtd)
{
	if (strstr(rtd->card->name, "wsa883x"))
		return msm_int_wsa883x_2_init(rtd);

	return msm_int_wsa884x_2_init(rtd);
}

static int msm_rx_tx_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int codec_variant = -1;
	struct snd_soc_component *component = NULL;
	struct snd_soc_component *lpass_cdc_component = NULL;
	struct snd_soc_dapm_context *dapm = NULL;
	struct snd_info_entry *entry = NULL;
	struct snd_card *card = NULL;
	struct msm_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(rtd->card);
	int ret = 0;

	lpass_cdc_component = snd_soc_rtdcom_lookup(rtd, "lpass-cdc");
	if (!lpass_cdc_component) {
		pr_err("%s: could not find component for lpass-cdc\n",
			__func__);
		return ret;
	}

	ret = snd_soc_add_component_controls(lpass_cdc_component, msm_common_snd_controls,
                                             ARRAY_SIZE(msm_common_snd_controls));
	if (ret < 0) {
		pr_err("%s: add common snd controls failed: %d\n",__func__, ret);
		return ret;
	}

	dapm = snd_soc_component_get_dapm(lpass_cdc_component);

	snd_soc_dapm_new_controls(dapm, msm_int_dapm_widgets,
				ARRAY_SIZE(msm_int_dapm_widgets));

	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic0");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic1");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic2");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic3");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic4");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic5");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic6");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic7");

	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic1");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic2");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic3");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic4");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic5");

	lpass_cdc_set_port_map(lpass_cdc_component, ARRAY_SIZE(sm_port_map), sm_port_map);

	card = rtd->card->snd_card;
	if (!pdata->codec_root) {
		entry = msm_snd_info_create_subdir(card->module, "codecs",
						 card->proc_root);
		if (!entry) {
			pr_debug("%s: Cannot create codecs module entry\n",
				 __func__);
			return ret;
		}
		pdata->codec_root = entry;
	}
	lpass_cdc_info_create_codec_entry(pdata->codec_root, lpass_cdc_component);
	lpass_cdc_register_wake_irq(lpass_cdc_component, false);

	if (pdata->wcd_disabled)
		goto done;

	component = snd_soc_rtdcom_lookup(rtd, WCD939X_DRV_NAME);
	if (!component) {
		pr_err("%s could not find component for %s\n",
			__func__, WCD939X_DRV_NAME);
		component = snd_soc_rtdcom_lookup(rtd, WCD9378_DRV_NAME);
		if (!component) {
			pr_err("%s component is NULL\n", __func__);
			return -EINVAL;
		}
		pdata->wcd_used = WCD9378_DEV_INDEX;
	} else {
		pdata->wcd_used = WCD939X_DEV_INDEX;
	}
	dapm = snd_soc_component_get_dapm(component);
	card = component->card->snd_card;

	snd_soc_dapm_ignore_suspend(dapm, "EAR");
	if (pdata->wcd_used != WCD939X_DEV_INDEX)
		snd_soc_dapm_ignore_suspend(dapm, "AUX");
	snd_soc_dapm_ignore_suspend(dapm, "HPHL");
	snd_soc_dapm_ignore_suspend(dapm, "HPHR");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC4");
	snd_soc_dapm_sync(dapm);

	pdata = snd_soc_card_get_drvdata(component->card);
	if (!pdata->codec_root) {
		entry = msm_snd_info_create_subdir(card->module, "codecs",
						 card->proc_root);
		if (!entry) {
			dev_dbg(component->dev, "%s: Cannot create codecs module entry\n",
				 __func__);
			 return 0;
		}
		pdata->codec_root = entry;
	}
	if (pdata->wcd_used == WCD9378_DEV_INDEX) {
		wcd9378_info_create_codec_entry(pdata->codec_root, component);
	} else {
		wcd939x_info_create_codec_entry(pdata->codec_root, component);
		codec_variant = wcd939x_get_codec_variant(component);
		dev_dbg(component->dev, "%s: variant %d\n", __func__, codec_variant);
	}
	if (codec_variant == WCD9395)
		ret = lpass_cdc_rx_set_fir_capability(lpass_cdc_component, true);
	else
		ret = lpass_cdc_rx_set_fir_capability(lpass_cdc_component, false);

	if (ret < 0) {
		dev_err_ratelimited(component->dev, "%s: set fir capability failed: %d\n",
			__func__, ret);
		return ret;
	}
done:
	msm_common_dai_link_init(rtd);

	return ret;
}

static int sun_ssr_enable(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct snd_soc_pcm_runtime *rtd = NULL, *rtd_wcd = NULL, *rtd_wsa = NULL;
	struct msm_asoc_mach_data *pdata = NULL;
	int ret = 0;

	if (!card) {
		dev_err_ratelimited(dev, "%s: card is NULL\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	if (!strcmp(card->name, "sun-stub-snd-card")) {
		/* TODO */
		dev_dbg(dev, "%s: TODO \n", __func__);
	}

	snd_card_notify_user(SND_CARD_STATUS_ONLINE);
	dev_dbg(dev, "%s: setting snd_card to ONLINE\n", __func__);

	pdata = snd_soc_card_get_drvdata(card);
	if (!pdata) {
		dev_dbg(dev, "%s: pdata is NULL \n", __func__);
		goto err;
	}
	rtd_wcd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	if (!rtd_wcd) {
		dev_dbg(dev,
			"%s: snd_soc_get_pcm_runtime for %s failed!\n",
			__func__, card->dai_link[0].name);
	}

	if (pdata->wsa_max_devs > 0) {
		rtd_wsa = snd_soc_get_pcm_runtime(card,
			&card->dai_link[ARRAY_SIZE(msm_rx_cdc_dma_be_dai_links) - 1]);
		if (!rtd_wsa) {
			dev_dbg(dev,
			"%s: snd_soc_get_pcm_runtime for %s failed!\n",
			__func__, card->dai_link[ARRAY_SIZE(msm_rx_cdc_dma_be_dai_links) - 1].name);
		}
	}
	/* set UPD configuration */
	if(!pdata->upd_config.backend_used) {
		dev_dbg(dev,
		"%s: upd- backend_used is NULL\n", __func__);
		goto err;
	}
	if (!strcmp(pdata->upd_config.backend_used, "wsa")) {
		if (!rtd_wsa)
			goto err;
		else
			rtd = rtd_wsa;
	} else if(!strcmp(pdata->upd_config.backend_used, "wcd")) {
		if (!rtd_wcd &&!pdata->wcd_disabled)
			goto err;
		else
			rtd = rtd_wcd;
	} else {
		dev_err_ratelimited(card->dev, "%s: Invalid backend to set UPD config\n",
			__func__);
		goto err;
	}

	msm_set_upd_config(rtd);

err:
	return ret;
}

static void sun_ssr_disable(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	if (!card) {
		dev_err_ratelimited(dev, "%s: card is NULL\n", __func__);
		return;
	}

	dev_dbg(dev, "%s: setting snd_card to OFFLINE\n", __func__);
	snd_card_notify_user(SND_CARD_STATUS_OFFLINE);

	if (!strcmp(card->name, "sun-stub-snd-card")) {
		/* TODO */
		dev_dbg(dev, "%s: TODO \n", __func__);
	}
}

static const struct snd_event_ops sun_ssr_ops = {
	.enable = sun_ssr_enable,
	.disable = sun_ssr_disable,
};

static int msm_audio_ssr_compare(struct device *dev, void *data)
{
	struct device_node *node = data;

	dev_dbg(dev, "%s: dev->of_node = 0x%p, node = 0x%p\n",
		__func__, dev->of_node, node);
	return (dev->of_node && dev->of_node == node);
}

static int msm_audio_ssr_register(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);
	struct msm_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	struct snd_event_clients *clients = NULL;
	struct device_node *node;
	int ret, i;

	for (i = 0; ; i++) {
		node = of_parse_phandle(dev->of_node, "qcom,msm_audio_ssr_devs", i);
		if (!node)
			break;
		ret = devm_add_action_or_reset(dev, msm_machine_put_node, node);
		if (ret)
			goto free_clients;
		snd_event_mstr_add_client(&clients, msm_audio_ssr_compare, node);
		if (IS_ERR(clients)) {
			ret = PTR_ERR(clients);
			goto free_clients;
		}
	}
	ret = snd_event_master_register(dev, &sun_ssr_ops, clients, NULL);
	if (ret)
		goto free_clients;
	pdata->ssr_registered = true;
	return snd_event_notify(dev, SND_EVENT_UP);
free_clients:
	snd_event_clients_free(clients);
	return ret;
}

struct msm_common_pdata *msm_common_get_pdata(struct snd_soc_card *card)
{
	struct msm_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);

	if (!pdata)
		return NULL;

	return pdata->common_pdata;
}

void msm_common_set_pdata(struct snd_soc_card *card,
			  struct msm_common_pdata *common_pdata)
{
	struct msm_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);

	if (!pdata)
		return;

	pdata->common_pdata = common_pdata;
}

static int smartpa_type_index(const char *name)
{
	int i;

	if (!name)
		return -EINVAL;
	for (i = 0; i < SMARTPA_MAX; i++)
		if (!strcmp(name, smartpa_cust_name[i]))
			return i;
	return -EINVAL;
}

bool check_smartpa_type(const char *name)
{
	bool match;

	mutex_lock(&smartpa_lock);
	match = name && (!smartpa_num || !strcmp(smartpa_type, name));
	mutex_unlock(&smartpa_lock);
	return match;
}
EXPORT_SYMBOL_GPL(check_smartpa_type);

int get_smartpa_type(void)
{
	int type;

	mutex_lock(&smartpa_lock);
	type = smartpa_num;
	mutex_unlock(&smartpa_lock);
	return type;
}
EXPORT_SYMBOL_GPL(get_smartpa_type);

int set_smartpa_type(const char *name, unsigned int size)
{
	char type[PA_NAME_MAX];
	size_t len;
	int index, ret = 0;

	if (!name || !size || size > sizeof(type))
		return -1;
	len = strnlen(name, size);
	if (!len || len >= sizeof(type))
		return -1;
	memcpy(type, name, len);
	type[len] = '\0';
	index = smartpa_type_index(type);
	if (index < 0)
		return -1;
	mutex_lock(&smartpa_lock);
	if (smartpa_count && index != smartpa_num) {
		ret = -1;
	} else {
		smartpa_num = index;
		strscpy(smartpa_type, type, sizeof(smartpa_type));
	}
	mutex_unlock(&smartpa_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(set_smartpa_type);

static char *fmt_single_name(struct device *dev, int *id)
{
	char *found, *name, *formatted;
	unsigned int id1, id2;

	if (!dev_name(dev))
		return NULL;
	name = kstrdup(dev_name(dev), GFP_KERNEL);
	if (!name)
		return NULL;
	*id = 0;
	found = strstr(name, dev->driver->name);
	if (found) {
		if (sscanf(&found[strlen(dev->driver->name)], ".%d", id) == 1 && *id == -1)
			found[strlen(dev->driver->name)] = '\0';
	} else if (sscanf(name, "%x-%x", &id1, &id2) == 2) {
		*id = ((id1 & 0xffff) << 16) + id2;
		formatted = kasprintf(GFP_KERNEL, "%s.%s", dev->driver->name, name);
		kfree(name);
		name = formatted;
	}
	return name;
}

int register_smartpa_codec(struct device *dev, const char *dai_name,
			   const char *type)
{
	char *name, *dai;
	int id, index, ret = 0;
	unsigned int i;

	if (!dev || !dev->driver || !dai_name || !*dai_name)
		return -EINVAL;
	index = smartpa_type_index(type);
	if (index < 0)
		return index;
	name = fmt_single_name(dev, &id);
	if (!name)
		return -ENOMEM;
	dai = kstrdup(dai_name, GFP_KERNEL);
	if (!dai) {
		kfree(name);
		return -ENOMEM;
	}
	mutex_lock(&smartpa_lock);
	if (smartpa_num && smartpa_num != index) {
		ret = -ENODEV;
		goto out;
	}
	for (i = 0; i < smartpa_count; i++) {
		if (smartpa_devices[i] != dev)
			continue;
		if (strcmp(smartpa_dails[i].name, name) ||
		    strcmp(smartpa_dails[i].dai_name, dai))
			ret = -EEXIST;
		goto out;
	}
	if (smartpa_count == SMARTPA_MAX_NUM) {
		ret = -ENOSPC;
		goto out;
	}
	smartpa_devices[smartpa_count] = get_device(dev);
	smartpa_dails[smartpa_count].name = name;
	smartpa_dails[smartpa_count].dai_name = dai;
	smartpa_count++;
	smartpa_num = index;
	strscpy(smartpa_type, type, sizeof(smartpa_type));
	name = NULL;
	dai = NULL;
out:
	mutex_unlock(&smartpa_lock);
	kfree(name);
	kfree(dai);
	return ret;
}
EXPORT_SYMBOL_GPL(register_smartpa_codec);

void unregister_smartpa_codec(struct device *dev)
{
	struct device *held = NULL;
	unsigned int i;

	mutex_lock(&smartpa_lock);
	for (i = 0; i < smartpa_count; i++) {
		if (smartpa_devices[i] != dev)
			continue;
		held = smartpa_devices[i];
		kfree(smartpa_dails[i].name);
		kfree(smartpa_dails[i].dai_name);
		smartpa_count--;
		memmove(&smartpa_dails[i], &smartpa_dails[i + 1],
			(smartpa_count - i) * sizeof(smartpa_dails[0]));
		memmove(&smartpa_devices[i], &smartpa_devices[i + 1],
			(smartpa_count - i) * sizeof(smartpa_devices[0]));
		memset(&smartpa_dails[smartpa_count], 0, sizeof(smartpa_dails[0]));
		smartpa_devices[smartpa_count] = NULL;
		break;
	}
	if (!smartpa_count) {
		smartpa_num = SMARTPA_NONE;
		strscpy(smartpa_type, "none", sizeof(smartpa_type));
	}
	mutex_unlock(&smartpa_lock);
	put_device(held);
}
EXPORT_SYMBOL_GPL(unregister_smartpa_codec);

void set_smartpa_codec(struct device *dev, const char *dai_name)
{
	char type[PA_NAME_MAX];
	int ret;

	mutex_lock(&smartpa_lock);
	strscpy(type, smartpa_type, sizeof(type));
	mutex_unlock(&smartpa_lock);
	ret = register_smartpa_codec(dev, dai_name, type);
	if (ret)
		pr_err("%s: codec registration failed: %d\n", __func__, ret);
}
EXPORT_SYMBOL_GPL(set_smartpa_codec);

static unsigned int smartpa_dt_count(const char *compatible)
{
	struct device_node *node;
	unsigned int count = 0;

	for_each_compatible_node(node, NULL, compatible) {
		struct device_node *parent = of_node_get(node);
		bool available = true;

		while (parent) {
			struct device_node *next;

			if (!of_device_is_available(parent)) {
				available = false;
				of_node_put(parent);
				break;
			}
			next = of_get_parent(parent);
			of_node_put(parent);
			parent = next;
		}
		if (available)
			count++;
	}
	return count;
}

static bool smartpa_ready(struct snd_soc_card *card)
{
	unsigned int aw, sia, needed;
	u32 tdm = 0;

	of_property_read_u32(card->dev->of_node, "qcom,tdm-audio-intf", &tdm);
	if (!tdm)
		return true;
	aw = smartpa_dt_count("awinic,aw882xx_smartpa");
	sia = smartpa_dt_count("si,sia91xx-i2c");
	if (!aw && !sia)
		return true;
	if (smartpa_num == SMARTPA_AW882XX)
		needed = aw;
	else if (smartpa_num == SMARTPA_SIA91XX)
		needed = sia;
	else
		return false;
	return needed && smartpa_count >= needed;
}

static int smartpa_dai_link_select(struct snd_soc_card *card)
{
	struct snd_soc_dai_link_component *codecs;
	struct snd_soc_dai_link *link;
	unsigned int i, j;
	int ret = 0;

	mutex_lock(&smartpa_lock);
	if (!smartpa_ready(card)) {
		ret = -EPROBE_DEFER;
		goto out;
	}
	if (!smartpa_count)
		goto out;
	for (i = 0; i < card->num_links; i++) {
		link = &card->dai_link[i];
		if (strcmp(link->name, tdm_rx_dailink_name[tdm_rx0_id]) &&
		    strcmp(link->name, tdm_rx_dailink_name[tdm_rx1_id]) &&
		    strcmp(link->name, tdm_tx_dailink_name[tdm_tx_id]))
			continue;
		codecs = devm_kcalloc(card->dev, smartpa_count, sizeof(*codecs), GFP_KERNEL);
		if (!codecs) {
			ret = -ENOMEM;
			goto out;
		}
		for (j = 0; j < smartpa_count; j++) {
			codecs[j].name = devm_kstrdup(card->dev, smartpa_dails[j].name, GFP_KERNEL);
			codecs[j].dai_name = devm_kstrdup(card->dev,
							  smartpa_dails[j].dai_name,
							  GFP_KERNEL);
			if (!codecs[j].name || !codecs[j].dai_name) {
				ret = -ENOMEM;
				goto out;
			}
		}
		link->codecs = codecs;
		link->num_codecs = smartpa_count;
	}
out:
	mutex_unlock(&smartpa_lock);
	return ret;
}

static ssize_t smartpa_type_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buff)
{
	ssize_t ret;

	mutex_lock(&smartpa_lock);
	ret = sysfs_emit(buff, "%s\n", smartpa_type);
	mutex_unlock(&smartpa_lock);
	return ret;
}

static struct kobj_attribute smartpa_type_attr =
	__ATTR(smartpa_type, 0644, smartpa_type_show, NULL);

static struct kobject *smartpa_kobj;

static int smartpa_sysfs_init(void)
{
	int ret;

	smartpa_kobj = kobject_create_and_add("smartpa", kernel_kobj);
	if (!smartpa_kobj)
		return -ENOMEM;
	ret = sysfs_create_file(smartpa_kobj, &smartpa_type_attr.attr);
	if (ret) {
		kobject_put(smartpa_kobj);
		smartpa_kobj = NULL;
	}
	return ret;
}

static void smartpa_sysfs_exit(void)
{
	if (!smartpa_kobj)
		return;
	sysfs_remove_file(smartpa_kobj, &smartpa_type_attr.attr);
	kobject_put(smartpa_kobj);
	smartpa_kobj = NULL;
}

static void msm_asoc_machine_cleanup(struct snd_soc_card *card)
{
	struct msm_asoc_mach_data *pdata = snd_soc_card_get_drvdata(card);
	int ret;

	if (pdata->ssr_registered) {
		snd_event_master_deregister(card->dev);
		pdata->ssr_registered = false;
	}
	if (pdata->card_registered) {
		snd_soc_unregister_card(card);
		pdata->card_registered = false;
	}
	msm_common_snd_deinit(pdata->common_pdata);
	pdata->common_pdata = NULL;
	if (pdata->dmic_enabled) {
		ret = msm_cdc_pinctrl_select_sleep_state(pdata->dmic_enable_gpio);
		if (ret)
			dev_err(card->dev, "DMIC shutdown failed: %d\n", ret);
		pdata->dmic_enabled = false;
	}
	snd_card_set_card_status(SND_CARD_STATUS_OFFLINE);
}

static int msm_machine_get_nodes(struct device *dev, struct msm_asoc_mach_data *pdata)
{
	struct {
		const char *name;
		struct device_node **node;
	} props[] = {
		{ "fsa4480-i2c-handle", &pdata->fsa_handle },
		{ "wcd939x-i2c-handle", &pdata->wcd_usbss_handle },
		{ "qcom,cdc-dmic01-gpios", &pdata->dmic01_gpio_p },
		{ "qcom,cdc-dmic23-gpios", &pdata->dmic23_gpio_p },
		{ "qcom,cdc-dmic45-gpios", &pdata->dmic45_gpio_p },
		{ "qcom,cdc-dmic67-gpios", &pdata->dmic67_gpio_p },
	};
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = msm_machine_get_node(dev, props[i].name, props[i].node);
		if (ret)
			return ret;
	}
	if (pdata->wcd_disabled)
		return msm_machine_get_node(dev, "qcom,cdc-dmic-enable-gpios",
					    &pdata->dmic_enable_gpio);
	return 0;
}

static int msm_asoc_machine_probe(struct platform_device *pdev)
{
	struct msm_asoc_mach_data *pdata;
	struct snd_soc_card *card = NULL;
	const struct of_device_id *match;
	struct device_node *dmic_nodes[4];
	unsigned int i;
	int ret;
	const char *stage = "card allocation";

	if (!pdev->dev.of_node)
		return -EINVAL;
	mutex_lock(&machine_probe_lock);
	if (machine_device) {
		ret = -EBUSY;
		goto unlock;
	}
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		ret = -ENOMEM;
		goto unlock;
	}
	pdata->mbhc_cfg = wcd_mbhc_defaults;
	of_property_read_u32(pdev->dev.of_node, "qcom,wcd-disabled", &pdata->wcd_disabled);
	of_property_read_u32(pdev->dev.of_node, "qcom,wsa-max-devs", &pdata->wsa_max_devs);
	pdata->dedicated_wsa2 = of_property_read_bool(pdev->dev.of_node, "qcom,dedicated-wsa2");
	card = populate_snd_card_dailinks(&pdev->dev, pdata->wsa_max_devs);
	if (IS_ERR_OR_NULL(card)) {
		ret = card ? PTR_ERR(card) : -EINVAL;
		goto unlock;
	}
	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, pdata);
	match = of_match_node(sun_asoc_machine_of_match, pdev->dev.of_node);
	if (!match) {
		ret = -EINVAL;
		goto cleanup;
	}
	stage = "card name";
	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret)
		goto cleanup;
	if (!strcmp(match->data, "codec")) {
		stage = "audio routing";
		ret = snd_soc_of_parse_audio_routing(card, "qcom,audio-routing");
		if (ret)
			goto cleanup;
	}
	stage = "amplifier selection";
	ret = smartpa_dai_link_select(card);
	if (ret)
		goto cleanup;
	stage = "DAI device nodes";
	ret = msm_populate_dai_link_component_of_node(card);
	if (ret)
		goto cleanup;
	msm_parse_upd_configuration(pdev, pdata);
	stage = "pinctrl device nodes";
	ret = msm_machine_get_nodes(&pdev->dev, pdata);
	if (ret)
		goto cleanup;
	if (pdata->fsa_handle || pdata->wcd_usbss_handle)
		pdata->mbhc_cfg.swap_gnd_mic = msm_usbc_swap_gnd_mic;
	if (pdata->wcd_disabled && pdata->dmic_enable_gpio) {
		stage = "DMIC pin activation";
		ret = msm_cdc_pinctrl_select_active_state(pdata->dmic_enable_gpio);
		if (ret)
			goto cleanup;
		pdata->dmic_enabled = true;
	}
	dmic_nodes[0] = pdata->dmic01_gpio_p;
	dmic_nodes[1] = pdata->dmic23_gpio_p;
	dmic_nodes[2] = pdata->dmic45_gpio_p;
	dmic_nodes[3] = pdata->dmic67_gpio_p;
	for (i = 0; i < ARRAY_SIZE(dmic_nodes); i++)
		if (dmic_nodes[i])
			msm_cdc_pinctrl_set_wakeup_capable(dmic_nodes[i], false);
	stage = "LPASS audio clock";
	pdata->lpass_audio_hw_vote = devm_clk_get(&pdev->dev, "lpass_audio_hw_vote");
	if (IS_ERR(pdata->lpass_audio_hw_vote)) {
		ret = PTR_ERR(pdata->lpass_audio_hw_vote);
		pdata->lpass_audio_hw_vote = NULL;
		if (ret == -EPROBE_DEFER)
			goto cleanup;
	}
	stage = "common card state";
	ret = msm_common_snd_init(pdev, card);
	if (ret)
		goto cleanup;
	/* All callbacks can now use common state and clock/pinctrl ownership. */
	stage = "ASoC card registration";
	ret = snd_soc_register_card(card);
	if (ret)
		goto cleanup;
	pdata->card_registered = true;
	stage = "SSR clients";
	ret = msm_audio_ssr_register(&pdev->dev);
	if (ret)
		goto cleanup;
	machine_device = &pdev->dev;
	snd_card_set_card_status(SND_CARD_STATUS_ONLINE);
	mutex_unlock(&machine_probe_lock);
	return 0;
cleanup:
	ret = dev_err_probe(&pdev->dev, ret, "%s failed\n", stage);
	msm_asoc_machine_cleanup(card);
	platform_set_drvdata(pdev, NULL);
unlock:
	mutex_unlock(&machine_probe_lock);
	return ret;
}

static void msm_asoc_machine_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	mutex_lock(&machine_probe_lock);
	if (card)
		msm_asoc_machine_cleanup(card);
	platform_set_drvdata(pdev, NULL);
	machine_device = NULL;
	mutex_unlock(&machine_probe_lock);
}

static struct platform_driver sun_asoc_machine_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = sun_asoc_machine_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = msm_asoc_machine_probe,
	.remove = msm_asoc_machine_remove,
};

static int __init msm_asoc_machine_init(void)
{
	int ret;

	ret = snd_card_sysfs_init();
	if (ret)
		return ret;
	ret = smartpa_sysfs_init();
	if (ret)
		goto card_exit;
	ret = platform_driver_register(&sun_asoc_machine_driver);
	if (ret) {
		smartpa_sysfs_exit();
		goto card_exit;
	}
	return 0;
card_exit:
	snd_card_sysfs_deinit();
	return ret;
}
module_init(msm_asoc_machine_init);

static void __exit msm_asoc_machine_exit(void)
{
	platform_driver_unregister(&sun_asoc_machine_driver);
	smartpa_sysfs_exit();
	snd_card_sysfs_deinit();
	/* Native callers are module dependencies and have already stopped. */
	while (smartpa_count)
		unregister_smartpa_codec(smartpa_devices[0]);
}
module_exit(msm_asoc_machine_exit);

#ifndef CONFIG_AUDIO_BTFM_PROXY
MODULE_SOFTDEP("pre: bt_fm_slim");
#else
MODULE_SOFTDEP("pre: btfmcodec");
#endif
MODULE_DESCRIPTION("ALSA SoC msm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, sun_asoc_machine_of_match);
