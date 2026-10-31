#!/usr/bin/env python3
"""Execute the production DMA-release extraction and oversize guard."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('workspace', nargs='?', type=Path, default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031'))
ap.add_argument('--source-dir', default='onyx')
ap.add_argument('--output', type=Path, default=H / 'dbr-validation.json')
args = ap.parse_args()
S = args.workspace / args.source_dir
rel = 'qca-wifi-host-cmn/target_if/direct_buf_rx/src/target_if_direct_buf_rx_main.c'
text = (S / rel).read_text()
def block(start):
    brace = text.index('{', start)
    pos = brace + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]
get_data = block(text.index('static QDF_STATUS target_if_get_dbr_data('))
guard = block(text.index('if (dbr_data.dbr_len > dbr_data.buffer_capacity)'))
hrel = 'qca-wifi-host-cmn/target_if/direct_buf_rx/inc/target_if_direct_buf_rx_api.h'
header = (S / hrel).read_text()
assert 'size_t buffer_capacity;' in header
tests = (H / 'tests/wlan_main_dbr.c').read_text()
mutations = {
    'oversize_delivered': (guard, guard.replace('dbr_data.dbr_len > dbr_data.buffer_capacity', 'false')),
    'firmware_length_as_capacity': (get_data, get_data.replace('dbr_ring_cap->min_buf_size;', 'dbr_rsp->dbr_entries[idx].len;', 1)),
    'oversize_not_reposted': (guard, guard.replace('target_if_dbr_replenish_ring(pdev, mod_param,\n\t\t\t\t\t\t\t      dbr_data.vaddr,\n\t\t\t\t\t\t\t      cookie)', 'QDF_STATUS_SUCCESS')),
}
assert mutations['oversize_not_reposted'][0] != mutations['oversize_not_reposted'][1]
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {x: hashlib.sha256((S / x).read_bytes()).hexdigest() for x in [rel, hrel]}, 'harness_sha256': {'wlan_main_dbr.c': hashlib.sha256(tests.encode()).hexdigest()}, 'limits': 'Production get-data function and exact oversize-guard block with mocked firmware/DMA/ring services. Verifies allocation-bound handling and ordering; does not establish hardware DMA safety, firmware correctness, RF samples or the complete WMI event parser.'}
with tempfile.TemporaryDirectory(prefix='wlan-dbr-test-') as directory:
    directory = Path(directory)
    for name in ['positive', *mutations]:
        fn, check = get_data, guard
        if name in mutations:
            old, new = mutations[name]
            if old == get_data:
                fn = new
            else:
                check = new
        code = tests.replace('/* GET DATA */', fn).replace('/* OVERSIZE GUARD */', check)
        path = directory / (name + '.c')
        binary = directory / name
        path.write_text(code)
        build = subprocess.run(['cc', '-std=gnu11', '-g', '-O1', '-no-pie', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-Werror', str(path), '-o', str(binary)], capture_output=True, text=True)
        assert build.returncode == 0, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        if name == 'positive':
            assert run.returncode == 0, run.stderr
            report['tests']['dbr_release_bounds'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode != 0, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
args.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
