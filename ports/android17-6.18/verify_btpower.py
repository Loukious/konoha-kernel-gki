#!/usr/bin/env python3
"""Exercise native Bluetooth power functions with host fault injection."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PREFIX = 'qcom/opensource/bt-kernel/'
FUNCTIONS = {
    'btpower_fmd': ['perform_fmd_operation'],
    'btpower_resources': ['vreg_configure', 'vreg_enable', 'vreg_disable_retention',
        'vreg_enable_retention', 'vreg_disable', 'bt_clk_enable', 'bt_clk_disable',
        'handle_pwr_disable_req', 'bt_regulators_pwr', 'uwb_regulators_pwr',
        'platform_regulators_pwr'],
    'btpower_wire': ['fmd_set_sdam_bit', 'fmd_reboot_on_usb_detection',
        'fmd_write_stop_counter', 'bt_aop_send_msg', 'btpower_aop_set_vreg_param', 'btpower_enable_ipa_vreg'],
    'btpower_requests': ['btpower_request_put', 'bt_power_vote', 'schedule_client_voting'],
    'btpower_lifetime': ['btpower_register_slimdev', 'bt_open', 'bt_ioctl'],
    'btpower_gpio': ['bt_pull_resetb', 'bt_resetb_operation', 'bt_release_gpios',
        'bt_configure_gpios'],
}
MUTATIONS = {
    'IPA_rebind_inherits_stale_flag': ('btpower_wire', 'if (pdata->ipa_config_done) {', 'static bool once; if (once || (once = pdata->ipa_config_done)) {'),
    'FMD_published_before_clock_success': ('btpower_fmd', 'ret = bt_clk_enable(pwr_data->bt_chip_clk);', 'pwr_data->is_fmd_mode_enable = true; ret = bt_clk_enable(pwr_data->bt_chip_clk);'),
    'discard_regulator_configuration_error': ('btpower_resources',
        'rc = vreg_configure(vreg, false);', 'vreg_configure(vreg, false);'),
    'unchecked_nvmem_error_pointer': ('btpower_wire', 'if (IS_ERR(buf)) {', 'if (false) {'),
    'read_past_AOP_string': ('btpower_wire', 'qmp_send(plat_priv->qmp, frame, sizeof(frame))',
        'qmp_send(plat_priv->qmp, mbox_msg, sizeof(frame))'),
    'worker_reference_missing': ('btpower_requests', 'refcount_set(&pending->refs, 2)',
        'refcount_set(&pending->refs, 1)'),
    'double_hardware_unlock': ('btpower_requests', 'mutex_unlock(&btpower_operation_lock);',
        'mutex_unlock(&btpower_operation_lock); mutex_unlock(&btpower_operation_lock);'),
    'old_file_rebind': ('btpower_lifetime',
        '(unsigned long)file->private_data == btpower_generation', 'true'),
    'unowned_GPIO_free': ('btpower_gpio', 'if (pwr_data->resetb_requested) {',
        'if (pwr_data->bt_gpio_resetb >= 0) {'),
}


def function(source, name):
    match = re.search(r'(?m)^(?:static )?(?:inline )?[\w *]+\b' + re.escape(name) +
                      r'\s*\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    args = parser.parse_args()
    root = args.workspace / args.source_dir
    source = (root / PREFIX / 'pwr/btpower.c').read_text()
    header = (root / PREFIX / 'include/btpower.h').read_text()
    enums = '\n'.join(re.findall(r'enum (?:power_modes|cores|retention_states|plt_pwr_state)\s*\{.*?\};', header, re.S))
    enums += '\nenum {BT_RESET_GPIO=0,BT_SW_CTRL_GPIO=1};\n#define DEFAULT_INVALID_VALUE -1\n#define EPROBE_DEFER 517\n'
    request = re.search(r'struct btpower_request \{.*?\};', source, re.S).group()
    tests = {}
    with tempfile.TemporaryDirectory(prefix='onyx-btpower-tests-') as temporary:
        for name in MUTATIONS if args.negative else FUNCTIONS:
            group = MUTATIONS[name][0] if args.negative else name
            selected = '\n'.join(function(source, item) for item in FUNCTIONS[group])
            if args.negative:
                before, after = MUTATIONS[name][1:]
                assert before in selected, name
                selected = selected.replace(before, after)
            code = (HERE / 'tests' / (group + '.c')).read_text()
            code = code.replace('/* ENUMS */', enums).replace('/* REQUEST */', request)
            code = code.replace('/* FUNCTIONS */', selected)
            code = code.replace('/* PARAM_ENUMS */', '\n'.join(re.findall(r'enum (?:btpower_vreg_param|btpower_tcs_seq)\s*\{.*?\};', source, re.S)))
            path = Path(temporary) / (name + '.c')
            binary = path.with_suffix('')
            path.write_text(code)
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-no-pie', '-Wall', '-Wextra',
                '-Wno-unused-function', '-Wno-unused-parameter',
                '-Werror=implicit-function-declaration', str(path), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if args.negative:
                assert result.returncode, name
                tests[name] = {'rejected': True, 'returncode': result.returncode,
                    'diagnostic': next((line for line in result.stderr.splitlines()
                        if 'Assertion' in line or 'runtime error:' in line or 'ERROR: AddressSanitizer' in line), result.stderr[:300])}
            else:
                assert not result.returncode, result.stdout + result.stderr
                tests[name] = json.loads(result.stdout)
    report = {
        'tests': tests,
        'source_sha256': {PREFIX + name: hashlib.sha256((root / PREFIX / name).read_bytes()).hexdigest()
            for name in ['pwr/btpower.c', 'include/btpower.h']},
        'harness_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted((HERE / 'tests').glob('btpower_*.c'))},
        'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
        'limits': 'Compiled production functions with mocked regulator/GPIO/clock, nvmem/AOP, task-independent file admission, queue/completion and host synchronization. Withdrawal is a host SRCU model around the actual entry functions; probe/remove are built, not executed in this harness. No live hardware, firmware, kernel IRQ or Bluetooth/audio validation.',
    }
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
