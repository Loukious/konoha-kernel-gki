from pathlib import Path
import re

H = Path(__file__).resolve().parent

for codec in ['wsa883x', 'wsa884x']:
    upper = codec.upper()
    path = Path('qcom/opensource/audio-kernel/asoc/codecs') / codec / (codec + '.c')
    src = (H / 'original' / path).read_text()
    assert (H / 'private-source' / path).read_text() == src
    start = src.index('static int ' + codec + '_swr_probe(')
    end = src.index('#ifdef CONFIG_PM_SLEEP', start)
    section = src[start:end]
    probe = section[:section.index('\nerr_mem:')]
    probe = re.sub(r'goto (?:err_mem|err_irq|dev_err|err_supply);', 'goto err;', probe)
    probe = probe.replace('\tbool pin_state_current = false;\n', '')
    probe = probe.replace('\tint ret = 0;\n', '\tint ret = 0, i;\n', 1)
    alloc_end = f'\t\treturn -ENOMEM;\n\n\t{codec}_sub_regmap_irq_chip'
    replacement = f'''\t\treturn -ENOMEM;

\tmutex_init(&{codec}->res_lock);
\t{codec}->dev = &pdev->dev;
\t{codec}->swr_slave = pdev;
\tswr_set_dev_data(pdev, {codec});

\t{codec}_sub_regmap_irq_chip'''
    assert alloc_end in probe
    probe = probe.replace(alloc_end, replacement, 1)
    probe = probe.replace(f'\tif (!{codec}_sub_regmap_irq_chip)\n\t\treturn -ENOMEM;',
        f'\tif (!{codec}_sub_regmap_irq_chip) {{\n\t\tret = -ENOMEM;\n\t\tgoto err;\n\t}}')
    probe = probe.replace('''\tif (ret) {
\t\tret = -EPROBE_DEFER;
\t\tgoto err;
\t}''', '\tif (ret)\n\t\tgoto err;', 1)
    probe = probe.replace('''\t\tdev_dbg(&pdev->dev, "%s: pinctrl not defined\\n", __func__);
\t\tgoto err;''', '''\t\tdev_dbg(&pdev->dev, "%s: pinctrl not defined\\n", __func__);
\t\tret = -EINVAL;
\t\tgoto err;''', 1)
    old = f'''\tswr_set_dev_data(pdev, {codec});
\t{codec}->swr_slave = pdev;
\t{codec}->dev = &pdev->dev;
\tpin_state_current = msm_cdc_pinctrl_get_state({codec}->wsa_rst_np);
\t{codec}_gpio_ctrl({codec}, true);'''
    new = f'''\t{codec}->reset_was_active =
\t\tmsm_cdc_pinctrl_get_state({codec}->wsa_rst_np);
\tret = {codec}_gpio_ctrl({codec}, true);
\tif (ret)
\t\tgoto err;
\t{codec}->reset_active = true;'''
    assert old in probe
    probe = probe.replace(old, new)

    # Publish the optional proc interface only after fallible setup is complete.
    proc_start = probe.index('\tproc_entry_name =')
    proc_end = probe.index('\n\t/* Set all interrupts', proc_start) if codec == 'wsa883x' else probe.index(f'\n\t{codec}_sub_regmap_irq_chip->irq_drv_data', proc_start)
    proc_block = probe[proc_start:proc_end]
    proc_block = proc_block.replace(f'\t{codec}->{codec}_proc_entry = proc_mkdir(proc_entry_name, NULL);',
        f'\tif (proc_entry_name)\n\t\t{codec}->{codec}_proc_entry = proc_mkdir(proc_entry_name, NULL);')
    probe = probe[:proc_start] + probe[proc_end:]
    probe = re.sub(r'^\s*' + codec + r'->swr_slave->slave_irq = ' + codec + r'->virq;\n', '\n', probe, flags=re.M)
    req = r'\twcd_request_irq\(&' + codec + r'->irq_info, (' + upper + r'_IRQ_INT_\w+),\n\s*([^;]+);'
    def checked_irq(m):
        return (f'\tret = wcd_request_irq(&{codec}->irq_info, {m[1]},\n\t\t\t{m[2]};\n'
                f'\tif (ret)\n\t\tgoto err;\n\tset_bit({m[1]}, &{codec}->requested_irqs);')
    probe, n = re.subn(req, checked_irq, probe)
    assert n == 10, n
    probe = re.sub(r'kstrndup\(buffer, strlen\(buffer\), GFP_KERNEL\)',
                   'devm_kstrdup(&pdev->dev, buffer, GFP_KERNEL)', probe)
    alloc_checks = f'''\tif (!{codec}->driver->name || !{codec}->dai_driver->name ||
\t    !{codec}->dai_driver->playback.stream_name) {{
\t\tret = -ENOMEM;
\t\tgoto err;
\t}}

'''
    probe = probe.replace('\t/* Number of DAI\'s used is 1 */', alloc_checks + '\t/* Number of DAI\'s used is 1 */')
    reg_end = f'\t\t\t\t{codec}->driver, {codec}->dai_driver, 1);'
    assert reg_end in probe
    probe = probe.replace(reg_end, reg_end + f'''\n\tif (ret)
\t\tgoto err;
\t{codec}->component_registered = true;''')

    # Register the parent notifier last, when all callback-visible state exists.
    parent_start = probe.index(f'\t{codec}->parent_np =')
    parent_end = probe.index('\n\t/* Start in speaker mode', parent_start) if codec == 'wsa884x' else probe.index(f'\n\tmutex_init(&{codec}->res_lock);', parent_start)
    parent_block = f'''\t{codec}->parent_np = of_parse_phandle(pdev->dev.of_node,
\t\t\t\t\t      "qcom,bolero-handle", 0);
\tif (!{codec}->parent_np)
\t\t{codec}->parent_np = of_parse_phandle(pdev->dev.of_node,
\t\t\t\t\t      "qcom,lpass-cdc-handle", 0);
\tif ({codec}->parent_np) {{
\t\t{codec}->parent_dev = of_find_device_by_node({codec}->parent_np);
\t\tif (!{codec}->parent_dev) {{
\t\t\tret = -EPROBE_DEFER;
\t\t\tgoto err;
\t\t}}
\t\tplat_data = dev_get_platdata(&{codec}->parent_dev->dev);
\t\tif (!plat_data || !plat_data->register_notifier) {{
\t\t\tret = -EPROBE_DEFER;
\t\t\tgoto err;
\t\t}}
\t\tif (!device_link_add(&pdev->dev, &{codec}->parent_dev->dev,
\t\t\t\t     DL_FLAG_AUTOREMOVE_CONSUMER)) {{
\t\t\tret = -ENOMEM;
\t\t\tgoto err;
\t\t}}
\t\t{codec}->handle = plat_data->handle;
\t\t{codec}->register_notifier = plat_data->register_notifier;
\t\t{codec}->parent_nblock.notifier_call = {codec}_event_notify;
\t\tret = {codec}->register_notifier({codec}->handle,
\t\t\t\t\t  &{codec}->parent_nblock, true);
\t\tif (ret)
\t\t\tgoto err;
\t\t{codec}->notifier_registered = true;
\t}}
'''
    probe = probe[:parent_start] + probe[parent_end:]
    # Drop the original late lock initialization (the new one is before setup).
    late_lock = f'\tmutex_init(&{codec}->res_lock);\n'
    pos = probe.rfind(late_lock)
    assert pos > probe.index(late_lock)
    probe = probe[:pos] + probe[pos + len(late_lock):]

    if codec == 'wsa883x':
        probe = probe.replace('''\tfor (i = 0; i < wsa883x_sub_regmap_irq_chip->num_regs; i++)
\t\tregmap_write(wsa883x->regmap, (WSA883X_INTR_LEVEL0 + i), 0);''', '''\tfor (i = 0; i < wsa883x_sub_regmap_irq_chip->num_regs; i++) {
\t\tret = regmap_write(wsa883x->regmap, WSA883X_INTR_LEVEL0 + i, 0);
\t\tif (ret)
\t\t\tgoto err;
\t}''')
    else:
        probe = probe.replace('''\t\t\t\t\t__func__);
\t\t\t\tgoto err;
\t\t\t}

\t\t\tsys_gain_length = sys_gain_size / sizeof(u32);''', '''\t\t\t\t\t__func__);
\t\t\t\tret = -EINVAL;
\t\t\t\tgoto err;
\t\t\t}
\t\t\tif (sys_gain_size != sizeof(wsa884x->sys_gains)) {
\t\t\t\tret = -EINVAL;
\t\t\t\tgoto err;
\t\t\t}

\t\t\tsys_gain_length = ARRAY_SIZE(wsa884x->sys_gains);''')
        probe = probe.replace('''\t\t\twsa884x->system_gain = wsa884x->sys_gains[''', '''\t\t\tfor (i = 0; i < ARRAY_SIZE(wsa884x->sys_gains); i++) {
\t\t\t\tif (wsa884x->sys_gains[i] >= G_MAX_DB) {
\t\t\t\t\tret = -EINVAL;
\t\t\t\t\tgoto err;
\t\t\t\t}
\t\t\t}
\t\t\twsa884x->system_gain = wsa884x->sys_gains[''')
        probe = probe.replace('''\t\t\tdev_err(&pdev->dev, "%s: parent dev not found\\n",
\t\t\t\t__func__);
\t\t\tgoto err;''', '''\t\t\tdev_err(&pdev->dev, "%s: parent dev not found\\n",
\t\t\t\t__func__);
\t\t\tret = -EPROBE_DEFER;
\t\t\tgoto err;''')
        probe = probe.replace('''\t\tdev_err(&pdev->dev, "%s: parent node not found\\n", __func__);
\t\tgoto err;''', '''\t\tdev_err(&pdev->dev, "%s: parent node not found\\n", __func__);
\t\tret = -EINVAL;
\t\tgoto err;''')
        probe = probe.replace('''\tret = wsa884x_validate_dt_configuration_params''', '''\tif (wsa884x->rload >= WSA_MAX_OHMS || wsa884x->bat_cfg >= CONFIG_MAX) {
\t\tret = -EINVAL;
\t\tgoto err;
\t}
\tret = wsa884x_validate_dt_configuration_params''')
        probe = probe.replace('''\tswr_init_port_params(wsa884x->swr_slave, WSA884X_MAX_SWR_PORTS,
\t\twsa884x->swr_wsa_port_params);''', '''\tret = swr_init_port_params(wsa884x->swr_slave, WSA884X_MAX_SWR_PORTS,
\t\t\t\t   wsa884x->swr_wsa_port_params);
\tif (ret)
\t\tgoto err;''')

    publish = f'''\tret = swr_set_slave_irq(pdev, {codec}->virq);
\tif (ret)
\t\tgoto err;
\t{codec}->irq_published = true;

'''
    probe = probe.replace('#ifdef CONFIG_DEBUG_FS', publish + parent_block + '\n' + proc_block + '\n#ifdef CONFIG_DEBUG_FS', 1)
    probe += f'''\nerr:
\t{codec}_swr_cleanup({codec});
\treturn ret;
}}

static int {codec}_swr_remove(struct swr_device *pdev)
{{
\tstruct {codec}_priv *{codec} = swr_get_dev_data(pdev);

\tif (!{codec})
\t\treturn 0;
\treturn {codec}_swr_cleanup({codec});
}}

'''
    macro_cleanup = '''\tif (wsa884x->macro_dev)
\t\tput_device(&wsa884x->macro_dev->dev);
\tof_node_put(wsa884x->macro_np);
''' if codec == 'wsa884x' else ''
    cleanup = f'''/* Called by the driver core with probe/remove serialized, never from an IRQ. */
static int {codec}_swr_cleanup(struct {codec}_priv *{codec})
{{
\tint irq, ret;

\tif ({codec}->notifier_registered) {{
\t\tret = {codec}->register_notifier({codec}->handle,
\t\t\t\t\t  &{codec}->parent_nblock, false);
\t\tif (ret)
\t\t\treturn ret;
\t\t{codec}->notifier_registered = false;
\t}}
\tif ({codec}->irq_published) {{
\t\t/* Withdrawal waits for dispatch without holding handler locks. */
\t\tret = swr_set_slave_irq({codec}->swr_slave, NULL);
\t\tif (ret)
\t\t\treturn ret;
\t\t{codec}->irq_published = false;
\t}}
\tproc_remove({codec}->{codec}_proc_entry);
\t{codec}->{codec}_proc_entry = NULL;
#ifdef CONFIG_DEBUG_FS
\tdebugfs_remove_recursive({codec}->debugfs_dent);
\t{codec}->debugfs_dent = NULL;
#endif
\tif ({codec}->component_registered) {{
\t\tsnd_soc_unregister_component({codec}->dev);
\t\t{codec}->component_registered = false;
\t}}
\tfor (irq = {upper}_NUM_IRQS - 1; irq >= 0; irq--) {{
\t\tif (test_and_clear_bit(irq, &{codec}->requested_irqs))
\t\t\twcd_free_irq(&{codec}->irq_info, irq, {codec});
\t}}
\tif ({codec}->virq) {{
\t\twcd_irq_exit(&{codec}->irq_info, {codec}->virq);
\t\t{codec}->virq = NULL;
\t}}
\tif ({codec}->reset_active && !{codec}->reset_was_active)
\t\t{codec}_gpio_ctrl({codec}, false);
\tif ({codec}->supplies_initialized)
\t\tmsm_cdc_release_supplies({codec}->dev, {codec}->supplies,
\t\t\t\t\t {codec}->regulator, {codec}->num_supplies);
{macro_cleanup}\tif ({codec}->parent_dev)
\t\tput_device(&{codec}->parent_dev->dev);
\tof_node_put({codec}->parent_np);
\tof_node_put({codec}->wsa_rst_np);
\tswr_set_dev_data({codec}->swr_slave, NULL);
\tmutex_destroy(&{codec}->res_lock);
\treturn 0;
}}

'''
    src = src[:start] + cleanup + probe + src[end:]
    # The supplier helper publishes the successful init before enable can fail.
    src = src.replace('''\tif (!priv->supplies) {''', '''\tif (ret || !priv->supplies) {''', 1)
    marker = '\tret = msm_cdc_enable_static_supplies(dev, priv->supplies,'
    src = src.replace(marker, '\tpriv->supplies_initialized = true;\n\n' + marker, 1)
    if codec == 'wsa884x':
        # This handler can fire before a machine card binds its component.
        marker = '\tcomponent = wsa884x->component;\n\tsnd_soc_component_update_bits(component,'
        assert marker in src
        src = src.replace(marker, '\tcomponent = wsa884x->component;\n\tif (!component)\n\t\treturn IRQ_NONE;\n\tsnd_soc_component_update_bits(component,', 1)
    (H / 'private-source' / path).write_text(src)
    internal = path.with_name('internal.h')
    header = (H / 'private-source' / internal).read_text()
    header = header.replace('\tstruct wcd_irq_info irq_info;\n', '''\tstruct wcd_irq_info irq_info;
\tunsigned long requested_irqs;
\tbool irq_published;
\tbool component_registered;
\tbool notifier_registered;
\tbool supplies_initialized;
\tbool reset_active;
\tbool reset_was_active;
''')
    (H / 'private-source' / internal).write_text(header)

print('Adapted fresh WSA codec private sources; no staged or baseline files changed')
