#!/usr/bin/env python3
"""Exercise actual provider publication/withdrawal and HDMI ownership paths."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
PREFIX = 'qcom/opensource/mm-drivers/msm_ext_display/'


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    m = re.search(r'(?m)^(?:static )?(?:const )?'
                  r'(?:struct \w+|[A-Za-z_]\w*)[ \t\n*]+\b' + name +
                  r'\([^;]*?\)\n\{', source)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root', type=Path, default=H / 'private-source')
    p.add_argument('--consumer-root', type=Path, default=Path('/tmp/onyx-audio-small-handoff/private-source'))
    p.add_argument('--output', type=Path, default=H / 'host-validation.json')
    p.add_argument('--negative', action='store_true')
    a = p.parse_args()
    source = (a.source_root / PREFIX / 'src/msm_ext_display.c').read_text()
    header = (a.source_root / PREFIX / 'include/msm_ext_display.h').read_text()
    source_functions = ['msm_ext_disp_codec_valid', 'msm_ext_disp_name',
        'msm_ext_disp_process_audio', 'msm_ext_disp_validate_and_get',
        'msm_ext_disp_update_audio_ops', 'msm_ext_disp_audio_config',
        'msm_ext_disp_audio_notify', 'msm_ext_disp_prepare_ready',
        'msm_ext_disp_register_audio_codec', 'msm_ext_disp_withdraw_audio_locked',
        'msm_ext_disp_unregister_audio_codec', 'msm_ext_disp_select_audio_codec']
    code = ''.join(function(source, name) for name in source_functions)
    types = re.sub(r'(?m)^#include[^\n]*\n', '', header)
    types += re.search(r'struct msm_ext_disp \{.*?\n\};', source, re.S)[0]
    template = (H / 'tests/provider.c').read_text()
    generated = {'provider': template.replace('/* TYPES */', types).replace('/* SOURCE */', code)}
    consumer_path = 'qcom/opensource/audio-kernel/asoc/codecs/msm_hdmi_codec_rx.c'
    consumer = (a.consumer_root / consumer_path).read_text()
    consumer_template = H / 'tests/consumer.c'
    if consumer_template.exists():
        private_type = re.search(r'struct msm_ext_disp_audio_codec_rx_data \{.*?\n\};', consumer, re.S)[0]
        definitions = ''.join(function(consumer, n) for n in
            ['msm_ext_disp_audio_codec_rx_probe', 'msm_ext_disp_audio_codec_rx_remove'])
        generated['consumer'] = consumer_template.read_text().replace('/* TYPES */', private_type).replace('/* SOURCE */', definitions)
    results, controls = {}, {}
    with tempfile.TemporaryDirectory(prefix='onyx-extdisp-audio-test-') as tmp:
        temp = Path(tmp)

        def execute(name, code, negative=False):
            path = temp / (name + '.c')
            path.write_text(code)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                            '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-unused-function', '-Wno-unused-variable',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-no-pie', str(path), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            if negative:
                assert run.returncode, name + ': mutation survived'
                diagnostic = next((l for l in run.stderr.splitlines() if any(t in l for t in
                    ('Assertion', 'runtime error:', 'ERROR: AddressSanitizer'))), None)
                assert diagnostic, (name, run.stderr)
                return {'rejected': True, 'diagnostic': diagnostic}
            assert not run.returncode, (name, run.stderr)
            return json.loads(run.stdout)

        for name, code in generated.items():
            results[name] = execute(name, code)
        if a.negative:
            mutations = json.loads((H / 'tests/mutations.json').read_text())
            for name, (group, before, after) in mutations.items():
                assert before in generated[group], name
                controls[name] = execute(name, generated[group].replace(before, after, 1), True)
    paths = json.loads((H / 'sources.json').read_text())
    report = {'tests': results, 'negative_controls': controls,
        'source_sha256': {path: hashlib.sha256((a.source_root / path).read_bytes()).hexdigest() for path in paths},
        'consumer_sha256': {consumer_path: hashlib.sha256(consumer.encode()).hexdigest()},
        'harness_sha256': {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted((H / 'tests').iterdir())},
        'limits': 'Actual production functions compiled with ASan/UBSan and pthread serialization. '
                  'Kernel device, mutex, extcon, display-interface lookup and ASoC services are simulated. '
                  'No physical HDMI/DP, real extcon notifier ordering, ASoC card lifecycle or phone boot.'}
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
