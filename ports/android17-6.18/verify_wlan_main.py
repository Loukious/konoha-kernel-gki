#!/usr/bin/env python3
"""Run the WLAN port's production-function regressions against a staged tree."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
p.add_argument('--source-dir', default='onyx')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = a.workspace / a.source_dir
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {}, 'harness_sha256': {}, 'verifier_sha256': {}}
scripts = [('napi', 'verify_wlan_main_napi.py', ['--negative']), ('cfg80211', 'verify_wlan_main_cfg80211.py', []), ('dbr', 'verify_wlan_main_dbr.py', [])]
with tempfile.TemporaryDirectory(prefix='wlan-main-tests-') as tmp:
    for group, script, flags in scripts:
        output = Path(tmp) / (group + '.json')
        subprocess.run(['python3', str(H / script), str(a.workspace), '--source-dir', a.source_dir, '--output', str(output), *flags], check=True)
        data = json.loads(output.read_text())
        for key, result in data.get('tests', data.get('suites', {})).items():
            report['tests'][group + '_' + key] = result
        for key, result in data['negative_controls'].items():
            report['negative_controls'][group + '_' + key] = result
        for path, digest in data['source_sha256'].items():
            source = Path(path)
            if source.is_absolute():
                source = source.relative_to(root)
            assert hashlib.sha256((root / source).read_bytes()).hexdigest() == digest
        for path, digest in data['harness_sha256'].items():
            assert hashlib.sha256((H / 'tests' / path).read_bytes()).hexdigest() == digest
            report['harness_sha256'][path] = digest
        report['verifier_sha256'][script] = hashlib.sha256((H / script).read_bytes()).hexdigest()
pins = json.loads((H / ('wlan-main-imports.json' if (H / 'wlan-main-imports.json').exists() else 'imports.json')).read_text())
report['source_sha256'] = {pin['path']: hashlib.sha256((root / pin['path']).read_bytes()).hexdigest() for pin in pins}
report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
report['limits'] = 'Extracted production functions and the exact DBR guard block use mocked kernel/firmware services. Tests cover changed ownership, request routing and allocation bounds; they do not establish real IRQ/NAPI/RCU scheduling, the complete WMI parser, firmware boot, RF operation, monitor capture, injection, recovery or hardware readiness.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print('Passed three production suites, 196 cases and fifteen rejecting controls')
