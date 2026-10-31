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
template = H / 'tests/audio_wsa_lifecycle.c.in'
tests, controls, hashes = {}, [], {}
for kind in ['wsa', 'wsa2']:
    prefix = 'lpass_cdc_' + kind + '_macro'
    priv = kind + '_priv'
    relative = 'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-' + kind + '-macro.c'
    source = (args.source_root / relative).read_text()
    hashes[relative] = hashlib.sha256(source.encode()).hexdigest()
    code = ''
    for suffix in ['get_max_state', 'get_cur_state', 'set_cur_state', 'deinit',
                   'cooling_adjust_gain', 'read_array']:
        match = re.search(r'^static (?:int|void) ' + prefix + '_' + suffix + r'\([^;{]*\{', source, re.M)
        assert match, suffix
        end = source.index('\n}', match.end()) + 2
        code += source[match.start():end] + '\n'
    with tempfile.TemporaryDirectory(prefix=kind + '-lifecycle-') as temporary:
        d = Path(temporary)
        test = d / 'test.c'
        test.write_text(template.read_text().replace('@KIND@', kind).replace('@UP@', kind.upper()))
        fragment = d / 'lifecycle.inc'
        fragment.write_text(code)
        command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                   '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
                   str(test), '-o', str(d / 'lifecycle')]
        subprocess.run(command, check=True)
        run = subprocess.run([str(d / 'lifecycle')], check=True, capture_output=True, text=True, timeout=30)
        tests[kind] = json.loads(run.stdout)
        print(kind, run.stdout.strip(), flush=True)
        if args.negative:
            variants = [
                ('thermal_range_lost', f'if (state <= {priv}->thermal_max_state)', 'if (true)'),
                ('component_pointer_retained', f'{priv}->component = NULL;', f'(void){priv};'),
                ('cooling_work_not_cancelled', f'cancel_work_sync(&{priv}->{prefix}_cooling_work);', f'(void){priv};'),
                ('work_component_gate_missing', f'if (!component || !{priv}->dapm_mclk_enable)', f'if (!{priv}->dapm_mclk_enable)'),
                ('array_alignment_lost', 'if (size != num_values * (int)sizeof(*output))', 'if (size / (int)sizeof(*output) != num_values)'),
                ('array_transport_error_lost', 'return of_property_read_u32_array(', '(void)of_property_read_u32_array('),
            ]
            if kind == 'wsa':
                variants.append(('vi_work_not_cancelled', 'cancel_delayed_work_sync(&wsa_priv->wsa_vi_dec_enable_work.dwork);', '(void)wsa_priv;'))
            for name, old, new in variants:
                assert old in code, (kind, name)
                changed = code.replace(old, new, 1)
                if name == 'array_transport_error_lost':
                    changed = changed[:-2] + '\treturn 0;\n}\n'
                fragment.write_text(changed)
                build = subprocess.run(command, capture_output=True, text=True)
                assert not build.returncode, (kind, name, build.stderr)
                try:
                    run = subprocess.run([str(d / 'lifecycle')], capture_output=True, timeout=10,
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
    'source_sha256': hashes,
    'harness_sha256': {template.name: hashlib.sha256(template.read_bytes()).hexdigest()},
    'verifier_sha256': {Path(__file__).name: hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
    'limits': 'Selected production cooling state/work, component deinit and DT-array functions execute '
              'under ASan/UBSan with pthread mutexes and mocked register/OF/workqueue services. '
              'VI work cancellation is observed as a call, not a simulated VI worker. Full probe, '
              'ALSA card teardown, PM/clock, DSP, firmware and hardware operation remain untested.'
}, indent=2) + '\n')
