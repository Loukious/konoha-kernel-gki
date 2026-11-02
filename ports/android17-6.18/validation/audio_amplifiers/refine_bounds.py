from pathlib import Path
B=Path('/tmp/onyx-audio-amplifiers-handoff/private-source/qcom/opensource/audio-kernel/asoc/codecs')
p=B/'sia91xx/sipa.c';s=p.read_text();a=s.index('static int sipa_pinctrl_select');z=s.index('static int sipa_probe',a)
s=s[:a]+'''static int sipa_select_state(struct pinctrl *pinctrl, const char *name)
{
	struct pinctrl_state *state = pinctrl_lookup_state(pinctrl, name);

	if (IS_ERR(state))
		return PTR_ERR(state);
	return pinctrl_select_state(pinctrl, state);
}

static int sipa_pinctrl_select(struct platform_device *pdev, sipa_dev_t *si_pa)
{
	struct pinctrl *pinctrl;
	int ret;

	if (si_pa->disable_pin)
		return 0;
	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);
	ret = sipa_select_state(pinctrl, "si_pa_gpio");
	if (ret)
		return ret;
	if (IS_DIGITAL_PA_TYPE(si_pa->chip_type)) {
		if (si_pa->chip_type == CHIP_TYPE_SIA5118 && gpio_is_valid(si_pa->boost_pin)) {
			ret = sipa_select_state(pinctrl, "si_pa_boost");
			if (ret)
				return ret;
			ret = gpio_direction_output(si_pa->boost_pin, SIA81XX_DISABLE_LEVEL);
			if (ret)
				return ret;
		}
		if (gpio_is_valid(si_pa->irq_pin)) {
			ret = sipa_select_state(pinctrl, "si_pa_irq");
			if (ret)
				return ret;
			ret = devm_gpio_request_one(&pdev->dev, si_pa->irq_pin,
						    GPIOF_IN, "SIA91XX_INT");
			if (ret)
				return ret;
		}
	}
	if (si_pa->en_dyn_id) {
		ret = sipa_select_state(pinctrl, "si_pa_id");
		if (ret)
			return ret;
		ret = gpio_direction_input(si_pa->id_pin);
		if (ret)
			return ret;
	}
	if (IS_SUPPORT_OWI_TYPE(si_pa->chip_type))
		return gpio_direction_output(si_pa->owi_pin, OWI_POLARITY);
	return 0;
}

'''+s[z:];p.write_text(s)
p=B/'sia91xx/sipa_regmap.c';s=p.read_text();a=s.index('struct regmap *sipa_regmap_init');z=s.index('void sipa_regmap_remove',a);x=s[a:z];x=x.replace('return NULL;', 'return ERR_PTR(-EINVAL);');s=s[:a]+x+s[z:];p.write_text(s)
p=B/'aw882xx/aw882xx_calib.c';s=p.read_text();s=s.replace('"dev_sel:%s"', '"dev_sel:%49s"');s=s.replace('\tkernel_buf = kzalloc(size + 1, GFP_KERNEL);','\tif (!size || size > PAGE_SIZE)\n\t\treturn -EINVAL;\n\tkernel_buf = kzalloc(size + 1, GFP_KERNEL);');p.write_text(s)
