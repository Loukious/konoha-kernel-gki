#!/usr/bin/env python3
"""Exercise extracted Onyx input helpers with simulated kernel services."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:inline )?(?:const )?(?:enum \w+|struct \w+|unsigned(?: int| long)?|[A-Za-z_]\w*)[ \t\n*]+(?:(?:__init|__exit)\s+)?\b' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--package', default='onyx')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    root = args.workspace.resolve() / args.package
    paths = ['drivers/soc/qcom/panel_event_notifier.c', 'include/linux/soc/qcom/panel_event_notifier.h', 'drivers/input/misc/pm8941-pwrkey.c']
    paths += ['drivers/input/fingerprint/mi_fp/' + n for n in ['fp_driver.c', 'fp_driver.h', 'fp_platform.c', 'fp_netlink.c']]
    sources = {Path(p).name: (root / p).read_text() for p in paths}
    panel, pwr, fp, platform, net = [sources[p] for p in ['panel_event_notifier.c', 'pm8941-pwrkey.c', 'fp_driver.c', 'fp_platform.c', 'fp_netlink.c']]
    groups = {
        'input_registry': [(panel, ['panel_event_notifier_tag_valid', 'panel_event_notifier_register', 'panel_event_notifier_unregister', 'panel_event_notification_trigger']), (pwr, ['mi_display_pwrkey_callback_set', 'mi_display_pwrkey_notify'])],
        'input_hardware': [(platform, ['fp_power_on', 'fp_power_off', 'fp_hw_reset_delay', 'fp_hw_reset', 'fp_enable_irq', 'fp_disable_irq']), (pwr, ['pm8941_debounce_code', 'pm8941_pwrkey_suspend', 'pm8941_pwrkey_resume'])],
        'input_netlink': [(net, ['fp_netlink_send', 'fp_netlink_recv', 'fp_netlink_init', 'fp_netlink_destroy'])],
        'input_lifetime': [(fp, ['fp_ioctl', 'fp_open', 'fp_release', 'fp_free_input_device', 'fp_cleanup', 'fp_probe', 'fp_remove'])],
    }
    result = {}
    with tempfile.TemporaryDirectory(prefix='onyx-input-') as tmp:
        for name, inputs in groups.items():
            functions = '\n'.join(function(src, n) for src, names in inputs for n in names)
            text = (HERE / 'tests' / (name + '.c')).read_text().replace('/* FUNCTIONS */', functions)
            if name == 'input_registry':
                header = sources['panel_event_notifier.h']
                types = '\n'.join(re.findall(r'enum \w+\s*\{[^}]*\};', header))
                for n in ['panel_event_notification_data', 'panel_event_notification']:
                    types += re.search(r'struct ' + n + r' \{.*?\n\};', header, re.S)[0]
                types += re.search(r'typedef void \(\*panel_event_notifier_handler\).*?;', header, re.S)[0]
                types += re.search(r'struct panel_event_notifier_entry \{.*?\n\};', panel, re.S)[0]
                text = text.replace('/* TYPES */', types)
            source, exe = Path(tmp) / (name + '.c'), Path(tmp) / name
            source.write_text(text)
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', '-pthread', str(source), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            if run.returncode:
                raise RuntimeError(name + ': ' + run.stderr)
            result[name] = json.loads(run.stdout)
    probe = function(pwr, 'pm8941_pwrkey_probe')
    assert 'IRQF_NO_AUTOEN' in probe
    assert probe.index('input_register_device') < probe.index('device_init_wakeup') < probe.index('enable_irq(pwrkey->irq)')
    cleanup = function(fp, 'fp_cleanup')
    assert cleanup.index('free_irq') < cleanup.index('panel_event_notifier_unregister') < cleanup.index('fp_netlink_destroy') < cleanup.index('wakeup_source_unregister')
    assert 'char vendor_name[11] = { 0 }' in fp and 'char device_info[30] = { 0 }' in fp
    assert 'sizeof(device_info)' in function(fp, 'fp_ioctl_locked')
    assert 'input_unregister_device' in function(fp, 'fp_free_input_device')
    assert 'FP_MINOR_COUNT' in function(fp, 'fp_probe') and 'inode->i_cdev' in function(fp, 'fp_open')
    result['source_sha256'] = {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths}
    result['harness_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE / 'tests').glob('input_*.c')}
    result['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    result['limits'] = 'Extracted C helpers with mocked kernel services and pthread SRCU models; no live kernel locking, VFS, IRQ, GPIO, regulator, panel, fingerprint firmware or boot validation. FOD primary-display configuration only.'
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')

if __name__ == '__main__':
    main()
