"""Compose the Si driver compatibility patch from pinned native snapshots."""
from pathlib import Path
import re, difflib

HERE = Path(__file__).resolve().parent
PREFIX = 'drivers/input/misc/si_haptic/'

def replace_function(text, name, replacement):
    m = re.search(r'^(?:static )?(?:int|void|sih_haptic_t \*|ssize_t|long)\s*' + name + r'\([^;]*?\)\n\{', text, re.M)
    assert m, name
    brace = text.index('{', m.start())
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[:m.start()] + replacement.strip() + text[end:]

def read(name):
    return (HERE / 'before' / PREFIX / name).read_text()

def write(name, text):
    # Keep continuation parameters aligned after adding internal linkage.
    lines = text.splitlines(True)
    for i, line in enumerate(lines):
        if any(line.startswith('static ' + typ + ' ' + func + '(')
               for typ, func in [('int', 'pointer_prehandle'),
                                 ('int', 'sih_upload_effect'),
                                 ('void', 'sih_reg_config_parse')]):
            col = line.expandtabs().index('(') + 1
            j = i + 1
            while ')' not in lines[j-1]:
                lines[j] = '\t' * (col // 8) + ' ' * (col % 8) + lines[j].lstrip()
                j += 1
    text = ''.join(lines)
    (HERE / 'candidate' / PREFIX / name).write_text(text)

s = read('haptic.h')
# All resources are initialized before IRQ/input/sysfs/misc publication.
s = s.replace('\tbool f0_cali_status;', '''\tstruct input_dev *input_dev;
\t/* Serializes firmware admission with teardown. */
\tstruct mutex fw_lock;
\tatomic_t fw_refs;
\twait_queue_head_t fw_wait;
\tbool stopping;
\tbool input_registered;
\tbool ff_created;
\tbool irq_requested;
\tbool ring_owned;
\tbool sysfs_created;
\tbool stream_initialized;
\tbool f0_cali_status;''')
s = s.replace('sih_haptic_t *get_global_haptic_ptr(void);', '''sih_haptic_t *sih_lock_global(void);
void sih_unlock_global(void);''')
write('haptic.h', s)

s = read('haptic.c')
s = s.replace('strlcpy(', 'strscpy(').replace('strtobool(', 'kstrtobool(')
s = s.replace('GPIOF_DIR_IN', 'GPIOF_IN')
for name in ['pointer_prehandle', 'sih_set_gain_work_routine',
             'sih_effect_strength_gain_adapter', 'sih_set_gain',
             'sih_upload_effect', 'sih_playback', 'sih_erase']:
    s = re.sub(r'^(int|void) ' + name + r'\(', r'static \1 ' + name + '(', s, flags=re.M)
s = s.replace('static sih_haptic_ptr_t g_haptic_t;', '''static sih_haptic_ptr_t g_haptic_t;
static DEFINE_MUTEX(sih_global_lock);
static int sih_request_ram(sih_haptic_t *sih_haptic);''')
s = s.replace('sih_haptic->soft_frame.vib_dev.name', '"si_haptic"')
s = s.replace('hrtimer_init(&sih_haptic->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);\n\tsih_haptic->timer.function = haptic_timer_func;', 'hrtimer_setup(&sih_haptic->timer, haptic_timer_func,\n\t\t      CLOCK_MONOTONIC, HRTIMER_MODE_REL);')
s = s.replace('static void sih_ram_load(', 'static void sih_ram_load_data(')
s = s.replace('\tif (!cont) {', '\tif (!cont || cont->size < 2) {', 1)
pos = s.index('static void sih_ram_play(')
s = s[:pos] + '''static void sih_ram_load(const struct firmware *cont, void *context)
{
\tsih_haptic_t *sih_haptic = context;

\tif (READ_ONCE(sih_haptic->stopping))
\t\trelease_firmware(cont);
\telse
\t\tsih_ram_load_data(cont, context);
\tif (atomic_dec_and_test(&sih_haptic->fw_refs))
\t\twake_up_all(&sih_haptic->fw_wait);
}

static int sih_request_ram(sih_haptic_t *sih_haptic)
{
\tint ret;

\tmutex_lock(&sih_haptic->fw_lock);
\tif (sih_haptic->stopping) {
\t\tmutex_unlock(&sih_haptic->fw_lock);
\t\treturn -ENODEV;
\t}
\tatomic_inc(&sih_haptic->fw_refs);
\tret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT,
\t\t\t\t      awinic_ram_name[sih_haptic->ram.lib_index],
\t\t\t\t      sih_haptic->dev, GFP_KERNEL,
\t\t\t\t      sih_haptic, sih_ram_load);
\tif (ret && atomic_dec_and_test(&sih_haptic->fw_refs))
\t\twake_up_all(&sih_haptic->fw_wait);
\tmutex_unlock(&sih_haptic->fw_lock);
\treturn ret;
}

''' + s[pos:]
s = replace_function(s, 'get_global_haptic_ptr', '''sih_haptic_t *sih_lock_global(void)
{
\tsih_haptic_t *sih_haptic;

\tmutex_lock(&sih_global_lock);
\tsih_haptic = g_haptic_t.g_haptic[SIH_HAPTIC_MMAP_DEV_INDEX];
\tif (!sih_haptic)
\t\tmutex_unlock(&sih_global_lock);
\treturn sih_haptic;
}

void sih_unlock_global(void)
{
\tmutex_unlock(&sih_global_lock);
}''')
s = re.sub(r'request_firmware_nowait\(\s*THIS_MODULE, FW_ACTION_UEVENT,\s*awinic_ram_name\[sih_haptic->ram.lib_index\],\s*sih_haptic->dev, GFP_KERNEL, sih_haptic, sih_ram_load\);', 'sih_request_ram(sih_haptic);', s)
s = replace_function(s, 'ram_work_init', '''static int ram_work_init(sih_haptic_t *sih_haptic)
{
\tsih_haptic->ram.ram_init = false;
\treturn sih_request_ram(sih_haptic);
}''')
start = s.index('static int sih_i2c_probe(')
end = s.index('static int sih_suspend(', start)
s = s[:start] + '''static void sih_cleanup(sih_haptic_t *sih_haptic)
{
\tmutex_lock(&sih_haptic->fw_lock);
\tWRITE_ONCE(sih_haptic->stopping, true);
\tmutex_unlock(&sih_haptic->fw_lock);
\tif (sih_haptic->sysfs_created) {
\t\tsysfs_remove_group(&sih_haptic->dev->kobj,
\t\t\t\t   &sih_vibra_attribute_group);
\t\tsih_haptic->sysfs_created = false;
\t}
\tmutex_lock(&sih_global_lock);
\tif (g_haptic_t.g_haptic[SIH_HAPTIC_MMAP_DEV_INDEX] == sih_haptic)
\t\tg_haptic_t.g_haptic[SIH_HAPTIC_MMAP_DEV_INDEX] = NULL;
\tmutex_unlock(&sih_global_lock);
\tif (sih_haptic->input_registered) {
\t\tinput_unregister_device(sih_haptic->input_dev);
\t\tsih_haptic->input_registered = false;
\t\tsih_haptic->ff_created = false;
\t} else if (sih_haptic->ff_created) {
\t\tinput_ff_destroy(sih_haptic->input_dev);
\t\tsih_haptic->ff_created = false;
\t}
\tif (sih_haptic->ring_owned)
\t\trb_force_exit();
\tatomic_set(&sih_haptic->rtp.exit_in_rtp_loop, 1);
\twake_up_interruptible(&sih_haptic->rtp.wait_q);
\twake_up_interruptible(&sih_haptic->rtp.stop_wait_q);
\tif (sih_haptic->irq_requested) {
\t\tdevm_free_irq(sih_haptic->dev,
\t\t\t      gpio_to_irq(sih_haptic->chip_attr.irq_gpio),
\t\t\t      sih_haptic);
\t\tsih_haptic->irq_requested = false;
\t}
\twait_event(sih_haptic->fw_wait, !atomic_read(&sih_haptic->fw_refs));
\thrtimer_cancel(&sih_haptic->timer);
\tif (sih_haptic->stream_initialized) {
\t\tsih_haptic->stream_func->stream_rtp_work_release(sih_haptic);
\t\tsih_haptic->stream_initialized = false;
\t}
\tcancel_work_sync(&sih_haptic->ram.gain_work);
\tcancel_work_sync(&sih_haptic->ram.ram_work);
\tcancel_work_sync(&sih_haptic->rtp.rtp_work);
\tif (sih_haptic->work_queue) {
\t\tdestroy_workqueue(sih_haptic->work_queue);
\t\tsih_haptic->work_queue = NULL;
\t}
\tif (!IS_ERR_OR_NULL(sih_haptic->regmapp.regmapping) && sih_haptic->hp_func)
\t\tsih_haptic->hp_func->stop(sih_haptic);
\tpm_relax(sih_haptic->dev);
\tdevice_init_wakeup(sih_haptic->dev, false);
\tsih_vfree_container(sih_haptic, sih_haptic->rtp.rtp_cont);
\tsih_haptic->rtp.rtp_cont = NULL;
\tkfree(sih_haptic->chip_reg.reg_addr);
\tsih_haptic->chip_reg.reg_addr = NULL;
\tif (sih_haptic->ring_owned) {
\t\trelease_rb();
\t\tsih_haptic->ring_owned = false;
\t}
}

static int sih_i2c_probe(struct i2c_client *i2c)
{
\tsih_haptic_t *sih_haptic;
\tstruct input_dev *input_dev;
\tstruct ff_device *ff;
\tint ret;

\tif (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C))
\t\treturn -EIO;
\tsih_haptic = devm_kzalloc(&i2c->dev, sizeof(*sih_haptic), GFP_KERNEL);
\tif (!sih_haptic)
\t\treturn -ENOMEM;
\tinput_dev = devm_input_allocate_device(&i2c->dev);
\tif (!input_dev)
\t\treturn -ENOMEM;
\tinput_dev->name = "si_haptic";
\tsih_haptic->input_dev = input_dev;
\tsih_haptic->dev = &i2c->dev;
\tsih_haptic->i2c = i2c;
\ti2c_set_clientdata(i2c, sih_haptic);
\tvibrator_init(sih_haptic);
\tINIT_WORK(&sih_haptic->ram.gain_work, sih_set_gain_work_routine);
\tmutex_init(&sih_haptic->fw_lock);
\tatomic_set(&sih_haptic->fw_refs, 0);
\tinit_waitqueue_head(&sih_haptic->fw_wait);
\tdevice_init_wakeup(sih_haptic->dev, true);
\tret = sih_parse_dts(&i2c->dev, sih_haptic, i2c->dev.of_node);
\tif (ret)
\t\tgoto error;
\tret = sih_acquire_prepare_res(&i2c->dev, sih_haptic);
\tif (ret) {
\t\tif (ret == -EBUSY)
\t\t\tret = -EPROBE_DEFER;
\t\tgoto error;
\t}
\tret = sih_register_func(sih_haptic);
\tif (ret)
\t\tgoto error;
\tsih_haptic->regmapp.regmapping =
\t\thaptic_regmap_init(i2c, sih_haptic->regmapp.config);
\tif (IS_ERR_OR_NULL(sih_haptic->regmapp.regmapping)) {
\t\tret = sih_haptic->regmapp.regmapping ?
\t\t\tPTR_ERR(sih_haptic->regmapp.regmapping) : -ENOMEM;
\t\tgoto error;
\t}
\tret = create_rb();
\tif (ret)
\t\tgoto error;
\tsih_haptic->ring_owned = true;
\tsih_haptic->work_queue = create_singlethread_workqueue("sih_haptic_work_queue");
\tif (!sih_haptic->work_queue) {
\t\tret = -ENOMEM;
\t\tgoto error;
\t}
\tret = vibrator_chip_init(sih_haptic);
\tif (ret)
\t\tgoto error;
\tsih_haptic->hp_func->get_tracking_f0(sih_haptic);
\tret = sih_haptic->stream_func->stream_rtp_work_init(sih_haptic);
\tif (ret)
\t\tgoto error;
\tsih_haptic->stream_initialized = true;
\tret = sih_acquire_irq_res(&i2c->dev, sih_haptic);
\tif (ret)
\t\tgoto error;
\tsih_haptic->irq_requested = true;
\tCUSTOME_WAVE_ID = sih_haptic->chip_ipara.effect_max;
\tinput_set_drvdata(input_dev, sih_haptic);
\tinput_set_capability(input_dev, EV_FF, FF_CONSTANT);
\tinput_set_capability(input_dev, EV_FF, FF_GAIN);
\tinput_set_capability(input_dev, EV_FF, FF_PERIODIC);
\tinput_set_capability(input_dev, EV_FF, FF_CUSTOM);
\tsih_haptic->chip_ipara.effects_count = 10;
\tret = input_ff_create(input_dev,
\t\t\t      max_t(int, FF_EFFECT_COUNT_MAX,
\t\t\t\t    sih_haptic->chip_ipara.effects_count + 1));
\tif (ret)
\t\tgoto error;
\tsih_haptic->ff_created = true;
\tff = input_dev->ff;
\tff->upload = sih_upload_effect;
\tff->playback = sih_playback;
\tff->erase = sih_erase;
\tff->set_gain = sih_set_gain;
\tret = ram_work_init(sih_haptic);
\tif (ret)
\t\tgoto error;
\tret = input_register_device(input_dev);
\tif (ret)
\t\tgoto error;
\tsih_haptic->input_registered = true;
\tret = sysfs_create_group(&i2c->dev.kobj, &sih_vibra_attribute_group);
\tif (ret)
\t\tgoto error;
\tsih_haptic->sysfs_created = true;
\tmutex_lock(&sih_global_lock);
\tg_haptic_t.sih_num = SIH_HAPTIC_DEV_NUM;
\tg_haptic_t.g_haptic[SIH_HAPTIC_MMAP_DEV_INDEX] = sih_haptic;
\tmutex_unlock(&sih_global_lock);
\txm_hap_driver_init(true);
\treturn 0;
error:
\tsih_cleanup(sih_haptic);
\treturn ret;
}

static void sih_i2c_remove(struct i2c_client *i2c)
{
\tsih_cleanup(i2c_get_clientdata(i2c));
}

''' + s[end:]
s = s.replace('static struct of_device_id sih_dt_match[]', 'static const struct of_device_id sih_dt_match[]')
s = s.replace('static struct i2c_driver sih_i2c_driver', 'MODULE_DEVICE_TABLE(of, sih_dt_match);\nMODULE_DEVICE_TABLE(i2c, sih_i2c_id);\n\nstatic struct i2c_driver sih_i2c_driver')
# __init isn't matched by the generic extractor above.
start = s.index('static int __init sih_i2c_init(')
end = s.index('static void __exit sih_i2c_exit(', start)
s = s[:start] + '''static int __init sih_i2c_init(void)
{
\tint ret;

\tret = sih_add_misc_dev();
\tif (ret)
\t\treturn ret;
\tret = i2c_add_driver(&sih_i2c_driver);
\tif (ret)
\t\tsih_del_misc_dev();
\treturn ret;
}

''' + s[end:]
s = s.replace('\ti2c_del_driver(&sih_i2c_driver);', '\tsih_del_misc_dev();\n\ti2c_del_driver(&sih_i2c_driver);')
write('haptic.c', s)

# Fix allocation failure dereference and unwind in the native private ring.
s = read('ringbuffer.c')
s = s.replace('int32_t get_free_size(int32_t tail, int32_t head)',
              'static s32 get_free_size(s32 tail, s32 head)')
start = s.index('int create_rb(void)')
end = s.index('void rb_init(void)', start)
s = s[:start] + '''int create_rb(void)
{
\ts32 tail, head;

\tif (grb)
\t\treturn -EBUSY;
\tgrb = kzalloc(sizeof(*grb), GFP_KERNEL);
\tif (!grb)
\t\treturn -ENOMEM;
\tgrb->gbuffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
\tif (!grb->gbuffer) {
\t\tkfree(grb);
\t\tgrb = NULL;
\t\treturn -ENOMEM;
\t}
\trb_init();
\tinit_waitqueue_head(&grb->wait_q);
\ttail = atomic_read(&grb->wr_index);
\thead = atomic_read(&grb->rd_index);
\tgrb->aval_size = get_free_size(tail, head);
\treturn 0;
}

''' + s[end:]
write('ringbuffer.c', s)

s = read('sih688x_func_config.c')
s = s.replace('void sih_reg_config_parse(', 'static void sih_reg_config_parse(')
write('sih688x_func_config.c', s)

# Register access is devm-managed; no manual regmap_exit on detach.
s = read('haptic_regmap.c')
s = replace_function(s, 'haptic_regmap_remove', '''void haptic_regmap_remove(struct regmap *regmap)
{
\t/* devm_regmap_init_i2c releases the map on detach. */
}''')
write('haptic_regmap.c', s)

s = read('haptic_misc.h')
s = s.replace('int sih_add_misc_dev(void);', 'int sih_add_misc_dev(void);\nvoid sih_del_misc_dev(void);')
write('haptic_misc.h', s)

s = read('haptic_misc.c').replace('#include <linux/page_size_compat.h>\n', '')
s = replace_function(s, 'memo_file_open', '''static int memo_file_open(struct inode *inode, struct file *file)
{
\tsih_haptic_t *sih_haptic = sih_lock_global();

\tif (!sih_haptic)
\t\treturn -ENODEV;
\tsih_unlock_global();
\treturn 0;
}''')
s = replace_function(s, 'memo_file_release', '''static int memo_file_release(struct inode *inode, struct file *file)
{
\treturn 0;
}''')
# Each operation revalidates publication while holding the global lifetime lock.
s = s.replace('sih_haptic_t *sih_haptic = (sih_haptic_t *)file->private_data;', 'sih_haptic_t *sih_haptic = sih_lock_global();')
s = s.replace('int ret = -1;\n\n\thp_info("cmd:', 'int ret = 0;\n\n\tif (!sih_haptic)\n\t\treturn -ENODEV;\n\tif (cmd == IOCTL_STREAM_MODE || cmd == IOCTL_STOP_MODE) {\n\t\tWRITE_ONCE(sih_haptic->stream_para.stream_mode, false);\n\t\tcancel_work_sync(&sih_haptic->stream_para.stream_work);\n\t}\n\thp_info("cmd:')
s = s.replace('\t\tcancel_work_sync(&sih_haptic->stream_para.stream_work);\n\t\thaptic_clean_buf', '\t\thaptic_clean_buf')
s = s.replace('\t\tcancel_work_sync(&sih_haptic->stream_para.stream_work);\n\t\tsih_haptic->chip_ipara.state', '\t\tsih_haptic->chip_ipara.state')
s = s.replace('\tdefault:\n\t\tbreak;\n\t}\n\n\tmutex_unlock(&sih_haptic->lock);\n\treturn 0;', '\tdefault:\n\t\tret = -ENOTTY;\n\t\tbreak;\n\t}\n\n\tmutex_unlock(&sih_haptic->lock);\n\tsih_unlock_global();\n\treturn ret;')
s = s.replace('static int memo_file_mmap(', '''static void sih_vma_open(struct vm_area_struct *vma)
{
\tget_page(vma->vm_private_data);
}

static void sih_vma_close(struct vm_area_struct *vma)
{
\tput_page(vma->vm_private_data);
}

static const struct vm_operations_struct sih_vm_ops = {
\t.open = sih_vma_open,
\t.close = sih_vma_close,
};

static int memo_file_mmap(''', 1)
s = replace_function(s, 'memo_file_mmap', '''static int memo_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
\tsih_haptic_t *sih_haptic = sih_lock_global();
\tstruct page *page;
\tint ret;

\tif (!sih_haptic)
\t\treturn -ENODEV;
\tif (vma->vm_pgoff || !(vma->vm_flags & VM_SHARED) ||
\t    (vma->vm_flags & VM_EXEC) ||
\t    vma->vm_end - vma->vm_start != PAGE_SIZE << IOCTL_MMAP_PAGE_ORDER) {
\t\tret = -EINVAL;
\t\tgoto out;
\t}
\tpage = virt_to_page(sih_haptic->stream_para.start_buf);
\tvm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
\tvm_flags_clear(vma, VM_MAYEXEC);
\tret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(page),
\t\t\t      vma->vm_end - vma->vm_start, vma->vm_page_prot);
\tif (!ret) {
\t\tvma->vm_private_data = page;
\t\tvma->vm_ops = &sih_vm_ops;
\t\tsih_vma_open(vma);
\t}
out:
\tsih_unlock_global();
\treturn ret;
}''')
s = s.replace('kmalloc(IOCTL_MMAP_BUF_SIZE * IOCTL_MMAP_BUF_SUM, GFP_KERNEL)', 'kzalloc(IOCTL_MMAP_BUF_SIZE * IOCTL_MMAP_BUF_SUM, GFP_KERNEL)')
s = s.replace('__get_free_pages(GFP_KERNEL,', '__get_free_pages(GFP_KERNEL | __GFP_ZERO | __GFP_COMP,')
s = s.replace('\t\thp_err("error get page failed\\n");\n\t\treturn -ENOMEM;', '\t\thp_err("error get page failed\\n");\n\t\tkfree(sih_haptic->stream_para.rtp_ptr);\n\t\tsih_haptic->stream_para.rtp_ptr = NULL;\n\t\treturn -ENOMEM;')
s = s.replace('\tSetPageReserved(virt_to_page(sih_haptic->stream_para.start_buf));\n', '')
s = s.replace('\tkfree(sih_haptic->stream_para.rtp_ptr);\n\tfree_pages(', '\tWRITE_ONCE(sih_haptic->stream_para.stream_mode, false);\n\tcancel_work_sync(&sih_haptic->stream_para.stream_work);\n\tkfree(sih_haptic->stream_para.rtp_ptr);\n\tsih_haptic->stream_para.rtp_ptr = NULL;\n\tfree_pages(')
s = s.replace('\t\t   IOCTL_MMAP_PAGE_ORDER);\n#endif', '\t\t   IOCTL_MMAP_PAGE_ORDER);\n\tsih_haptic->stream_para.start_buf = NULL;\n#endif')
# Treat mapped descriptor links as untrusted, and derive the next slot locally.
s = s.replace('uint32_t count = IOCTL_WAIT_BUFF_VALID_MAX_TRY;', 'uint32_t count = IOCTL_WAIT_BUFF_VALID_MAX_TRY;\n\tunsigned int index = 0;\n\tunsigned int length;')
s = s.replace('opbuf = opbuf->kernel_next;', 'opbuf = sih_haptic->stream_para.start_buf + (i + 1) % IOCTL_MMAP_BUF_SUM;', 1)
s = s.replace('\t\t\thaptic_regmap_write(sih_haptic->regmapp.regmapping,\n\t\t\t\t\t    SIH688X_REG_RTPDATA, opbuf->length,', '\t\t\tlength = READ_ONCE(opbuf->length);\n\t\t\tif (length > sizeof(opbuf->data))\n\t\t\t\tbreak;\n\t\t\thaptic_regmap_write(sih_haptic->regmapp.regmapping,\n\t\t\t\t\t    SIH688X_REG_RTPDATA, length,')
s = s.replace('memset(opbuf->data, 0, opbuf->length);', 'memset(opbuf->data, 0, length);')
s = s.replace('opbuf = opbuf->kernel_next;', 'index = (index + 1) % IOCTL_MMAP_BUF_SUM;\n\t\t\topbuf = sih_haptic->stream_para.start_buf + index;', 1)
s += '''
void sih_del_misc_dev(void)
{
\tmisc_deregister(&sih_haptic_misc);
}
'''
write('haptic_misc.c', s)

patch = []
for p in sorted((HERE / 'before').rglob('*')):
    if p.is_file():
        rel = p.relative_to(HERE / 'before').as_posix()
        new = (HERE / 'candidate' / rel).read_text()
        patch.extend(difflib.unified_diff(p.read_text().splitlines(True), new.splitlines(True), fromfile='a/'+rel, tofile='b/'+rel))
(HERE.parent.parent / 'patches/0160-restore-native-silicon-integrated-vibrator.patch').write_text(''.join(patch))
print('Si driver candidate composed; production build and tests pending')
