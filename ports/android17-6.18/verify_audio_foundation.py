#!/usr/bin/env python3
"""Run real source bodies with allocator, callback and threaded lifecycle models."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PREFIX = 'qcom/opensource/audio-kernel/'
GROUPS = {'snd': ('soc/snd_event.c', ['lifecycle', 'partial', 'stale', 'detach', 'faults', 'rollback', 'races', 'nulls']),
          'pdr': ('dsp/audio_pdr.c', ['faults', 'lifecycle', 'bounds'])}

def source_body(source):
    return '\n'.join(line for line in source.splitlines()
                     if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)', line))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative', action='store_true')
    args = parser.parse_args()
    results, hashes, harnesses = {}, {}, {}
    with tempfile.TemporaryDirectory(prefix='onyx-audio-foundation-tests-') as tmp:
        temp = Path(tmp)
        for group, (path, scenarios) in GROUPS.items():
            data = (args.workspace / args.source_dir / PREFIX / path).read_bytes()
            hashes[PREFIX + path] = hashlib.sha256(data).hexdigest()
            harness = HERE / 'tests' / ('audio_foundation_' + group + '.c')
            fixture = harness.read_bytes()
            harnesses[harness.name] = hashlib.sha256(fixture).hexdigest()
            output = temp / (group + '.c')
            output.write_text(fixture.decode().replace('/* SOURCE */', source_body(data.decode())))
            binary = temp / group
            run = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                                  '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                  '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration',
                                  str(output), '-o', str(binary)], capture_output=True, text=True)
            assert not run.returncode, run.stdout + run.stderr
            for scenario in scenarios:
                if args.negative and scenario == 'rollback':
                    continue
                run = subprocess.run([str(binary), scenario], capture_output=True, text=True, timeout=30)
                if args.negative:
                    assert run.returncode, 'Original body unexpectedly passed ' + group + ':' + scenario
                    results[group + ':' + scenario] = {'rejected': True, 'returncode': run.returncode,
                        'diagnostic': next((line for line in run.stderr.splitlines() if 'Assertion' in line or 'ERROR: AddressSanitizer' in line), run.stderr[:200])}
                else:
                    assert not run.returncode, run.stdout + run.stderr
                    results.update(json.loads(run.stdout))
    report = {'tests': results, 'source_sha256': hashes, 'harness_sha256': harnesses,
              'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
              'limits': 'Actual source bodies run against simulated allocators, devices and PDR services. Includes a two-thread master registration race; no real kernel PDR callbacks, sound firmware, IRQ, suspend/resume or hardware validation.'}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
