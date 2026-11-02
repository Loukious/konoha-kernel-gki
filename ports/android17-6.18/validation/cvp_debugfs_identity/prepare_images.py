#!/usr/bin/env python3
"""Package the verified IPA/SRAM/WLAN/SoundWire/EVA candidate; never flashes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifact', type=Path)
parser.add_argument('--tag', required=True)
args = parser.parse_args()
C = Path(__file__).resolve().parents[2]
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
ROM = Path('/home/loukious/Android/custom-rom')
A = args.artifact.resolve()
PREVIOUS = W / 'artifacts/20261102T032148820151Z'
INSTALLED = W / 'artifacts/20261102T020511350870Z'
P = W / 'packaging'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report = json.loads((A / 'report.json').read_text())
assert len(report['module_verification']) == 318
assert all(sha(A / rel) == digest for rel, digest in report['sha256'].items())
for subdir in ['wlan_data_stall_dispatch', 'soundwire_bulk_lock', 'cvp_debugfs_identity']:
    validation = json.loads((C / 'validation' / subdir / 'validation.json').read_text())
    assert validation['status'] == 'PASS'
    assert sha(Path(validation['production_source'])) == validation['source_sha256']
assert (A / 'inputs/patches/0172-allocate-unique-eva-instance-debugfs-ids.patch').read_bytes() == (C / 'patches/0172-allocate-unique-eva-instance-debugfs-ids.patch').read_bytes()
changes = {}
for category in ['gki', 'platform']:
    old = {p.name: sha(p) for p in (PREVIOUS / category).iterdir() if p.is_file()}
    new = {p.name: sha(p) for p in (A / category).iterdir() if p.is_file()}
    assert old.keys() == new.keys()
    changes[category] = sorted(k for k in old if old[k] != new[k])
assert changes == {'gki': [], 'platform': ['msm-eva.ko']}, changes
for name in ['tuna.dtb', 'tuna7.dtb']:
    assert sha(A / 'devicetrees/checked' / name) == sha(PREVIOUS / 'devicetrees/checked' / name)
for name in changes['platform']:
    for field in ['alias', 'depends', 'vermagic']:
        before = subprocess.check_output(['modinfo', '-F', field, str(PREVIOUS / 'platform' / name)])
        after = subprocess.check_output(['modinfo', '-F', field, str(A / 'platform' / name)])
        assert before == after, (name, field)
inventory_path = P / ('module-inventory-' + args.tag + '.json')
assert not inventory_path.exists()
inventory = json.loads((P / 'module-inventory-20261102-ipa-sram-wlan-swr-bulk.json').read_text())
inventory['previous_basis'] = inventory['basis']
inventory['basis'] = str(A)
inventory['continuation_evidence'] = {'changes_from_wlan_swr': changes, 'trees_identical': True}
inventory_path.write_text(json.dumps(inventory, indent=2) + '\n')
first = P / ('first-stage-' + args.tag)
bundle = P / ('experimental-bundle-' + args.tag)
images = P / ('experimental-images-' + args.tag)
serial = P / ('experimental-images-' + args.tag + '-serial')

def run(script, arguments, suffix):
    with (P / (args.tag + '-' + suffix + '.log')).open('wb') as log:
        subprocess.run([sys.executable, str(C / script), *map(str, arguments)],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print(script, 'PASS', flush=True)

run('assemble_first_stage.py', [A, ROM / 'out/target/product/onyx',
    '--inventory', inventory_path, '--output', first], 'first-stage')
run('assemble_module_bundle.py', [A, ROM / 'out/target/product/onyx',
    '--first-stage', first, '--output', bundle], 'bundle')
before = json.loads((P / 'experimental-bundle-20261102-ipa-sram-wlan-swr-bulk/report.json').read_text())
after = json.loads((bundle / 'report.json').read_text())
assert before['partition_modules'] == after['partition_modules']
changed_copies = [k for k, v in after['sha256'].items()
                 if k.endswith('.ko') and before['sha256'].get(k) != v]
assert {Path(k).name for k in changed_copies} == set(changes['platform'])
run('build_experimental_images.py', [A, bundle, P / 'baseline-20261101T011044Z', ROM,
    '--output', images,
    '--partition-sizes', W / 'device-backups/5493f3c6-20261101T160034Z/target-dlkm-sizes.json',
    '--compact-system-erofs', '--diagnostic-boot', '--boot-checkpoints',
    '--funnel-checkpoints', '--gpi-checkpoints'], 'images')
run('build_serial_module_diagnostic.py', [images,
    P / 'serial-module-loaders-20261101-v2', ROM, '--output', serial], 'serial-images')
before = json.loads((P / 'experimental-images-20261102-ipa-sram-wlan-swr-bulk-serial/report.json').read_text())
after = json.loads((serial / 'report.json').read_text())
assert before['boot_cmdline'] == after['boot_cmdline']
assert before['loader_manifest_sha256'] == after['loader_manifest_sha256']
for name in ['boot', 'dtbo']:
    assert before['images'][name]['sha256'] == after['images'][name]['sha256']
for item in after['images'].values():
    assert sha(Path(item['path'])) == item['sha256']
result = {
    'status': 'PASS: source tests, full build, module/DT invariants and image checks; hardware pending',
    'artifact': str(A), 'images_report': str(serial / 'report.json'),
    'images_report_sha256': sha(serial / 'report.json'),
    'changes_from_wlan_swr': changes, 'changed_module_copies': changed_copies,
    'installed_baseline': str(INSTALLED),
    'changes_from_installed': ['ipam.ko', 'qca_cld3_wcn7750.ko', 'swr_ctrl_dlkm.ko', 'msm-eva.ko', 'three SRAM DT properties'],
    'preserved': ['GKI', 'boot command line', 'serial loaders', 'recovery image', 'prior working audio/USB/charging/fingerprint/camera/haptics fixes'],
    'limitations': 'No candidate flash or hardware validation. CMD Ignored and off-charger boot mode remain unresolved.'
}
(C / 'validation/cvp_debugfs_identity/package-validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2), flush=True)
