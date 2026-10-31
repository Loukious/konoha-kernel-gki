from pathlib import Path
import argparse
import hashlib
import json
import re
import resource
import subprocess
import tempfile

H = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--source-root', type=Path, default=H / 'private-source')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
args = ap.parse_args()
tests, controls, hashes, harnesses = {}, [], {}, {}

for kind in ['wsa', 'wsa2']:
    f = 'lpass_cdc_' + kind + '_macro'
    v = kind + '_priv'
    relative = 'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-' + kind + '-macro.c'
    source = (args.source_root / relative).read_text()
    hashes[relative] = hashlib.sha256(source.encode()).hexdigest()
    code = ''
    for suffix in ['notify_swr', 'remove_child_devices', 'add_child_devices']:
        match = re.search(r'^static (?:int|void) ' + f + '_' + suffix + r'\([^;{]*\{', source, re.M)
        assert match, suffix
        end = source.index('\n}', match.end()) + 2
        code += source[match.start():end] + '\n'
    harness = H / 'tests' / ('audio_' + kind + '_children.c')
    harnesses[harness.name] = hashlib.sha256(harness.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix='audio-' + kind + '-children-') as temporary:
        d = Path(temporary)
        fragment = d / 'children.inc'
        fragment.write_text(code)
        command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                   '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
                   str(harness), '-o', str(d / 'children')]
        subprocess.run(command, check=True)
        run = subprocess.run([str(d / 'children')], text=True, capture_output=True, timeout=30, check=True)
        tests[kind] = json.loads(run.stdout)
        print(kind, run.stdout.strip(), flush=True)
        if args.negative:
            variants = [
                ('registered_child_put', 'platform_device_unregister(child);', 'platform_device_put(child);'),
                ('missing_node_reference', 'pdev->dev.of_node = of_node_get(node);', 'pdev->dev.of_node = node;'),
                ('failed_device_leaked', 'platform_device_put(pdev);', '(void)pdev;'),
                ('iterator_reference_leaked', 'of_node_put(node);', '(void)node;'),
                ('child_rollback_removed', f'{f}_remove_child_devices({v});', f'(void){v};'),
                ('failed_storage_leaked', 'kfree(controllers);\n\treturn ret;', '(void)controllers;\n\treturn ret;'),
                ('notification_error_lost', 'return ret;\n}\nstatic void', '(void)ret;\n\treturn 0;\n}\nstatic void'),
                ('notification_count_overflow', f'if ({v}->swr_ctrl_users == UINT_MAX)', 'if (false)'),
                ('target_withdrawal_missing', f'{v}->swr_ctrl_data = NULL;', f'(void){v};'),
                ('notifications_not_drained', f'wait_event({v}->swr_ctrl_wait, !READ_ONCE({v}->swr_ctrl_users));', f'(void){v};'),
            ]
            for name, old, new in variants:
                assert old in code, (kind, name)
                fragment.write_text(code.replace(old, new, 1))
                build = subprocess.run(command, text=True, capture_output=True)
                assert not build.returncode, (kind, name, build.stderr)
                try:
                    run = subprocess.run([str(d / 'children')], capture_output=True, timeout=10,
                        preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                    assert run.returncode, (kind, name)
                    rc = run.returncode
                except subprocess.TimeoutExpired:
                    rc = 'bounded-deadlock-timeout'
                controls.append({'module': kind, 'name': name, 'rejected': True, 'returncode': rc})
                print('REJECTED', kind, name, flush=True)
                fragment.write_text(code)

args.output.write_text(json.dumps({
    'status': 'PASS', 'tests': tests, 'negative_controls': controls,
    'source_sha256': hashes, 'harness_sha256': harnesses,
    'verifier_sha256': {Path(__file__).name: hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
    'limits': 'The actual child setup, cleanup and notification functions execute with pthread synchronization '
              'and mocked OF/platform/SoundWire services. The suite covers known child filtering, allocation '
              'failures, reference rollback and reentrant notification/removal; it does not exercise complete '
              'probe, ALSA, PM, firmware or hardware.'
}, indent=2) + '\n')
