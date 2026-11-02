"""Applied once: wire SIA native registration/unwind, firmware and IRQ ownership."""
from pathlib import Path
B=Path('/tmp/onyx-audio-amplifiers-handoff/private-source/qcom/opensource/audio-kernel/asoc/codecs/sia91xx')
def rep(s,a,b):
 assert a in s,a[:100];return s.replace(a,b)
p=B/'sipa.c';s=p.read_text()
s=rep(s,'\t\tsia91xx_component_probe(component);','\t\treturn sia91xx_component_probe(component);')
s=rep(s,'static void sipa_component_remove(struct snd_soc_component *component)\n{\n\tpr_info("[debug][%s] %s: running \\n", LOG_FLAG, __func__);\n\treturn;\n}', '''static void sipa_component_remove(struct snd_soc_component *component)
{
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);

	if (si_pa->irq >= 0) {
		free_irq(si_pa->irq, si_pa);
		si_pa->irq = -1;
	}
	if (si_pa->interrupt_initialized) {
		cancel_delayed_work_sync(&si_pa->interrupt_work);
		si_pa->interrupt_initialized = false;
	}
}''')
s=rep(s,'if ((NULL != of_node) && (NULL != si_pa->pdev)) {','if (si_pa->ready && si_pa->pdev) {')
s=rep(s,'snprintf(si_pa->name, strlen("sipa-spk.%u"), "sipa-spk.%u", i);','snprintf(si_pa->name, sizeof(si_pa->name), "sipa-spk.%u", i);')
a=s.index('static void put_sipa_dev(');z=s.index('static unsigned int get_chip_type',a)
s=s[:a]+'''static void put_sipa_dev(sipa_dev_t *si_pa)
{
	int i;

	if (!si_pa)
		return;
	mutex_lock(&sipa_mutex);
	if (si_pa->pdev || si_pa->client)
		goto out;
	for (i = 0; i < ARRAY_SIZE(g_sipa_dev); i++) {
		if (si_pa == g_sipa_dev[i]) {
			g_sipa_dev[i] = NULL;
			if (g_default_sia_dev == si_pa)
				g_default_sia_dev = NULL;
			kfree(si_pa);
			break;
		}
	}
out:
	mutex_unlock(&sipa_mutex);
}

'''+s[z:]
# All sysfs attributes are established by the owning probe, after discovery.
s=s.replace('\t\t\tdevice_create_file(&si_pa->pdev->dev,\n\t\t\t\t\t   &dev_attr_sipa_device);','\t\t\tsi_pa->device_available = true;')
s=s.replace('\t\t\t\tdevice_create_file(&si_pa->pdev->dev,\n\t\t\t\t\t\t   &dev_attr_sipa_device);','\t\t\t\tsi_pa->device_available = true;')
a=s.index('#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))\nint sipa_i2c_probe');z=s.index('void sipa_i2c_shutdown(',a)
s=s[:a]+'''static void sipa_remove_files(sipa_dev_t *si_pa)
{
	if (si_pa->command_created) {
		device_remove_file(&si_pa->pdev->dev, &dev_attr_sipa_cmd);
		si_pa->command_created = false;
	}
#ifdef DISTINGUISH_CHIP_TYPE
	if (si_pa->device_created) {
		device_remove_file(&si_pa->pdev->dev, &dev_attr_sipa_device);
		si_pa->device_created = false;
	}
#endif
}

static int sipa_create_files(sipa_dev_t *si_pa)
{
	int ret;

#ifdef DISTINGUISH_CHIP_TYPE
	if (si_pa->device_available && !si_pa->device_created) {
		ret = device_create_file(&si_pa->pdev->dev, &dev_attr_sipa_device);
		if (ret)
			return ret;
		si_pa->device_created = true;
	}
#endif
	ret = device_create_file(&si_pa->pdev->dev, &dev_attr_sipa_cmd);
	if (ret) {
		sipa_remove_files(si_pa);
		return ret;
	}
	si_pa->command_created = true;
	return 0;
}

int sipa_i2c_probe(struct i2c_client *client)
{
	struct device_node *node;
	struct snd_soc_dai_driver *dai;
	sipa_dev_t *si_pa;
	struct device_link *link;
	int ret, addr_offset = 0, addr;
	u16 original_addr = client->addr;
	unsigned int value;

	if (!check_smartpa_type("sia91xx"))
		return -ENODEV;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;
	node = of_parse_phandle(client->dev.of_node, "si,sipa-dev", 0);
	if (!node)
		return -EINVAL;
	si_pa = find_sipa_dev(node);
	of_node_put(node);
	if (!si_pa)
		return -EPROBE_DEFER;
	if (si_pa->client)
		return -EBUSY;
	if (si_pa->chip_type == CHIP_TYPE_UNKNOWN)
		return -ENODEV;
	link = device_link_add(&client->dev, &si_pa->pdev->dev,
			       DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!link)
		return -ENOMEM;

	of_property_read_s32(client->dev.of_node, "si,sipa-addr_offset", &addr_offset);
	if (addr_offset < -(int)original_addr || addr_offset > 0x7f - original_addr) {
		ret = -EINVAL;
		goto err_link;
	}
	addr = original_addr + addr_offset;
	client->addr = addr;
	si_pa->original_addr = original_addr;
	si_pa->client = client;
	dev_set_drvdata(&client->dev, si_pa);
	si_pa->regmap = sipa_regmap_init(client, si_pa->channel_num,
				       get_one_available_chip_type(si_pa->chip_type));
	if (IS_ERR(si_pa->regmap)) {
		ret = PTR_ERR(si_pa->regmap);
		si_pa->regmap = NULL;
		goto err_client;
	}
	if (si_pa->chip_type == CHIP_TYPE_SIA8152X) {
		ret = regmap_read(si_pa->regmap, 0x00, &value);
		if (ret < 0) {
			/* Regmap is managed by this I2C device; retain it to teardown. */
			if (client->addr < 0x30) {
				ret = -EINVAL;
				goto err_client;
			}
			client->addr -= 0x30;
			si_pa->regmap = sipa_regmap_init(client, si_pa->channel_num,
				get_one_available_chip_type(si_pa->chip_type));
			if (IS_ERR(si_pa->regmap)) {
				ret = PTR_ERR(si_pa->regmap);
				si_pa->regmap = NULL;
				goto err_client;
			}
			ret = regmap_read(si_pa->regmap, 0x00, &value);
			if (ret)
				goto err_client;
		}
	}
	if (gpio_is_valid(si_pa->rst_pin)) {
		ret = devm_gpio_request_one(&client->dev, si_pa->rst_pin,
					    GPIOF_OUT_INIT_LOW, "SIA91XX_RST");
		if (ret) {
			if (ret == -EBUSY)
				ret = -EPROBE_DEFER;
			goto err_client;
		}
	}
	if (si_pa->en_compatible_type)
		sipa_compatible_chips_adapt(si_pa);
	if (IS_DIGITAL_PA_TYPE(si_pa->chip_type)) {
		ret = sia91xx_detect_chip(si_pa);
		if (ret < 0)
			goto err_client;
		if (gpio_is_valid(si_pa->spksw_gpio)) {
			ret = devm_gpio_request_one(&client->dev, si_pa->spksw_gpio,
						    GPIOF_OUT_INIT_LOW, "sia91xx_spksw");
			if (ret)
				goto err_client;
		}
		dai = devm_kmemdup(&client->dev, sia91xx_dai, sizeof(sia91xx_dai), GFP_KERNEL);
		if (!dai) {
			ret = -ENOMEM;
			goto err_client;
		}
		ret = sia91xx_append_i2c_address(&client->dev, client, NULL, 0,
						 dai, ARRAY_SIZE(sia91xx_dai));
		if (ret)
			goto err_client;
		ret = snd_soc_register_component(&client->dev, &soc_component_dev_sipa,
						 dai, ARRAY_SIZE(sia91xx_dai));
		if (ret)
			goto err_client;
		si_pa->component_registered = true;
		ret = register_smartpa_codec(&client->dev, dai[0].name, "sia91xx");
		if (ret)
			goto err_component;
	}
	if (si_pa->chip_type != CHIP_TYPE_UNKNOWN &&
	    !IS_SUPPORT_OWI_TYPE(si_pa->chip_type)) {
		ret = sipa_create_files(si_pa);
		if (ret)
			goto err_registry;
		sipa_param_load_fw(&client->dev, "sipa.bin");
	}
	if (si_pa->chip_type == CHIP_TYPE_SIA8101 && !si_pa->channel_num)
		g_default_sia_dev = si_pa;
	return 0;
err_registry:
	unregister_smartpa_codec(&client->dev);
err_component:
	if (si_pa->component_registered) {
		snd_soc_unregister_component(&client->dev);
		si_pa->component_registered = false;
	}
err_client:
	si_pa->regmap = NULL;
	si_pa->client = NULL;
	dev_set_drvdata(&client->dev, NULL);
	client->addr = original_addr;
err_link:
	device_link_del(link);
	return ret;
}
EXPORT_SYMBOL(sipa_i2c_probe);

void sipa_i2c_remove(struct i2c_client *client)
{
	sipa_dev_t *si_pa = dev_get_drvdata(&client->dev);

	if (!si_pa)
		return;
	unregister_smartpa_codec(&client->dev);
	sipa_remove_files(si_pa);
	sipa_param_stop(si_pa);
	if (si_pa->component_registered) {
		snd_soc_unregister_component(&client->dev);
		si_pa->component_registered = false;
	}
	if (g_default_sia_dev == si_pa)
		g_default_sia_dev = NULL;
	si_pa->regmap = NULL;
	si_pa->client = NULL;
	client->addr = si_pa->original_addr;
	dev_set_drvdata(&client->dev, NULL);
	put_sipa_dev(si_pa);
}
EXPORT_SYMBOL(sipa_i2c_remove);

'''+s[z:]
# Preserve explicit pin errors/defer and retain selected managed pinctrl states.
s=rep(s,'if (NULL == si_pa_pinctrl)', 'if (IS_ERR(si_pa_pinctrl))')
s=rep(s,'\t\t\treturn -ENODEV;\n\t\t}\n\n\t\t/* get owi gpio', '\t\t\treturn PTR_ERR(si_pa_pinctrl);\n\t\t}\n\n\t\t/* get owi gpio')
s=s.replace('if (NULL == pinctrl_state)', 'if (IS_ERR(pinctrl_state))')
s=s.replace('ret = -ENODEV;\n\t\t\tgoto err;', 'ret = PTR_ERR(pinctrl_state);\n\t\t\tgoto err;') # followup review select error branches
s=rep(s,'err:\n\tif (0 == si_pa->disable_pin) {\n\t\tdevm_pinctrl_put(si_pa_pinctrl);\n\t}\n\treturn ret;', 'err:\n\treturn ret;')
s=rep(s,'\tsi_pa->channel_num = (uint32_t)channel_num;', '\tif (channel_num < 0 || channel_num >= SIPA_CHANNEL_NUM)\n\t\treturn -EINVAL;\n\tsi_pa->channel_num = (uint32_t)channel_num;')
# GPIO zero is valid; absent GPIOs must begin invalid.
for n in ['rst_pin','boost_pin','id_pin','irq_pin','spksw_gpio','owi_pin']:
 s=s.replace('int '+n+' = 0;', 'int '+n+' = -1;')
