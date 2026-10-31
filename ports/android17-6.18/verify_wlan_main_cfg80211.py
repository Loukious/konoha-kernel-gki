#!/usr/bin/env python3
"""Test the actual 6.18 request adapters, not a second implementation."""
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
ap.add_argument('--output', type=Path, default=H / 'cfg80211-validation.json')
args = ap.parse_args()
P = args.workspace / args.source_dir / 'qcacld-3.0/core/hdd/src/wlan_hdd_cfg80211.c'
text = P.read_text()
begin = text.index('/* The selected driver owns netdev-backed interfaces. */')
end = text.index('static struct cfg80211_ops wlan_hdd_cfg80211_ops = {', begin)
adapters = text[begin:end]
tests = (H / 'tests/wlan_main_cfg80211.c').read_text()
controls = {
    'lost_key_link_id': ('wiphy, dev, link_id, key_index', 'wiphy, dev, -1, key_index'),
    'wrong_wiphy_accepted': ('wdev->wiphy != wiphy || ', ''),
    'foreign_netdev_accepted': ('wdev->netdev->ieee80211_ptr != wdev', 'false'),
    'radio_request_misrouted': ('if (radio_idx != -1)', 'if (false)'),
    'receive_filter_ignored': ('if (rx_addr)', 'if (false)'),
}
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {str(P): hashlib.sha256(P.read_bytes()).hexdigest()}, 'harness_sha256': {'wlan_main_cfg80211.c': hashlib.sha256(tests.encode()).hexdigest()}, 'limits': 'Production request adapters with mocked native callbacks. Tests validate routing/rejection and argument preservation; they do not establish actual key installation, monitor operation, regulatory behavior or radio functionality.'}
with tempfile.TemporaryDirectory(prefix='wlan-cfg-test-') as directory:
    directory = Path(directory)
    for name in ['positive', *controls]:
        source = adapters
        if name in controls:
            old, new = controls[name]
            assert old in source
            source = source.replace(old, new, 1)
        path = directory / (name + '.c')
        binary = directory / name
        path.write_text(tests.replace('/* PRODUCTION ADAPTERS */', source))
        build = subprocess.run(['cc', '-std=gnu11', '-g', '-O1', '-no-pie', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-Werror', '-Wno-unused-function', str(path), '-o', str(binary)], capture_output=True, text=True)
        assert build.returncode == 0, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        if name == 'positive':
            assert run.returncode == 0, run.stderr
            report['tests']['request_routing'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode != 0, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
args.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
