"""Applied once: connect checked registration, firmware and owned IRQ/work teardown."""
from pathlib import Path
B = Path('/tmp/onyx-audio-amplifiers-handoff/private-source/qcom/opensource/audio-kernel/asoc/codecs')
def edit(name, fn):
 p=B/name; s=p.read_text(); t=fn(s); assert s!=t,name; p.write_text(t)
def replace(s,a,b):
 assert a in s,a[:100]; return s.replace(a,b)
def aw(s):
 s=replace(s,'\taw882xx_dai_drv_append_suffix(aw882xx, dai_drv,\n\t\t\t\t      ARRAY_SIZE(aw882xx_dai));','\tret = aw882xx_dai_drv_append_suffix(aw882xx, dai_drv,\n\t\t\t\t\t     ARRAY_SIZE(aw882xx_dai));\n\tif (ret)\n\t\treturn ret;')
 s=replace(s,'\t\treturn -EINVAL;\n\t}\n\n\tif (aw882xx->rename_flag', '\t\treturn ret;\n\t}\n\n\tif (aw882xx->rename_flag')
 s=replace(s,'\tset_smartpa_codec(aw882xx->dev, dai_drv[0].name);','\tret = register_smartpa_codec(aw882xx->dev, dai_drv[0].name, "aw882xx");\n\tif (ret) {\n\t\taw882xx->codec_ops->unregister_codec(aw882xx->dev);\n\t\treturn ret;\n\t}')
 s=replace(s,'\tset_smartpa_type("aw882xx", sizeof("aw882xx"));\n','')
 s=replace(s,'\taw882xx_gpio_request(aw882xx);','\tret = aw882xx_gpio_request(aw882xx);\n\tif (ret)\n\t\treturn ret;')
 s=replace(s,'\taw882xx_interrupt_init(aw882xx);','\tret = aw882xx_interrupt_init(aw882xx);\n\tif (ret)\n\t\tgoto err_device;')
 s=replace(s,'\t\taw_dev_err(&i2c->dev, "codec register failde");\n\t\treturn ret;', '\t\taw_dev_err(&i2c->dev, "codec register failed");\n\t\tgoto err_irq;')
 s=replace(s,'err_sysfs:\n\taw_componet_codec_ops.unregister_codec(&i2c->dev);\n\n\treturn ret;', '''err_sysfs:
	unregister_smartpa_codec(&i2c->dev);
	aw_componet_codec_ops.unregister_codec(&i2c->dev);
err_irq:
	if (aw882xx->irq_requested) {
		devm_free_irq(&i2c->dev, gpio_to_irq(aw882xx->irq_gpio), aw882xx);
		aw882xx->irq_requested = false;
	}
err_device:
	aw882xx_dev_remove(aw882xx->aw_pa);
	return ret;''')
 a=s.index('\t/*rm irq*/',s.index('static void aw882xx_i2c_remove'))
 z=s.index('\t/*remove device to total list*/',a)
 s=s[:a]+'''	unregister_smartpa_codec(&i2c->dev);
	sysfs_remove_group(&i2c->dev.kobj, &aw882xx_attribute_group);
	if (aw882xx->irq_requested) {
		devm_free_irq(&i2c->dev, gpio_to_irq(aw882xx->irq_gpio), aw882xx);
		aw882xx->irq_requested = false;
	}
	/* Remove ALSA callbacks and drain work before profile/device storage. */
	aw882xx->codec_ops->unregister_codec(&i2c->dev);
	aw882xx_dev_remove(aw882xx->aw_pa);

'''+s[z:]
 a=s.index('static int aw882xx_interrupt_init'); z=s.index('static ssize_t reg_store',a)
 x=s[a:z]; x=replace(x,'\t} else {','\t\taw882xx->irq_requested = true;\n\t} else {');s=s[:a]+x+s[z:]
 # Component removal can happen independently of driver unbind. Disable the IRQ
 # while callbacks/work are drained, then restore it on a future component bind.
 s=replace(s,'\taw882xx->codec = aw_codec;','\taw882xx->codec = aw_codec;\n\tif (aw882xx->irq_disabled) {\n\t\tenable_irq(gpio_to_irq(aw882xx->irq_gpio));\n\t\taw882xx->irq_disabled = false;\n\t}')
 a=s.index('#ifdef AW_KERNEL_VER_OVER_4_19_1\nstatic void aw882xx_codec_remove');z=s.index('static int aw882xx_dai_drv_append_suffix',a)
 s=s[:a]+'''static void aw882xx_stop_component(struct aw882xx *aw882xx)
{
	if (aw882xx->irq_requested && !aw882xx->irq_disabled) {
		disable_irq(gpio_to_irq(aw882xx->irq_gpio));
		aw882xx->irq_disabled = true;
	}
	if (!aw882xx->work_queue)
		return;
	cancel_delayed_work_sync(&aw882xx->fw_work);
	cancel_delayed_work_sync(&aw882xx->interrupt_work);
	cancel_delayed_work_sync(&aw882xx->start_work);
	cancel_delayed_work_sync(&aw882xx->dc_work);
	aw882xx_monitor_stop(&aw882xx->aw_pa->monitor_desc);
	aw882xx_device_stop(aw882xx->aw_pa);
	destroy_workqueue(aw882xx->work_queue);
	aw882xx->work_queue = NULL;
	aw882xx->aw_pa->work_queue = NULL;
	aw882xx_dev_deinit(aw882xx->aw_pa);
	aw882xx->codec = NULL;
}

#ifdef AW_KERNEL_VER_OVER_4_19_1
static void aw882xx_codec_remove(aw_snd_soc_codec_t *aw_codec)
{
	aw882xx_stop_component(aw_componet_codec_ops.codec_get_drvdata(aw_codec));
}
#else
static int aw882xx_codec_remove(aw_snd_soc_codec_t *aw_codec)
{
	aw882xx_stop_component(aw_componet_codec_ops.codec_get_drvdata(aw_codec));
	return 0;
}
#endif

'''+s[z:]
 # Validate fixed-format request lengths, ranges, and short I2C writes.
 s=replace(s,'\tstr_len = count - AWRW_HDR_LEN - 1;','\tif (count <= AWRW_HDR_LEN)\n\t\treturn -EINVAL;\n\tstr_len = count - AWRW_HDR_LEN - 1;')
 s=replace(s,'\t\t} else {\n\t\t\treturn -EINVAL;\n\t\t}\n\t}\n\n\tret = i2c_master_send', '\t\t} else {\n\t\t\tkfree(data_buf);\n\t\t\treturn -EINVAL;\n\t\t}\n\t}\n\n\tret = i2c_master_send')
 s=replace(s,'\tif (ret < 0) {\n\t\taw_dev_err(aw882xx->dev, "write failed");', '\tif (ret != data_len + 1) {\n\t\taw_dev_err(aw882xx->dev, "write failed");')
 s=replace(s,'\tif (data[AWRW_HDR_REG_ADDR] >= reg_num_max) {', '''	if (data[AWRW_HDR_REG_ADDR] < 0 ||
	    data[AWRW_HDR_REG_ADDR] >= reg_num_max ||
	    data[AWRW_HDR_REG_NUM] <= 0 ||
	    data[AWRW_HDR_REG_NUM] > reg_num_max - data[AWRW_HDR_REG_ADDR] ||
	    data[AWRW_HDR_REG_NUM] > 255) {''')
 s=replace(s,'\tret = aw882xx_awrw_parse_buf(aw882xx, buf, count);','\tmutex_lock(&aw882xx->lock);\n\tret = aw882xx_awrw_parse_buf(aw882xx, buf, count);\n\tmutex_unlock(&aw882xx->lock);')
 s=replace(s,'\tif (packet->status != AWRW_I2C_ST_READ) {','\tmutex_lock(&aw882xx->lock);\n\tif (packet->status != AWRW_I2C_ST_READ) {')
 s=replace(s,'\t\taw_dev_err(aw882xx->dev, "please write read cmd first");\n\t\treturn -EINVAL;', '\t\taw_dev_err(aw882xx->dev, "please write read cmd first");\n\t\tret = -EINVAL;\n\t\tgoto exit;')
 s=replace(s,'\treg_data = kmalloc(data_len, GFP_KERNEL);\n\tif (reg_data == NULL)\n\t\treturn -ENOMEM;', '\treg_data = kmalloc(data_len, GFP_KERNEL);\n\tif (reg_data == NULL) {\n\t\tret = -ENOMEM;\n\t\tgoto exit;\n\t}')
 s=replace(s,'\tpacket->status = AWRW_I2C_ST_NONE;\n\treturn ret;', '\tpacket->status = AWRW_I2C_ST_NONE;\n\tmutex_unlock(&aw882xx->lock);\n\treturn ret;')
 s=replace(s,'\tmutex_lock(&g_aw882xx_lock);\n\tif (g_awinic_cfg == NULL)', '\tif (!cont->size || cont->size > INT_MAX - sizeof(int)) {\n\t\trelease_firmware(cont);\n\t\treturn;\n\t}\n\n\tmutex_lock(&g_aw882xx_lock);\n\tif (g_awinic_cfg == NULL)')
 return s
