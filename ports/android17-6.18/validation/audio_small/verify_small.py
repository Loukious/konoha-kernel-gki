#!/usr/bin/env python3
"""Compile extracted production paths with focused kernel-service fixtures."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
PREFIX = 'qcom/opensource/audio-kernel/'


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:inline )?(?:const )?'
                      r'(?:enum \w+|struct \w+|unsigned(?: int)?|[A-Za-z_]\w*)'
                      r'[ \t\n*]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError('missing production function ' + name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def struct(source, name):
    return re.search(r'struct ' + re.escape(name) + r' \{.*?\n\};', source, re.S)[0] + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root', type=Path, default=H / 'private-source')
    p.add_argument('--output', type=Path, default=H / 'host-validation.json')
    p.add_argument('--group', action='append')
    p.add_argument('--negative', action='store_true')
    a = p.parse_args()
    groups = {}
    source = (a.source_root / PREFIX / 'asoc/codecs/sdca-registers-api.c').read_text()
    header = (a.source_root / PREFIX / 'include/asoc/sdca-registers-api.h').read_text()
    groups['sdca'] = (H / 'tests/sdca.c').read_text().replace('/* TYPES */',
        struct(header, 'sdca_debugfs_info') + struct(header, 'sdca_regdump_info'))
    groups['sdca'] = groups['sdca'].replace('/* SOURCE */',
        re.sub(r'(?m)^#include[^\n]*\n', '', source))
    for group in ('lpi', 'haptics', 'dmic'):
        template = H / 'tests' / (group + '.c')
        if not template.exists():
            continue
        selection = json.loads((H / 'tests' / (group + '.json')).read_text())
        source = (a.source_root / PREFIX / selection['source']).read_text()
        code = ''.join(function(source, name) for name in selection['functions'])
        types = ''.join(struct(source, name) for name in selection.get('structs', []))
        if selection.get('defines'):
            definitions = []
            active = False
            for line in source.splitlines():
                if line.startswith('#define') or active:
                    definitions.append(line)
                    active = line.endswith('\\')
            types = '\n'.join(definitions) + '\n' + types
        for pat in selection.get('patterns', []):
            types += re.search(pat, source, re.S)[0] + '\n'
        groups[group] = template.read_text().replace('/* TYPES */', types).replace('/* SOURCE */', code)
    results = {}
    with tempfile.TemporaryDirectory(prefix='onyx-audio-small-tests-') as tmp:
        temp = Path(tmp)

        def execute(name, code, negative=False):
            path = temp / (name + '.c')
            path.write_text(code)
            binary = path.with_suffix('')
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
                            '-Wno-unused-variable', '-Wno-unused-but-set-variable',
                            '-Wno-sign-compare', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                            '-no-pie', '-pthread', str(path), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            if negative:
                assert run.returncode, name + ': mutation survived'
                diagnostic = next((l for l in run.stderr.splitlines() if any(t in l for t in
                    ('Assertion', 'runtime error:', 'ERROR: AddressSanitizer'))), None)
                assert diagnostic, (name, run.stderr)
                return {'rejected': True, 'diagnostic': diagnostic}
            assert not run.returncode, (name, run.stderr)
            return json.loads(run.stdout)

        for name, code in groups.items():
            if not a.group or name in a.group:
                results[name] = execute(name, code)
        controls = {}
        if a.negative:
            for name, (group, old, new) in json.loads((H / 'tests/mutations.json').read_text()).items():
                if a.group and group not in a.group:
                    continue
                assert old in groups[group], (name, old)
                controls[name] = execute(name, groups[group].replace(old, new, 1), True)
    paths = json.loads((H / 'sources.json').read_text())
    report = {'tests': results, 'negative_controls': controls,
              'source_sha256': {path: hashlib.sha256((a.source_root / path).read_bytes()).hexdigest()
                                for path in paths},
              'harness_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                 for path in sorted((H / 'tests').iterdir()) if path.is_file()},
              'limits': 'Extracted production C under ASan/UBSan with simulated Linux, DT, ASoC, '
                        'regmap, SoundWire, PM, clocks and notifier services. No real IRQ/DAPM, '
                        'debugfs proxy, supplier unbind, DSP, electrical or phone boot validation.'}
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
