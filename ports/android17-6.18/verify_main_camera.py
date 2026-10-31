#!/usr/bin/env python3
"""Run the camera port's production-function tests with mocked kernel services."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

H = Path(__file__).resolve().parent
SUITES = ['memory', 'nodes', 'master', 'dma_fence', 'synx',
          'sync_lifecycle', 'smmu', 'minidump', 'clients']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    workspace = args.workspace.resolve()
    source = args.source_root or workspace / args.source_dir
    catalog = H / 'main-camera-sources.json'
    if not catalog.exists():
        catalog = H / 'sources.json'
    source_hashes = {name: sha(source / name) for name in json.loads(catalog.read_text())}
    reports = {}
    with tempfile.TemporaryDirectory(prefix='main-camera-validation-') as td:
        directory = Path(td)

        def run(name):
            out = directory / (name + '.json')
            command = [sys.executable, str(H / ('verify_main_camera_' + name + '.py')),
                       '--source-root', str(source), '--output', str(out)]
            if name in ['synx', 'smmu']:
                command += ['--provider-root', str(workspace / 'onyx')]
            if name == 'smmu':
                command += ['--common-root', str(workspace / 'common')]
            if args.negative:
                command.append('--negative')
            result = subprocess.run(command, text=True, capture_output=True, timeout=180)
            if result.returncode:
                raise RuntimeError(name + ' failed:\n' + result.stdout + result.stderr)
            return name, json.loads(out.read_text())

        with ThreadPoolExecutor(max_workers=3) as executor:
            for task in as_completed([executor.submit(run, name) for name in SUITES]):
                name, report = task.result()
                assert report['status'] == 'PASS'
                assert all(x['rejected'] for x in report['negative_controls'])
                reports[name] = report
                print(name + ': PASS', flush=True)

    receipts = {'harness_sha256': {}, 'provider_sha256': {}, 'common_sha256': {}}
    roots = {'harness_sha256': H / 'tests', 'provider_sha256': workspace / 'onyx',
             'common_sha256': workspace / 'common'}
    for report in reports.values():
        for name, digest in report['source_sha256'].items():
            assert source_hashes[name] == digest, name
        for key in receipts:
            for name, digest in report.get(key, {}).items():
                assert sha(roots[key] / name) == digest, name
                assert receipts[key].get(name, digest) == digest, name
                receipts[key][name] = digest
    assert source_hashes == {name: sha(source / name) for name in source_hashes}
    verifier_hashes = {('verify_main_camera_' + name + '.py'):
                      sha(H / ('verify_main_camera_' + name + '.py')) for name in SUITES}
    verifier_hashes[Path(__file__).name] = sha(Path(__file__))
    result = {'status': 'PASS', 'suites': {name: reports[name] for name in SUITES},
              'negative_controls': sum(len(r['negative_controls']) for r in reports.values()),
              'source_sha256': source_hashes, **receipts, 'verifier_sha256': verifier_hashes,
              'limits': 'Selected production functions with mocked kernel services. '
                        'Native device execution, firmware, camera HAL, real VFS/IRQ/DMA/RCU '
                        'and full VM operation are not validated. See each suite for scope.'}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('All nine camera suites passed; ' + str(result['negative_controls']) + ' rejecting controls')


if __name__ == '__main__':
    main()