edit('aw882xx/aw882xx.c',aw)
edit('aw882xx/aw882xx.h',lambda s:replace(s,'\tint irq_gpio;','\tint irq_gpio;\n\tbool irq_requested;\n\tbool irq_disabled;'))
def awdev(s):
 s=replace(s,'\taw882xx_monitor_deinit(&aw_dev->monitor_desc);\n\taw882xx_cali_deinit', '\tmutex_lock(&g_dev_lock);\n\tlist_del_init(&aw_dev->list_node);\n\tmutex_unlock(&g_dev_lock);\n\taw882xx_monitor_deinit(&aw_dev->monitor_desc);\n\taw882xx_cali_deinit')
 s=replace(s,'\tret = aw882xx_spin_init(&aw_dev->spin_desc);\n\tif (ret)\n\t\treturn ret;', '\tret = aw882xx_spin_init(&aw_dev->spin_desc);\n\tif (ret) {\n\t\taw882xx_monitor_deinit(&aw_dev->monitor_desc);\n\t\taw882xx_cali_deinit(&aw_dev->cali_desc);\n\t\tif (aw_dev->channel == 0)\n\t\t\taw_algo_auth_misc_deinit(aw_dev);\n\t\treturn ret;\n\t}')
 return s
edit('aw882xx/aw882xx_device.c',awdev)