a=s.index('static int sipa_probe(');z=s.index('static void sipa_shutdown',a)
s=s[:a]+'''static int sipa_probe(struct platform_device *pdev)
{
	sipa_dev_t *si_pa;
	int ret;

	if (!check_smartpa_type("sia91xx"))
		return -ENODEV;
	si_pa = create_sipa_dev(&pdev->dev);
	if (!si_pa)
		return -ENOMEM;
	si_pa->pdev = pdev;
	si_pa->irq = -1;
	si_pa->rst_pin = -1;
	si_pa->irq_pin = -1;
	si_pa->spksw_gpio = -1;
	dev_set_drvdata(&pdev->dev, si_pa);
	spin_lock_init(&si_pa->owi_lock);
	spin_lock_init(&si_pa->rst_lock);
	sipa_param_init(si_pa);
	ret = sipa_property_init(pdev->dev.of_node, si_pa);
	if (ret)
		goto err_device;
	ret = sipa_pinctrl_select(pdev, si_pa);
	if (ret)
		goto err_device;
	si_pa->sipa_wq = alloc_ordered_workqueue("sipa_%u", 0, si_pa->channel_num);
	if (!si_pa->sipa_wq) {
		ret = -ENOMEM;
		goto err_device;
	}
	if (!IS_DIGITAL_PA_TYPE(si_pa->chip_type)) {
		ret = snd_soc_register_component(&pdev->dev, &soc_component_dev_sipa, NULL, 0);
		if (ret)
			goto err_work;
		si_pa->platform_component_registered = true;
	}
	if (si_pa->en_dyn_id)
		distinguish_chip_type(si_pa);
	if (IS_SUPPORT_OWI_TYPE(si_pa->chip_type)) {
		ret = sipa_create_files(si_pa);
		if (ret)
			goto err_component;
	}
	mutex_lock(&sipa_mutex);
	si_pa->ready = true;
	mutex_unlock(&sipa_mutex);
	if (IS_SUPPORT_OWI_TYPE(si_pa->chip_type))
		sipa_param_load_fw(&pdev->dev, "sipa.bin");
	return 0;
err_component:
	if (si_pa->platform_component_registered)
		snd_soc_unregister_component(&pdev->dev);
err_work:
	destroy_workqueue(si_pa->sipa_wq);
err_device:
	dev_set_drvdata(&pdev->dev, NULL);
	si_pa->pdev = NULL;
	put_sipa_dev(si_pa);
	return ret;
}

static void sipa_remove(struct platform_device *pdev)
{
	sipa_dev_t *si_pa = dev_get_drvdata(&pdev->dev);

	if (!si_pa)
		return;
	mutex_lock(&sipa_mutex);
	si_pa->ready = false;
	mutex_unlock(&sipa_mutex);
	sipa_remove_files(si_pa);
	sipa_param_stop(si_pa);
	if (si_pa->platform_component_registered)
		snd_soc_unregister_component(&pdev->dev);
	sipa_suspend(si_pa);
	destroy_workqueue(si_pa->sipa_wq);
	si_pa->sipa_wq = NULL;
	si_pa->pdev = NULL;
	dev_set_drvdata(&pdev->dev, NULL);
	put_sipa_dev(si_pa);
}

'''+s[z:]
s=rep(s,'\t\t       LOG_FLAG, __func__, ret);\n\t\treturn ret;\n\t}\n\n\treturn 0;\n}\n\nstatic void __exit sipa_pa_exit', '\t\t       LOG_FLAG, __func__, ret);\n\t\tplatform_driver_unregister(&si_sipa_dev_driver);\n\t\tsipa_param_release();\n\t\treturn ret;\n\t}\n\n\treturn 0;\n}\n\nstatic void __exit sipa_pa_exit')
s=rep(s,'\tplatform_driver_unregister(&si_sipa_dev_driver);\n}\n\nmodule_init', '\tplatform_driver_unregister(&si_sipa_dev_driver);\n\tsipa_param_release();\n}\n\nmodule_init')
# TLV size is checked before even the fixed header/payload access.
s=rep(s,'\tif (copy_from_user(regval, data, REG_TOTAL_LEN)) {','\tif (size < REG_TOTAL_LEN)\n\t\treturn -EINVAL;\n\tif (copy_from_user(regval, data, REG_TOTAL_LEN)) {')
p.write_text(s)
p=B/'sipa_common.h';s=p.read_text();s=rep(s,'\tbool device_created;', '\tbool device_created;\n\tbool device_available;\n\tbool platform_component_registered;\n\tu16 original_addr;');s=rep(s,'void sipa_i2c_shutdown(', 'int sipa_i2c_probe(struct i2c_client *client);\nvoid sipa_i2c_remove(struct i2c_client *client);\nvoid sipa_i2c_shutdown(');p.write_text(s)
p=B/'sipa_91xx.c';s=p.read_text();a=s.index('static void sia91xx_register_interrupt(');z=s.index('#if 0',a)
s=s[:a]+'''static int sia91xx_register_interrupt(sipa_dev_t *si_pa)
{
	int irq, ret;

	if (!gpio_is_valid(si_pa->irq_pin))
		return 0;
	irq = gpio_to_irq(si_pa->irq_pin);
	if (irq < 0)
		return irq;
	ret = request_irq(irq, sia91xx_irq, IRQF_TRIGGER_RISING,
			  si_pa->channel_num ? "sia91xx_R" : "sia91xx_L", si_pa);
	if (ret)
		return ret;
	si_pa->irq = irq;
	return 0;
}

'''+s[z:]
a=s.index('int sia91xx_component_probe(');z=s.index('\n#else\nint sia91xx_codec_probe',a);x=s[a:z]
x=rep(x,'\tsipa_dev_t *si_pa = NULL;', '\tsipa_dev_t *si_pa = NULL;\n\tint ret;')
x=rep(x,'\t//INIT_DELAYED_WORK(&si_pa->monitor_work, sia91xx_monitor);\n\tsia91xx_register_interrupt(si_pa);','\tsi_pa->interrupt_initialized = true;')
x=rep(x,'\tif (!widgets)\n\t\treturn 0;', '\tif (!widgets)\n\t\treturn -ENOMEM;')
x=rep(x,'\tsia91xx_append_i2c_address(', '\tret = sia91xx_append_i2c_address(')
x=rep(x,'\t\t\t\t   num_dapm_widgets, NULL, 0);','\t\t\t\t   num_dapm_widgets, NULL, 0);\n\tif (ret)\n\t\treturn ret;')
x=rep(x,'\tsnd_soc_dapm_new_controls(', '\tret = snd_soc_dapm_new_controls(')
x=rep(x,'\t\t\t\t  ARRAY_SIZE(sia91xx_dapm_widgets_common));', '\t\t\t\t  ARRAY_SIZE(sia91xx_dapm_widgets_common));\n\tif (ret)\n\t\treturn ret;')
x=rep(x,'\tsnd_soc_dapm_add_routes(', '\tret = snd_soc_dapm_add_routes(')
x=rep(x,'\t\t\t\tARRAY_SIZE(sia91xx_dapm_routes_common));\n\n\treturn 0;', '\t\t\t\tARRAY_SIZE(sia91xx_dapm_routes_common));\n\tif (ret)\n\t\treturn ret;\n\treturn sia91xx_register_interrupt(si_pa);')
s=s[:a]+x+s[z:];p.write_text(s)
