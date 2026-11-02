"""Balance the native DMIC's supplier, notifier and component lifetimes."""
from pathlib import Path

P = Path(__file__).parent / 'private-source/qcom/opensource/audio-kernel/asoc/codecs/swr-dmic.c'
s = P.read_text()
s = s.replace('struct snd_soc_component *supply_component;', '''struct snd_soc_component *supply_component;
	struct device *supply_dev;
	struct mutex supply_lock;
	bool notifier_registered;
	bool component_registered;''')
s = s.replace('if (max_uc != SWR_UC_MAX)', '''if (map_size != SWR_UC_MAX * SWR_DMIC_MAX_PORTS *
			SWR_PORT_PARAMS * sizeof(u32))''')
s = s.replace('char w_name[MAX_NAME_LEN];', 'char w_name[MAX_NAME_LEN];\n\tint ret;')
s = s.replace('''	swr_dmic->nblock.notifier_call = swr_dmic_event_notify;
#ifdef CONFIG_SND_SOC_WCD939X
	wcd939x_swr_dmic_register_notifier(swr_dmic->supply_component,
					&swr_dmic->nblock, true);
#else
	wcd938x_swr_dmic_register_notifier(swr_dmic->supply_component,
					&swr_dmic->nblock, true);
#endif

	return 0;''', '''	if (swr_dmic->is_wcd_supply) {
		swr_dmic->nblock.notifier_call = swr_dmic_event_notify;
#ifdef CONFIG_SND_SOC_WCD939X
		ret = wcd939x_swr_dmic_register_notifier(swr_dmic->supply_component,
						 &swr_dmic->nblock, true);
#else
		ret = wcd938x_swr_dmic_register_notifier(swr_dmic->supply_component,
						 &swr_dmic->nblock, true);
#endif
		if (ret)
			return ret;
		swr_dmic->notifier_registered = true;
	}

	return 0;''')
s = s.replace('''	swr_dmic->component = NULL;
	return;''', '''	if (swr_dmic->notifier_registered) {
#ifdef CONFIG_SND_SOC_WCD939X
		wcd939x_swr_dmic_register_notifier(swr_dmic->supply_component,
						&swr_dmic->nblock, false);
#else
		wcd938x_swr_dmic_register_notifier(swr_dmic->supply_component,
						&swr_dmic->nblock, false);
#endif
		swr_dmic->notifier_registered = false;
	}
	swr_dmic->component = NULL;''')
start = s.index('static int swr_dmic_parse_supply(')
end = s.index('\nstatic struct snd_soc_dai_driver', start)
s = s[:start] + '''static int swr_dmic_parse_supply(struct device_node *np,
				struct swr_dmic_priv *swr_dmic)
{
	struct platform_device *pdev;
	struct snd_soc_component *component;

	if (!np || !swr_dmic)
		return -EINVAL;
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -EPROBE_DEFER;

	/* The borrowed component must outlive this consumer's binding. */
	if (!device_link_add(swr_dmic->dev, &pdev->dev,
			     DL_FLAG_AUTOREMOVE_CONSUMER)) {
		put_device(&pdev->dev);
		return -ENOMEM;
	}
	component = snd_soc_lookup_component(&pdev->dev, NULL);
	if (!component) {
		put_device(&pdev->dev);
		return -EPROBE_DEFER;
	}
	swr_dmic->supply_component = component;
	swr_dmic->supply_dev = &pdev->dev;
	return 0;
}

static void swr_dmic_release_supply(struct swr_dmic_priv *swr_dmic)
{
	if (swr_dmic->is_en_supply > 0) {
		enable_wcd_codec_supply(swr_dmic, false);
		swr_dmic->is_en_supply = 0;
	}
	swr_dmic->supply_component = NULL;
	put_device(swr_dmic->supply_dev);
	swr_dmic->supply_dev = NULL;
	of_node_put(swr_dmic->wcd_handle);
	swr_dmic->wcd_handle = NULL;
	swr_dmic->is_wcd_supply = false;
}
''' + s[end:]
needle = '''	if (!swr_dmic)
		return -ENOMEM;

	ret = of_property_read_u32'''
