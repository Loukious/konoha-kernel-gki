#!/usr/bin/env python3
"""Reuse verified image bytes when an audited candidate changes only its DTB."""
import argparse
import json
from pathlib import Path
import shutil

from build_devicetrees import digest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('previous', type=Path)
parser.add_argument('candidate', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
read = lambda p: json.loads(p.read_text())
old = read(args.previous / 'report.json')
new = read(args.candidate / 'report.json')
assert old['status'] == new['status'] == 'PASS'
assert old.get('diagnostic_serial_module_loading') == new.get('diagnostic_serial_module_loading')
assert old['layouts'] == new['layouts'] and old['boot_cmdline'] == new['boot_cmdline']
assert old.get('bootconfig') == new.get('bootconfig')
assert old['loader_manifest_sha256'] == new['loader_manifest_sha256']
old_bundle, new_bundle = [read(Path(r['bundle'])/'report.json') for r in [old, new]]
assert old_bundle['sha256'] == new_bundle['sha256']
assert old_bundle['stages'] == new_bundle['stages']
for record, bundle in [(old, old_bundle), (new, new_bundle)]:
    assert record['bundle_report_sha256'] == digest(Path(record['bundle'])/'report.json')
    for name, expected in bundle['sha256'].items():
        assert digest(Path(record['bundle'])/name) == expected, name
    for name, item in record['images'].items():
        path = Path(item['path'])
        assert path.stat().st_size == item['size'] and digest(path) == item['sha256'], name
assert old['images']['boot']['sha256'] == new['images']['boot']['sha256']
assert old['images']['dtbo']['sha256'] == new['images']['dtbo']['sha256']
for category in ['gki', 'platform']:
    old_dir, new_dir = [Path(r['artifact'])/category for r in [old,new]]
    assert {p.name for p in old_dir.iterdir() if p.is_file()} == {p.name for p in new_dir.iterdir() if p.is_file()}
    for path in old_dir.iterdir():
        if path.is_file(): assert digest(path) == digest(new_dir/path.name), path.name
old_checked, new_checked = [root/'checked' for root in [args.previous,args.candidate]]
if old.get('diagnostic_proxy_execution_disabled') and not (old_checked/'dtb').exists():
    # The proxy diagnostic repacked boot only; its vendor payload is inherited.
    vendor_report_path = Path(old['previous_report'])
    assert digest(vendor_report_path) == old['previous_report_sha256']
    vendor_report = read(vendor_report_path)
    assert vendor_report['status'] == 'PASS'
    assert vendor_report['images']['vendor_boot']['sha256'] == old['images']['vendor_boot']['sha256']
    old_checked = vendor_report_path.parent/'checked'
assert {p.name for p in old_checked.iterdir() if p.is_file()} == {p.name for p in new_checked.iterdir() if p.is_file()}
for path in old_checked.iterdir():
    if path.is_file() and path.name != 'dtb':
        assert digest(path) == digest(new_checked/path.name), path.name
assert not args.output.exists()
(args.output/'images').mkdir(parents=True)
result = dict(new)
result['images'] = {}
for name in new['images']:
    record = new if name == 'vendor_boot' else old
    src = Path(record['images'][name]['path'])
    target = args.output/'images'/src.name
    shutil.copyfile(src,target)
    assert digest(target) == record['images'][name]['sha256']
    result['images'][name] = {'path': str(target), 'sha256': digest(target), 'size': target.stat().st_size}
assert [name for name in old['images'] if old['images'][name]['sha256'] != result['images'][name]['sha256']] == ['vendor_boot']
result.update(purpose='Device-specific DT-only diagnostic; only vendor_boot bytes change',
              dt_only_image_reuse={
                  'previous_report': str(args.previous/'report.json'),
                  'previous_report_sha256': digest(args.previous/'report.json'),
                  'candidate_report': str(args.candidate/'report.json'),
                  'candidate_report_sha256': digest(args.candidate/'report.json'),
                  'checks': ['all kernel/module files identical', 'all bundle hashes and load lists identical',
                             'vendor_boot payload files other than DTB identical', 'only vendor_boot image bytes change']})
(args.output/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: five checked images; only vendor_boot bytes change')
