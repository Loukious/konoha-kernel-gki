#!/usr/bin/env python3
"""Stage checked images and fresh installer/restore receipts without rebooting."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

C = Path(__file__).resolve().parents[2]
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
old = Path(Path('/tmp/onyx-eva-attempt.txt').read_text().strip())
backup = Path(Path('/tmp/onyx-current-backup.txt').read_text().strip())
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
old_plan = json.loads((old / 'READY.json').read_text())
package = json.loads((C / 'validation/ipa_wan_tx_napi/package-validation.json').read_text())
assert package['status'].startswith('PASS')
tag = 'ipa-tx-napi-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
p = backup / 'logging-investigation' / tag
p.mkdir(mode=0o700)
stage = Path('/mnt/d/platform-tools') / ('onyx-618-' + tag)
(stage / 'images').mkdir(parents=True)
report_path = Path(package['images_report'])
assert sha(report_path) == package['images_report_sha256']
report = json.loads(report_path.read_text())
for name, item in report['images'].items():
    source = Path(item['path'])
    assert source.stat().st_size == item['size'] and sha(source) == item['sha256']
    target = stage / 'images' / (name + '.img')
    shutil.copyfile(source, target)
    assert sha(target) == item['sha256']
recovery = W / 'packaging/recovery-init-fix-20261102/recovery.img'
shutil.copyfile(recovery, stage / 'recovery.img')
assert sha(recovery) == sha(stage / 'recovery.img')
r = subprocess.run(['/mnt/d/platform-tools/adb.exe', '-s', '5493f3c6', 'shell',
    'uname -r; getprop sys.boot_completed; getprop ro.boot.slot_suffix; cat /proc/uptime'],
    capture_output=True, text=True, timeout=10)
state = r.stdout.replace('\r', '').splitlines()
assert r.returncode == 0 and state[:3] == ['6.18.33-android17-5-maybe-dirty-4k', '1', '_a']
(p / 'preflash-state.json').write_text(json.dumps({'state': state}, indent=2) + '\n')
old_report = Path(old_plan['images_report'])
source = (old / 'install-fastbootd.py').read_text()
for before, after in [(str(old), str(p)), (old_plan['stage'], str(stage)),
                      (str(old_report), str(report_path)), (sha(old_report), sha(report_path))]:
    assert before in source
    source = source.replace(before, after)
source = source.replace('shared WAN NAPI lifecycle and three-property SRAM address layout repaired;',
    'shared WAN NAPI, SRAM layout, optional WiFi settings, SoundWire locking and EVA debugfs identities repaired;')
previous_report = Path(old_plan['previous_report'])
previous_manifest = json.loads(previous_report.read_text())
current_manifest = json.loads(old_report.read_text())
expected_before = previous_manifest['images']['vendor_boot']['sha256']
expected_current = current_manifest['images']['vendor_boot']['sha256']
assert expected_before in source
source = source.replace(expected_before, expected_current)
source = source.replace('EVA debugfs identities repaired;', 'EVA debugfs identities and WAN TX NAPI cleanup repaired;')
(p / 'install-fastbootd.py').write_text(source)
rescue = (old / 'rescue-android.py').read_text()
for before, after in [(str(old), str(p)), (old_plan['restore_stage'], old_plan['stage']),
                      (str(previous_report), str(old_report)), (sha(previous_report), sha(old_report))]:
    assert before in rescue
    rescue = rescue.replace(before, after)
(p / 'rescue-android.py').write_text(rescue)
for name in ['live-618', 'returned-618']:
    (p / name).mkdir(mode=0o700)
    shutil.copyfile(old / name / 'capture.py', p / name / 'capture.py')
ready = dict(old_plan)
ready.update(status='Staged; fresh installer/restore checks pending; no flash',
    previous_working_artifact=old_plan['artifact'], previous_report=str(old_report),
    previous_attempt=str(old), restore_stage=old_plan['stage'],
    artifact=package['artifact'], images_report=str(report_path),
    images_report_sha256=sha(report_path), stage=str(stage),
    changes=package['changes_from_installed'], supersedes_uninstalled_attempt=str(old),
    goal='All kernel errors/warnings remain in scope; CMD Ignored and off charging unresolved',
    current_phone='Installed EVA042308 6.18 remains the rollback target; prior returned capture active',
    installed_capture_pid=None)
(p / 'READY.json').write_text(json.dumps(ready, indent=2) + '\n')
Path('/tmp/onyx-ipa-tx-attempt.txt').write_text(str(p) + '\n')
for name in ['install-fastbootd.py', 'rescue-android.py']:
    r = subprocess.run(['python3', str(p / name), '--verify-only'], capture_output=True, text=True)
    (p / (name + '.preflight.log')).write_text(r.stdout + r.stderr)
    assert r.returncode == 0, (name, r.stdout, r.stderr)
    print(name, r.stdout.strip(), flush=True)
test = (C / 'validation/ipa_wan_napi_lifetime/verify_installer.py').read_text()
test = test.replace("attempt = Path(Path('/tmp/onyx-ipa-napi-attempt.txt').read_text().strip())",
    "attempt = Path(Path('/tmp/onyx-ipa-tx-attempt.txt').read_text().strip())")
test = test.replace('H = Path(__file__).resolve().parent', 'H = Path(' + repr(str(p)) + ')')
(p / 'verify_installer.py').write_text(test)
r = subprocess.run(['python3', str(p / 'verify_installer.py')], capture_output=True, text=True)
(p / 'installer-mock.log').write_text(r.stdout + r.stderr)
assert r.returncode == 0, (r.stdout, r.stderr)
ready['status'] = 'READY: source/build/images/Windows stage/previous6.18 restore and10installer fixtures PASS; no flash'
ready['installer_tests'] = str(p / 'installer-tests.json')
(p / 'READY.json').write_text(json.dumps(ready, indent=2) + '\n')
print(p, flush=True)
