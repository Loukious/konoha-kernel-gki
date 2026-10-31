#!/usr/bin/env python3
"""Exercise the native SWR provider and regmap implementation with host mocks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
PREFIX = 'qcom/opensource/audio-kernel/'
SUITES = {
    'swr_provider.c': ('soc/soundwire.c', ['lifecycle', 'ports', 'exhaustion',
        'faults', 'groups', 'drain', 'driver_drain', 'init_faults']),
    'swr_regmap.c': ('soc/regmap-swr.c', ['bounds', 'unaligned', 'paging',
        'page_faults', 'page_crossing', 'allocations', 'register_edges', 'config_widths']),
}
MUTATIONS = {
    'callback_storage_freed_by_sync_response': ('swr_provider.c',
        'refcount_set(&txn->refs, 2);', 'refcount_set(&txn->refs, 1);', 'ports'),
    'controller_teardown_without_drain': ('swr_provider.c',
        'wait_event(master->call_wait, !atomic_read(&master->active_calls));',
        '(void)0;', 'drain'),
    'driver_teardown_without_drain': ('swr_provider.c',
        'wait_event(swr->driver_wait, !atomic_read(&swr->driver_calls));',
        '(void)0;', 'driver_drain'),
    'short_write_underflow': ('swr_regmap.c',
        'if (count < addr_bytes + VAL_BYTES)\n\t\treturn -EINVAL;', '', 'bounds'),
    'native_three_byte_paged_stride': ('swr_regmap.c',
        'stride = addr_bytes + VAL_BYTES;', 'stride = ADDR_BYTES + VAL_BYTES;', 'paging'),
    'stale_page_cache_after_reset': ('swr_regmap.c',
        'if (generation != swr->scp_cached_generation) {', 'if (false) {', 'paging'),
}


def clean(body):
    return '\n'.join(line for line in body.splitlines()
                     if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)', line))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = args.workspace / args.source_dir
    results = {}
    provider = root / PREFIX / 'include/soc/audio_mod_devicetable.h'
    if not provider.exists():
        provider = args.workspace / 'onyx' / PREFIX / 'include/soc/audio_mod_devicetable.h'
    headers = clean(provider.read_text()) + '\n' + clean((root / PREFIX / 'include/soc/soundwire.h').read_text())
    fixtures = list(MUTATIONS) if args.negative else list(SUITES)
    with tempfile.TemporaryDirectory(prefix='onyx-swr-test-') as temporary:
        for fixture in fixtures:
            name = MUTATIONS[fixture][0] if args.negative else fixture
            source, scenarios = SUITES[name]
            body = (root / PREFIX / source).read_text()
            if args.negative:
                _, before, after, scenario = MUTATIONS[fixture]
                assert body.count(before) == 1, fixture
                body = body.replace(before, after)
                scenarios = [scenario]
            code = (HERE / 'tests' / name).read_text().replace('/* HEADERS */', headers).replace('/* SOURCE */', clean(body))
            path = Path(temporary) / name
            binary = path.with_suffix('')
            path.write_text(code)
            run = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration',
                str(path), '-o', str(binary)], capture_output=True, text=True)
            assert not run.returncode, run.stdout + run.stderr
            for scenario in scenarios:
                run = subprocess.run([str(binary), scenario], capture_output=True, text=True, timeout=15)
                if args.negative:
                    assert run.returncode, fixture
                    results[fixture] = {'rejected': True, 'returncode': run.returncode,
                        'diagnostic': next((line for line in run.stderr.splitlines()
                        if 'Assertion' in line or 'ERROR: AddressSanitizer' in line
                        or 'runtime error:' in line), run.stderr[:200])}
                else:
                    assert not run.returncode, scenario + ': ' + run.stdout + run.stderr
                    results.update(json.loads(run.stdout))
    report = {
        'tests': results,
        'source_sha256': {PREFIX + path: hashlib.sha256((root / PREFIX / path).read_bytes()).hexdigest()
            for path in ['soc/soundwire.c', 'soc/regmap-swr.c', 'include/soc/soundwire.h']},
        'provider_header_sha256': {PREFIX + 'include/soc/audio_mod_devicetable.h': hashlib.sha256(provider.read_bytes()).hexdigest()},
        'harness_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (HERE / 'tests').glob('swr_*.c')},
        'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
        'limits': 'Full provider/regmap sources and actual public header with pthread locks, callback drain, simulated device registration/refcounts, allocation failures and controller transfers. Native controller allocation lifetime and group state changes still need adaptation. Native asynchronous transaction IDs require exactly one response before reuse; the wire callback has no generation field. Does not exercise Linux driver core, firmware or physical SoundWire bus.',
    }
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
