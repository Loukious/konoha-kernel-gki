#!/usr/bin/env python3
"""Exercise WCD transport, register sequences, error handling and runtime votes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:const )?(?:enum \w+|struct \w+|unsigned(?: int)?|[A-Za-z_]\w*)'
                      r'[ \t\n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(f'missing function {name}')
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]+'\n'


def defines(source):
    result = []
    active = False
    for line in source.splitlines():
        if line.startswith('#define') or active:
            result.append(line)
            active = line.endswith('\\')
    return '\n'.join(result)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve()
    base = root/args.package/'drivers/soc/qcom'
    source = (base/'wcd939x-i2c.c').read_text()
    utils = (base/'wcd-usbss-utils.c').read_text()
    priv = (base/'wcd-usbss-priv.h').read_text()
    public = (root/args.package/'include/linux/soc/qcom/wcd939x-i2c.h').read_text()
    entry = next(e for e in json.loads((HERE/'imports.json').read_text())
                 if e['path'] == 'drivers/soc/qcom/wcd939x-i2c.c')
    original = subprocess.check_output(['git', '-C', str(root/entry['project']),
                                       'cat-file', 'blob', entry['blob']], text=True)
    types = ''
    for file in ['wcd-usbss-registers.h', 'wcd-usbss-reg-masks.h', 'wcd-usbss-reg-shifts.h']:
        types += re.sub(r'(?m)^#include[^\n]*', '', (base/file).read_text())+'\n'
    types += '\n'.join(re.findall(r'enum(?: \w+)?\s*\{[^}]*\};', public+source))+'\n'
    types += defines(source)+defines(utils)+defines(priv)
    types += re.search(r'struct wcd_usbss_ctxt \{.*?\n\};', priv, re.S)[0]+'\n'
    types += re.search(r'struct wcd_usbss_saved_state \{.*?\n\};', source, re.S)[0]+'\n'
    types += re.search(r'struct wcd_usbss_reg_mask_val \{.*?\n\};', source, re.S)[0]+'\n'
    types += re.search(r'static const struct wcd_usbss_reg_mask_val coeff_init\[\] = \{.*?\n\};', source, re.S)[0]+'\n'
    io_names = ['wcd_usbss_i2c_transfer', 'wcd_usbss_i2c_write_device',
                'wcd_usbss_i2c_read_device', 'wcd_usbss_page_write',
                'wcd_usbss_regmap_transfer', 'regmap_bus_read',
                'regmap_bus_gather_write', 'regmap_bus_write']
    current_names = ['wcd_usbss_read', 'wcd_usbss_write', 'wcd_usbss_update_bits',
                     'wcd_usbss_update_bits_base', 'wcd_usbss_cache_sync',
                     'wcd_usbss_begin', 'wcd_usbss_end', 'acquire_runtime_env',
                     'release_runtime_env', 'wcd_usbss_standby_control_locked',
                     'wcd_usbss_switch_update_defaults', 'wcd_usbss_validate_display_port_settings',
                     'wcd_usbss_display_port_switch_update', 'wcd_usbss_dpdm_switch_connect',
                     'status_to_str', 'wcd_usbss_pd_pu_enable',
                     'wcd_usbss_dpdm_switch_update_ctx', 'wcd_usbss_switch_update_ctx']
    baseline_names = ['acquire_runtime_env', 'release_runtime_env',
                      'wcd_usbss_standby_control_locked', 'wcd_usbss_switch_update_defaults',
                      'wcd_usbss_validate_display_port_settings', 'wcd_usbss_display_port_switch_update',
                      'wcd_usbss_dpdm_switch_connect', 'status_to_str', 'wcd_usbss_pd_pu_enable',
                      'wcd_usbss_dpdm_switch_update', 'wcd_usbss_switch_update']
    staged = '\n'.join(function(utils,n) for n in io_names)
    staged += '\n'.join(function(source,n) for n in current_names)
    baseline = '\n'.join(function(original,n) for n in baseline_names)
    for name in baseline_names:
        baseline = re.sub(r'\b'+name+r'\b', 'baseline_'+name, baseline)
    cleanup = function(source, 'wcd_usbss_cleanup')
    assert cleanup.index('RCU_INIT_POINTER') < cleanup.index('synchronize_srcu') < cleanup.index('sysfs_remove_group')
    assert cleanup.index('unregister_ucsi_glink_notifier') < cleanup.index('cancel_work_sync') < cleanup.index('regulator_bulk_disable')
    assert cleanup.index('devm_free_irq') < cleanup.index('regulator_bulk_disable')
    probe = function(source, 'wcd_usbss_probe')
    for initialize in ['mutex_init(&priv->notification_lock)', 'INIT_WORK', 'BLOCKING_INIT_NOTIFIER_HEAD']:
        assert probe.index(initialize) < probe.index('register_ucsi_glink_notifier') < probe.index('rcu_assign_pointer')
    assert 'of_find_i2c_device_by_node' not in source
    for name in re.findall(r'EXPORT_SYMBOL_GPL\((\w+)\)', source):
        body = function(source, name)
        assert 'wcd_usbss_get(&index)' in body and 'srcu_read_unlock' in body
    template = HERE/'tests/wcd_harness.c'
    with tempfile.TemporaryDirectory(prefix='onyx-wcd-') as tmp:
        path = Path(tmp)
        text = template.read_text().replace('/* DRIVER_TYPES */', types)
        text = text.replace('/* STAGED_DEFINITIONS */', staged).replace('/* BASELINE_DEFINITIONS */', baseline)
        cleanup_text = (HERE/'tests/cleanup.inc').read_text()
        cleanup_text = cleanup_text.replace('/* CLEANUP_DEFINITIONS */', function(source, 'wcd_usbss_disable_surge_kthread') + function(source, 'wcd_usbss_cleanup'))
        text = text.replace('/* CLEANUP_TEST */', cleanup_text)
        (path/'harness.c').write_text(text)
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration',
                        str(path/'harness.c'), '-o', str(path/'harness')], check=True)
        run = subprocess.run([str(path/'harness')], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(run.stderr)
    report = json.loads(run.stdout)
    report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    report['source_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in sorted(base.glob('wcd*'))}
    report['harness_sha256'] = hashlib.sha256(template.read_bytes()).hexdigest()
    report['cleanup_harness_sha256'] = hashlib.sha256((HERE/'tests/cleanup.inc').read_bytes()).hexdigest()
    report['baseline_blob'] = entry['blob']
    report['limits'] = ('Actual extracted helpers with simulated I2C, regmap, runtime-PM and mutex services. '
                        'Trace parity compares regmap operations with pinned vendor source, not physical bus timing. '
                        'No real SRCU concurrency, interrupts, workqueues, USB/DisplayPort/audio traffic or device boot.')
    text = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
