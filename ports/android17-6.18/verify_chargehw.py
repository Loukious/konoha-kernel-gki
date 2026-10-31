#!/usr/bin/env python3
"""Check actual staged MCA registries, charger I/O bounds and fault unwinding."""
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
    m = re.search(r'(?m)^[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{', source)
    if not m:
        raise ValueError(name)
    end = m.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--support-package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve()
    base = 'drivers/power/supply/mca/'
    paths = [base+'mca_platform/'+p+'.c' for p in ['mca_platform_base', 'mca_platform_fg_ic_ops', 'mca_platform_cp_class']]
    paths += [base+'mca_hardware_ic/fuelgauge_ic/bq27z561/bq27z561.c', base+'mca_hardware_ic/charge_pump_ic/sc8581/sc8581.c']
    sources = [(root/args.package/p).read_text() for p in paths]
    bus, fgops, cp, fg, sc = sources
    logpath = base+'mca_common/mca_log.c'
    log = (root/args.support_package/logpath).read_text()
    groups = {
        'registry': [(fgops, ['platform_get_fg_ic_ops', 'platform_fg_ic_ops_register', 'platform_fg_ic_ops_unregister', 'platform_fg_ops_get_soc']),
                     (log, ['mca_log_charge_log_register', 'mca_log_charge_log_unregister', 'mca_log_get_charge_log_ops', 'mca_log_charge_log_dump_log_head', 'mca_log_charge_log_dump_log_context'])],
        'io': [(bus, ['bank_to_i2c', 'mca_platform_base_regmap_write', 'mca_platform_base_regmap_read']),
               (fg, ['__fg_read_block', '__fg_write_block', 'checksum', 'fg_mac_read_block']),
               (sc, ['sc8581_init_protection'])],
        'class': [(cp, ['cp_sysfs_remove_groups', 'cp_sysfs_create_group'])],
    }
    protection_calls = re.findall(r'ret = (sc8581_\w+)\(', function(sc, 'sc8581_init_protection'))
    mocks = '\n'.join('#define '+name+'(...) protection_call()' for name in protection_calls)
    results = {}
    with tempfile.TemporaryDirectory(prefix='onyx-chargehw-') as tmp:
        p = Path(tmp)
        for name, parts in groups.items():
            definitions = '\n\n'.join(function(source, fn) for source, names in parts for fn in names)
            template = (HERE/'tests'/('chargehw_'+name+'.c')).read_text()
            (p/'harness.c').write_text(template.replace('/* STAGED_DEFINITIONS */', definitions).replace('/* PROTECTION_MOCKS */', mocks))
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread',
                            '-Werror=implicit-function-declaration', str(p/'harness.c'), '-o', str(p/'harness')], check=True)
            run = subprocess.run([str(p/'harness')], capture_output=True, text=True, check=True)
            results[name] = json.loads(run.stdout)
    registry_guards = {}
    getter_names = {'fg_ic_ops': 'platform_get_fg_ic_ops', 'buckchg_class': 'platform_class_buckchg_get_ops_data',
                    'cp_class': 'platform_cp_class_get_ic_ops', 'loadsw_class': 'platform_loadsw_class_get_ic_ops',
                    'wireless_class': 'platform_wireless_class_get_ic_ops'}
    for name, getter in getter_names.items():
        s = (root/args.package/base/'mca_platform'/('mca_platform_'+name+'.c')).read_text()
        refs = len(re.findall(r'\b'+getter+r'\(', s)) - 1
        guards = s.count('guard(srcu)(&mca_ops_srcu)')
        assert refs == guards, (name, refs, guards)
        registry_guards[name] = guards
    cleanup = function(sc, 'sc8581_cleanup')
    assert cleanup.index('platform_class_cp_unregister_ops') < cleanup.index('mca_log_charge_log_unregister') < cleanup.index('mca_debugfs_remove_group') < cleanup.index('free_irq') < cleanup.index('cancel_delayed_work_sync')
    remove = function(fg, 'bq_fg_remove')
    assert remove.index('mca_log_charge_log_unregister') < remove.index('fg_sysfs_remove_group') < remove.index('platform_fg_ic_ops_unregister')
    results['provider_guards'] = registry_guards
    results['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    results['source_sha256'] = {p: hashlib.sha256((root/args.package/p).read_bytes()).hexdigest() for p in paths}
    results['source_sha256'][logpath] = hashlib.sha256((root/args.support_package/logpath).read_bytes()).hexdigest()
    results['limits'] = 'Actual extracted functions with mock kernel services; SRCU lifetime modeled by pthread read/write locks. No live kernel concurrency, firmware, battery, I2C hardware, charging, PD negotiation or boot validation.'
    serialized = json.dumps(results, indent=2)+'\n'
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end='')


if __name__ == '__main__':
    main()
