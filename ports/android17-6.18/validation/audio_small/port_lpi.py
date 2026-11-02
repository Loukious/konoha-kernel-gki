"""Apply LPI's public-API, pin-boundary and registration-lifetime adaptation."""
from pathlib import Path

P = Path(__file__).parent / 'private-source/qcom/opensource/audio-kernel/soc/pinctrl-lpi.c'
s = P.read_text()
s = s.replace('#include "core.h"\n#include "pinctrl-utils.h"\n', '')
s = s.replace('struct pinctrl_dev  *ctrl;', 'struct pinctrl_dev  *ctrl;\n\tstruct pinctrl_desc *desc;')
s = s.replace('static int lpi_gpio_get_groups_count', '''static const struct pinctrl_desc *lpi_gpio_desc(struct pinctrl_dev *pctldev)
{
	struct lpi_gpio_state *state = pinctrl_dev_get_drvdata(pctldev);

	return state->desc;
}

static int lpi_gpio_get_groups_count''')
s = s.replace('state->ctrl->desc', 'state->desc')
s = s.replace('pctldev->desc', 'lpi_gpio_desc(pctldev)')
s = s.replace('dev_get_drvdata(pctldev->dev)', 'pinctrl_dev_get_drvdata(pctldev)')
s = s.replace('pctldev->dev', 'state->dev')
s = s.replace('pinctrl_utils_free_map', 'pinconf_generic_dt_free_map')
s = s.replace('static void lpi_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)',
              'static int lpi_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)')
s = s.replace('\tlpi_config_set(state->ctrl, pin, &config, 1);',
              '\treturn lpi_config_set(state->ctrl, pin, &config, 1);')
s = s.replace('int ret, npins, i;', 'int ret, i;\n\tu32 npins;')
s = s.replace('WARN_ON(npins > ARRAY_SIZE(lpi_gpio_groups));',
              'if (!npins || npins > ARRAY_SIZE(lpi_gpio_groups))\n\t\treturn -EINVAL;')
s = s.replace('state->chip.can_sleep = false;', 'state->chip.can_sleep = true;')
s = s.replace('int i, ret = 0;', 'int i, bit, ret = 0;')
s = s.replace('for (i = 0; i < LPI_SLEW_BITS_SIZE; i++)',
              'for (bit = 0; bit < LPI_SLEW_BITS_SIZE; bit++)')
s = s.replace('arg = pad->pullup = LPI_GPIO_BIAS_DISABLE;',
              'arg = pad->pullup == LPI_GPIO_BIAS_DISABLE;')
s = s.replace('arg = pad->pullup = LPI_GPIO_KEEPER;',
              'arg = pad->pullup == LPI_GPIO_KEEPER;')
s = s.replace('pctrldesc->npins = npins;', 'pctrldesc->npins = npins;\n\tstate->desc = pctrldesc;')
s = s.replace('''		if ((lpi_slew_base[i] != LPI_SLEW_OFFSET_INVALID) &&
		     lpi_slew_base[i])
			pad->lpi_slew_reg = devm_ioremap(dev,
                                                lpi_slew_base[i], 0x4);''', '''		if (lpi_offset[i] > LPI_ADDRESS_SIZE - 2 * sizeof(u32) ||
		    (lpi_offset[i] & 3) ||
		    (lpi_slew_offset[i] != LPI_SLEW_OFFSET_INVALID &&
		     lpi_slew_offset[i] > 32 - LPI_SLEW_BITS_SIZE))
			return -EINVAL;
		if (lpi_slew_base[i] != LPI_SLEW_OFFSET_INVALID &&
		    lpi_slew_base[i]) {
			pad->lpi_slew_reg = devm_ioremap(dev, lpi_slew_base[i], sizeof(u32));
			if (!pad->lpi_slew_reg)
				return -ENOMEM;
		}''')
# Resource votes and PM must exist before pinctrl/notifier publication.
start = s.index('\t/* Register LPASS core hw vote */')
end = s.index('\n\treturn 0;', start)
votes = s[start:end]
s = s[:start] + s[end:]
marker = '\tstate->ctrl = devm_pinctrl_register(dev, pctrldesc, state);'
s = s.replace(marker, votes + '''
	lpi_dev = dev;
	lpi_dev_up = true;
	initial_boot = true;

''' + marker)
s = s.replace('''	if (IS_ERR(state->ctrl))
		return PTR_ERR(state->ctrl);''', '''	if (IS_ERR(state->ctrl)) {
		ret = PTR_ERR(state->ctrl);
		goto err_pm;
	}''')
s = s.replace('''	lpi_dev = &pdev->dev;
	lpi_dev_up = true;
	initial_boot = true;

''', '')
s = s.replace('''		goto err_range;
	}


	return 0;

//err_snd_evt:
//	audio_notifier_deregister("lpi_tlmm");''', '''		goto err_snd_event;
	}

	return 0;

err_snd_event:
	snd_event_client_deregister(dev);''')
assert 'err_snd_event:' in s
s = s.replace('''err_chip:
	mutex_destroy''', '''err_chip:
	devm_pinctrl_unregister(dev, state->ctrl);
err_pm:
	pm_runtime_disable(dev);
	lpi_pinctrl_runtime_suspend(dev);
	pm_runtime_set_suspended(dev);
	lpi_dev_up = false;
	lpi_dev = NULL;
	mutex_destroy''')
start = s.index('static int lpi_pinctrl_remove(')
end = s.index('\nstatic const struct of_device_id', start)
s = s[:start] + '''static void lpi_pinctrl_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpi_gpio_state *state = platform_get_drvdata(pdev);

	audio_notifier_deregister("lpi_tlmm");
	snd_event_client_deregister(dev);
	gpiochip_remove(&state->chip);
	devm_pinctrl_unregister(dev, state->ctrl);
	pm_runtime_disable(dev);
	lpi_pinctrl_runtime_suspend(dev);
	pm_runtime_set_suspended(dev);
	lpi_dev_up = false;
	lpi_dev = NULL;
	mutex_destroy(&state->core_hw_vote_lock);
	mutex_destroy(&state->slew_access_lock);
}
''' + s[end:]
# Failed runtime resume must not leak a usage vote or be reported as success.
s = s.replace('\tpm_runtime_get_sync(lpi_dev);', '''	ret = pm_runtime_resume_and_get(lpi_dev);
	if (ret < 0)
		return ret;''')
s = s.replace('exit:\n\tmutex_unlock(&state->core_hw_vote_lock);\n\treturn 0;',
              'exit:\n\tmutex_unlock(&state->core_hw_vote_lock);\n\treturn ret;')
P.write_text(s)
