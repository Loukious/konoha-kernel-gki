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
a = ap.parse_args()
p = a.source_root / 'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-rx-macro.c'
s = p.read_text()

def function(name):
    match = re.search(r'^static (?:void|int) ' + name + r'\([^;{]*\{', s, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[match.start():end] + '\n'

code = ''.join(function(name) for name in ['lpass_cdc_rx_macro_notify_swr',
              'lpass_cdc_rx_macro_remove_child_devices', 'lpass_cdc_rx_macro_add_child_devices'])
controls = []
with tempfile.TemporaryDirectory(prefix='audio-rx-children-') as td:
    d = Path(td)
    fragment = d / 'rx_children.inc'
    fragment.write_text(code)
    command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
               '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
               str(H / 'tests/audio_rx_children.c'), '-o', str(d / 'children')]
    subprocess.run(command, check=True)
    result = subprocess.run([str(d / 'children')], check=True, capture_output=True, text=True, timeout=30)
    tests = json.loads(result.stdout)
    print(result.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('registered_child_put', 'platform_device_unregister(child);', 'platform_device_put(child);'),
            ('missing_node_reference', 'pdev->dev.of_node = of_node_get(node);', 'pdev->dev.of_node = node;'),
            ('failed_device_leaked', 'platform_device_put(pdev);', '(void)pdev;'),
            ('iterator_reference_leaked', 'of_node_put(node);', '(void)node;'),
            ('child_rollback_removed', 'lpass_cdc_rx_macro_remove_child_devices(rx_priv);', '(void)rx_priv;'),
            ('failed_controller_storage_leaked', 'kfree(controllers);\n\treturn ret;', '(void)controllers;\n\treturn ret;'),
            ('notification_error_lost', 'return ret;\n}\nstatic void', '(void)ret;\n\treturn 0;\n}\nstatic void'),
            ('notification_count_overflow', 'if (rx_priv->swr_ctrl_users == UINT_MAX)', 'if (false)'),
            ('target_withdrawal_missing', 'rx_priv->swr_ctrl_data = NULL;', '(void)rx_priv;'),
            ('notifications_not_drained', 'wait_event(rx_priv->swr_ctrl_wait, !READ_ONCE(rx_priv->swr_ctrl_users));', '(void)rx_priv;'),
        ]
        for name, old, new in variants:
            assert old in code, name
            fragment.write_text(code.replace(old, new, 1))
            build = subprocess.run(command, text=True, capture_output=True)
            assert not build.returncode, name + '\n' + build.stderr
            try:
                run = subprocess.run([str(d / 'children')], capture_output=True, timeout=10,
                    preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert run.returncode, name
                rc = run.returncode
            except subprocess.TimeoutExpired:
                rc = 'bounded-deadlock-timeout'
            controls.append({'name': name, 'rejected': True, 'returncode': rc})
            print('REJECTED', name, flush=True)
            fragment.write_text(code)
report = {'status': 'PASS', 'tests': {'children': tests}, 'negative_controls': controls,
          'source_sha256': {str(p.relative_to(a.source_root)): hashlib.sha256(p.read_bytes()).hexdigest()},
          'harness_sha256': {'audio_rx_children.c': hashlib.sha256((H / 'tests/audio_rx_children.c').read_bytes()).hexdigest()},
          'limits': 'Actual child setup, cleanup and notification functions with pthread gates and mocked '
                    'platform/OF/SoundWire services. Relevant private fields are modeled; full probe, '
                    'ALSA, PM/clock, kernel and hardware behavior are not exercised by this suite.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