assert needle in s
s = s.replace(needle, '''	if (!swr_dmic)
		return -ENOMEM;

	swr_dmic->dev = &pdev->dev;
	swr_dmic->swr_slave = pdev;
	mutex_init(&swr_dmic->supply_lock);
	swr_set_dev_data(pdev, swr_dmic);

	ret = of_property_read_u32''')
s = s.replace('''		swr_dmic_parse_supply(swr_dmic->wcd_handle, swr_dmic);
		swr_dmic->is_wcd_supply = true;''', '''		ret = swr_dmic_parse_supply(swr_dmic->wcd_handle, swr_dmic);
		if (ret)
			goto err;
		swr_dmic->is_wcd_supply = true;''')
s = s.replace('''			swr_dmic->is_wcd_supply = false;
			swr_dmic->wcd_handle = NULL;
''', '')
s = s.replace('''	swr_set_dev_data(pdev, swr_dmic);

	swr_dmic->swr_slave = pdev;

''', '')
start = s.index('\n\t\tif (swr_dmic->is_en_supply == 1)', s.index('static int swr_dmic_probe('))
end = s.index('\n\t\tgoto err;', start)
s = s[:start] + s[end:]
s = s.replace('''	component = snd_soc_lookup_component(&pdev->dev,
''', '''	swr_dmic->component_registered = true;
	component = snd_soc_lookup_component(&pdev->dev,
''')
s = s.replace('''		dev_err(&pdev->dev, "%s: could not find swr_dmic component\\n",
			__func__);
		goto dev_err;''', '''		dev_err(&pdev->dev, "%s: could not find swr_dmic component\\n",
			__func__);
		ret = -EPROBE_DEFER;
		goto dev_err;''')
start = s.index('\ndev_err:', s.index('static int swr_dmic_probe('))
end = s.index('\nstatic int swr_dmic_reset(', start)
s = s[:start] + '''
dev_err:
err:
	if (swr_dmic->component_registered)
		snd_soc_unregister_component(&pdev->dev);
	swr_dmic_release_supply(swr_dmic);
	swr_set_dev_data(pdev, NULL);
	mutex_destroy(&swr_dmic->supply_lock);
	return ret;
}

static int swr_dmic_remove(struct swr_device *pdev)
{
	struct swr_dmic_priv *swr_dmic = swr_get_dev_data(pdev);

	if (!swr_dmic)
		return 0;
	/* Component removal drains the supplier notifier before releasing power. */
	if (swr_dmic->component_registered)
		snd_soc_unregister_component(&pdev->dev);
	swr_dmic_release_supply(swr_dmic);
	swr_set_dev_data(pdev, NULL);
	mutex_destroy(&swr_dmic->supply_lock);
	return 0;
}

static int swr_dmic_up(struct swr_device *pdev)
{
	struct swr_dmic_priv *swr_dmic = swr_get_dev_data(pdev);
	int ret = 0;

	if (!swr_dmic)
		return -EINVAL;
	if (!swr_dmic->is_wcd_supply)
		return 0;
	mutex_lock(&swr_dmic->supply_lock);
	if (!swr_dmic->is_en_supply)
		ret = enable_wcd_codec_supply(swr_dmic, true);
	if (!ret)
		swr_dmic->is_en_supply++;
	mutex_unlock(&swr_dmic->supply_lock);
	return ret;
}

static int swr_dmic_down(struct swr_device *pdev)
{
	struct swr_dmic_priv *swr_dmic = swr_get_dev_data(pdev);
	int ret = 0;

	if (!swr_dmic)
		return -EINVAL;
	if (!swr_dmic->is_wcd_supply)
		return 0;
	mutex_lock(&swr_dmic->supply_lock);
	if (swr_dmic->is_en_supply == 1)
		ret = enable_wcd_codec_supply(swr_dmic, false);
	if (!ret && swr_dmic->is_en_supply > 0)
		swr_dmic->is_en_supply--;
	mutex_unlock(&swr_dmic->supply_lock);
	return ret;
}
''' + s[end:]
assert 'swr_remove_device(' not in s
assert 'notifier_registered = true' in s
P.write_text(s)
