#!/usr/bin/env python3
"""Exercise the actual machine registry against lifetime and failure scenarios."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def function(text, name):
    match = re.search(r'(?m)^(?:static )?[^;{}\n]*\b' + re.escape(name) + r'\([^;]*?\)\n\{', text)
    if not match:
        raise ValueError(name)
    depth = 1
    for token in re.finditer(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|/\*.*?\*/|//[^\n]*|[{}]', text[match.end():], re.S):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if not depth:
                return text[match.start():match.end() + token.end()]
    raise ValueError(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--cc', default=shutil.which('clang') or shutil.which('cc'))
    args = parser.parse_args()
    compiler = args.cc
    assert compiler
    source_path = args.source_root / 'qcom/opensource/audio-kernel/asoc/sun.c'
    source = source_path.read_text()
    names = ['smartpa_type_index', 'check_smartpa_type', 'get_smartpa_type',
             'set_smartpa_type', 'fmt_single_name', 'register_smartpa_codec',
             'unregister_smartpa_codec', 'set_smartpa_codec', 'smartpa_dt_count',
             'smartpa_ready', 'smartpa_dai_link_select']
    body = '\n\n'.join(function(source, name) for name in names)
    fixture = Path(__file__).parent / 'tests/smartpa_registry.c'
    template = fixture.read_text()
    mutations = {
        'borrowed_dai_storage': ('dai = kstrdup(dai_name, GFP_KERNEL);', 'dai = (char *)dai_name;'),
        'missing_device_release': ('put_device(held);', '(void)held;'),
        'mixed_amplifier_families': ('if (smartpa_num && smartpa_num != index)', 'if (false && smartpa_num && smartpa_num != index)'),
        'duplicate_registration': ('if (smartpa_devices[i] != dev)\n\t\t\tcontinue;\n\t\tif (strcmp', 'if (true || smartpa_devices[i] != dev)\n\t\t\tcontinue;\n\t\tif (strcmp'),
        'borrowed_card_dai_storage': ('devm_kstrdup(card->dev,\n\t\t\t\t\t\t\t  smartpa_dails[j].dai_name,\n\t\t\t\t\t\t\t  GFP_KERNEL)', 'smartpa_dails[j].dai_name'),
        'premature_card_registration': ('if (!smartpa_ready(card))', 'if (false && !smartpa_ready(card))'),
    }
    report = {'status': 'PASS', 'negative_controls': [],
              'source_sha256': {str(source_path): hashlib.sha256(source_path.read_bytes()).hexdigest()},
              'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest(),
              'limits': 'Mocked allocation, device tree, devices and ALSA structures. Pthread tests do not establish real kernel/ALSA or hardware concurrency.'}
    cases = [('positive', body)]
    if args.negative:
        for name, (old, new) in mutations.items():
            assert body.count(old) == 1, (name, body.count(old))
            cases.append((name, body.replace(old, new)))
    with tempfile.TemporaryDirectory(prefix='onyx-machine-smartpa-') as tmp:
        for name, code in cases:
            unit = Path(tmp) / (name + '.c')
            binary = Path(tmp) / name
            unit.write_text(template.replace('/* PRODUCTION_SOURCE */', code))
            subprocess.run([compiler, '-std=gnu11', '-g', '-O1', '-Wall', '-Werror',
                            '-Wno-unused-function', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-pthread', str(unit), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            if name == 'positive':
                assert run.returncode == 0, run.stdout + run.stderr
                report['result'] = run.stdout.strip()
            else:
                assert run.returncode != 0, name + ' was not rejected'
                report['negative_controls'].append({'name': name, 'compiled': True, 'rejected': True})
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(report['result'])
    print('Rejected', len(report['negative_controls']), 'compiled regressions')


if __name__ == '__main__':
    main()
