from pathlib import Path
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import subprocess
import sys
import tempfile

H = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('workspace', type=Path)
ap.add_argument('--source-dir', default='onyx')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
groups = [('children', 'verify_rx.py'), ('resources', 'verify_rx_resources.py'),
          ('fir', 'verify_rx_fir.py'), ('cleanup', 'verify_rx_cleanup.py')]
with tempfile.TemporaryDirectory(prefix='rx-integrated-tests-') as td:
    root = Path(td)
    def run(item):
        group, script = item
        command = [sys.executable, str(H / script)]
        if group != 'children':
            command += [str(a.workspace)]
        command += ['--source-root', str(a.workspace / a.source_dir), '--output', str(root / (group + '.json'))]
        if a.negative:
            command += ['--negative']
        job = subprocess.run(command, text=True, capture_output=True)
        if job.returncode:
            raise RuntimeError(group + '\n' + job.stdout + job.stderr)
        report = json.loads((root / (group + '.json')).read_text())
        assert report['status'] == 'PASS'
        print(group + ' PASS: ' + str(len(report['negative_controls'])) + ' rejecting controls', flush=True)
        return group, report
    with ThreadPoolExecutor(max_workers=4) as pool:
        reports = dict(pool.map(run, groups))
hashes = next(iter(reports.values()))['source_sha256']
headers = {}
for name, report in reports.items():
    assert report['source_sha256'] == hashes
    for path, digest in report.get('header_sha256', {}).items():
        assert sha(a.workspace / 'onyx' / path) == digest
        headers[path] = digest
    for path, digest in report['harness_sha256'].items():
        assert sha(H / 'tests' / path) == digest
for path, digest in hashes.items():
    assert sha(a.workspace / a.source_dir / path) == digest
for path, digest in json.loads((H / 'audio-rx-shared-headers-20261031.json').read_text()).items():
    assert sha(a.workspace / 'onyx' / path) == digest
    headers[path] = digest
controls = sum(len(r['negative_controls']) for r in reports.values())
assert controls == (32 if a.negative else 0)
output = {'status': 'PASS', 'suites': reports, 'negative_controls': controls,
          'source_sha256': hashes, 'provider_sha256': headers,
          'verifier_sha256': {p.name: sha(p) for p in H.glob('verify_rx*.py')},
          'harness_sha256': {p.name: sha(p) for p in (H / 'tests').glob('audio_rx_*')},
          'limits': 'Production functions and layouts with mocked services; no live kernel, firmware, audio or phone validation.'}
a.output.write_text(json.dumps(output, indent=2) + '\n')
