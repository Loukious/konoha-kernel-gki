#!/usr/bin/env python3
"""Validate integrated amplifier inputs, native metadata and built artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path)
parser.add_argument('artifact', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
read = lambda p: json.loads(p.read_text())
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
workspace, artifact = args.workspace, args.artifact
report = read(artifact / 'report.json')
modules = {m['module']: m for m in report['module_verification']}
native = read(HERE / 'native-evidence.json')
hashes = read(HERE / 'final-sha256.json')
assert hashes == {n: sha(workspace / 'onyx' / n) for n in hashes}
for name, provider in read(HERE / 'provider-inputs.json').items():
    assert sha(workspace / 'onyx' / name) == provider['sha256'], name
result = {}
for target, reference in native.items():
    built = modules[target + '.ko']
    assert sorted(built['depends'].split(',')) == sorted(reference['depends'][0].split(',')), target
    assert sorted(built['aliases']) == sorted(reference['aliases']), target
    module = artifact / 'platform' / (target + '.ko')
    params = subprocess.check_output(['modinfo', '-F', 'parm', str(module)], text=True).splitlines()
    assert params == reference['params'], target
    exports = [line.split()[1] for line in (artifact / 'platform' / (target + '_Module.symvers')).read_text().splitlines()]
    # The discovery list also matched the __ksymtab_strings section name.
    # Native Module.symvers entries contain the actual exported symbols.
    native_exports = [line.split()[1] for line in reference['exports']]
    assert sorted(exports) == sorted(native_exports), target
    makefiles = workspace / 'bazel-bin/onyx' / (target + '_makefiles/makefiles')
    kbuild = (makefiles / 'Kbuild').read_text()
    objects = re.findall(r'^' + target + r'-y \+= (\S+)$', kbuild, re.M)
    expected = [n.removesuffix('.c') + '.o' for n in reference['units']]
    assert sorted(objects) == sorted(expected + (['ddk_modinfo.o'] if 'ddk_modinfo.o' in objects else [])), target
    rows = read(workspace / 'bazel-bin/onyx' / (target + '_makefiles/srcs.json'))
    units = [str(Path(n).relative_to('onyx')) for row in rows for n in row.get('files', []) if n.endswith('.c')]
    assert sorted(units) == sorted(reference['units']), target
    flags = (makefiles / (target + '.cflags_shipped')).read_text()
    assert not re.search(r'Wno-|KBUILD_MODPOST_WARN', flags), target
    config = (artifact / 'platform' / (target + '.config')).read_text().splitlines()
    assert all(n in config for n in ['CONFIG_MODULES=y', 'CONFIG_MODVERSIONS=y', 'CONFIG_TRIM_UNUSED_KSYMS=y'])
    result[target] = built | {'native_units': units, 'native_export_names': exports}
tests = {}
for suite in ['bounds', 'lifecycle']:
    path = HERE / ('integrated-' + suite + '.json')
    data = read(path)
    assert data['status'] == 'PASS'
    tests[suite] = {'sha256': sha(path), 'cases': data['cases'], 'controls': data['controls']}
    if suite == 'lifecycle':
        assert all(sha(Path(n)) == digest for n, digest in data['source_sha256'].items())
assert tests['bounds']['cases'] + tests['lifecycle']['cases'] == 250
args.output.write_text(json.dumps({'status': 'PASS', 'artifact': str(artifact),
    'source_sha256': hashes, 'modules': result, 'tests': tests,
    'limits': 'Build and selected production-function host checks; no physical audio, firmware, concurrent hot-unbind or phone validation.'}, indent=2) + '\n')
print('PASS: three amplifiers, 15 native units, 250 host cases, 18 rejecting controls')
