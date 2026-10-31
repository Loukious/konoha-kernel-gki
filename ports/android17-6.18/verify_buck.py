#!/usr/bin/env python3
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
    m = re.search(r'(?m)^static [\w \t*\n]+?\b' + name + r'\([^;]*?\)\n\{', source)
    if not m:
        raise ValueError(name)
    e, depth = m.end(), 1
    while depth:
        depth += (source[e] == '{') - (source[e] == '}')
        e += 1
    return source[m.start():e]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    base = args.workspace / args.package / 'drivers/power/supply/mca/mca_strategy/strategy_charger'
    source = (base / 'mca_strategy_buckchg.c').read_text()
    header = (base / 'inc/mca_strategy_buckchg.h').read_text()
    names = [
        'strategy_buckchg_destroy_voters', 'strategy_buckchg_init_voter',
        'strategy_buckchg_validate_config', 'strategy_buckchg_process_event',
        'strategy_buckchg_event_workfunc', 'strategy_buckchg_stop',
        'strategy_buckchg_class_probe', 'strategy_buckchg_class_remove',
        'strategy_buckchg_class_shutdown', 'strategy_buckchg_get_status',
        'strategy_buckchg_check_user', 'strategy_buckchg_if_set_chg_cur',
        'strategy_buckchg_if_set_input_cur', 'strategy_buckchg_if_set_chg_en',
        'strategy_buckchg_if_set_input_suspend', 'strategy_buckchg_if_set_ship_mode',
        'strategy_buckchg_soc_limit_sts_callback', 'mca_charger_buckchg_pwr_boost_sts_callback',
        'strategy_buckchg_smartchg_set_fcc_callback', 'strategy_buckchg_monitor_workfunc',
        'strategy_buckchg_input_suspend', 'strategy_buckchg_input_voltage',
        'strategy_buckchg_set_5v_input', 'strategy_buckchg_set_5v_ichg',
        'strategy_buckchg_set_9v_input', 'strategy_buckchg_set_9v_ichg',
        'strategy_buckchg_read_battery', 'strategy_buckchg_process_soc_limit_change_more',
        'strategy_buckchg_process_soc_limit_change', 'strategy_buckchg_soc_limit_stepper_workfunc_locked',
    ]
    declarations = '\n'.join(function(source, name).split('{', 1)[0].strip() + ';' for name in names)
    definitions = '\n'.join(function(source, name) for name in names)
    stepper = (args.workspace / args.package / 'drivers/power/supply/mca/include/mca/strategy/strategy_soc_limit_stepper.h')
    if not stepper.exists():
        stepper = args.workspace / 'onyx_chargehw/drivers/power/supply/mca/include/mca/strategy/strategy_soc_limit_stepper.h'
    if not stepper.exists():
        stepper = args.workspace / 'onyx_chargebusiness/drivers/power/supply/mca/include/mca/strategy/strategy_soc_limit_stepper.h'
    common = (HERE / 'tests/buck_common.h').read_text().replace('/* HEADER */', header + '\n' + stepper.read_text()).replace('/* DECLARATIONS */', declarations).replace('/* FUNCTIONS */', definitions)
    dtbase = args.workspace / 'baseline/kernel/xiaomi/sm8735-devicetrees/qcom'
    dtfiles = [dtbase / 'mca.dtsi', dtbase / 'onyx-charger-common.dtsi']
    props = {}
    for dtfile in dtfiles:
        content = re.sub(r'/\*.*?\*/|//[^\n]*', '', dtfile.read_text(), flags=re.S)
        block = re.search(r'(?:mca_strategy_buckchg_class:|&)mca_strategy_buckchg_class\s*\{(.*?)\n\s*\};', content, re.S)
        if block is None:
            block = re.search(r'mca_strategy_buckchg_class:\s*mca_strategy_buckchg_class\s*\{(.*?)\n\s*\};', content, re.S)
        assert block, dtfile
        for name, values in re.findall(r'([\w_-]+)\s*=\s*<([^>]*)>', block.group(1)):
            props[name] = [int(v, 0) for v in values.split()]
    stock = 'static void test_stock_config(void) { struct strategy_buckchg_dev cfg={0};\n'
    dtparser = function(source, 'strategy_buckchg_parse_dt')
    for name, field, default in re.findall(r'mca_parse_dts_u32\(info->dev->of_node,\s*"([^"]+)",\s*&info->(\w+),\s*(\w+)\)', dtparser):
        stock += 'cfg.' + field + ' = ' + (str(props[name][0]) if name in props else default) + ';\n'
    for name in ['rev_req_vadp', 'rev_vadp_valid_h', 'rev_vadp_valid_l']:
        for i, value in enumerate(props[name]):
            stock += f'cfg.{name}[{i}] = {value};\n'
    stock += 'assert(!strategy_buckchg_validate_config(&cfg));}\n'
    common += stock
    result = {}
    with tempfile.TemporaryDirectory(prefix='onyx-buck-tests-') as tmp:
        d = Path(tmp)
        for group in ['lifecycle', 'controls', 'queue']:
            test = d / 'test.c'
            out = d / 'test'
            test.write_text(common + '\n' + (HERE / ('tests/buck_' + group + '.c')).read_text())
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(test), '-o', str(out)], check=True)
            run = subprocess.run([str(out)], capture_output=True, text=True, timeout=30)
            if run.returncode:
                raise RuntimeError(run.stdout + run.stderr)
            result[group] = json.loads(run.stdout)
    stop = function(source, 'strategy_buckchg_stop')
    assert stop.index('mca_strategy_ops_unregister') < stop.index('disable_delayed_work_sync') < stop.index('strategy_buckchg_destroy_voters')
    assert 'cancel_delayed_work_sync(' not in source
    assert 'schedule_delayed_work(' not in source and 'mca_queue_delayed_work(' not in source
    assert 'guard(mutex)' not in function(source, 'strategy_buckchg_process_event')
    assert 'policy_lock' not in function(source, 'strategy_buckchg_get_status')
    for name in ['strategy_buckchg_soc_limit_sts_callback', 'mca_charger_buckchg_pwr_boost_sts_callback', 'strategy_buckchg_smartchg_set_fcc_callback']:
        assert 'policy_lock' not in function(source, name)
    assert 'g_buckchg_info' not in source and 'static u8 cur_step' not in source
    result['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    result['stock_device_tree'] = {'profile': 'Onyx buck with inherited mca.dtsi values', 'properties': len(props), 'source_sha256': {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in dtfiles}}
    result['source_sha256'] = {str(f.relative_to(base)): hashlib.sha256(f.read_bytes()).hexdigest() for f in [base / 'mca_strategy_buckchg.c', base / 'inc/mca_strategy_buckchg.h']}
    result['limits'] = 'Extracted production lifecycle, queue, control and status callbacks with pthread locks and mocked kernel workqueue/provider/voter APIs. No live kernel workqueue/SRCU, charging firmware, phone boot, hardware or flash validation.'
    report = json.dumps(result, indent=2) + '\n'
    print(report, end='')
    if args.output:
        args.output.write_text(report)

if __name__ == '__main__':
    main()
