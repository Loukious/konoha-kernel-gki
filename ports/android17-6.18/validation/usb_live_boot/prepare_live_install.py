#!/usr/bin/env python3
"""Prepare a verified experimental update from the USB-accessible 6.18 build."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

WORK = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
BACKUP = Path(Path('/tmp/onyx-current-backup.txt').read_text().strip())
PREVIOUS = Path(Path('/tmp/onyx-capture-attempt.txt').read_text().strip())
ADB = ['/mnt/d/platform-tools/adb.exe', '-s', '5493f3c6']


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    images = Path(sys.argv[1]).resolve()
    report = images/'report.json'
    candidate = json.loads(report.read_text())
    old_plan = json.loads((PREVIOUS/'capture-plan.json').read_text())
    old_report = Path(old_plan['images'])/'report.json'
    old = json.loads(old_report.read_text())
    assert candidate['status'] == 'PASS'
    assert candidate['loader_manifest_sha256'] == old['loader_manifest_sha256']
    assert not candidate['diagnostic_module_blacklist']
    originals = json.loads((BACKUP/'backup-manifest.json').read_text())
    for name, item in originals['images'].items():
        for path in [Path(item['path']), Path('/mnt/d/platform-tools/onyx-618-20261101/backup')/(name+'_a.img')]:
            assert path.stat().st_size == item['bytes'] and sha(path) == item['sha256']
    state = subprocess.check_output(ADB+['shell', 'uname -r; getprop sys.boot_completed; getprop ro.boot.slot_suffix'], text=True).replace('\r', '').splitlines()
    assert state[0].startswith('6.18.') and state[2] == '_a', state
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    attempt = BACKUP/'logging-investigation'/('live-boot-'+stamp)
    attempt.mkdir(mode=0o700)
    stage = Path('/mnt/d/platform-tools')/('onyx-618-'+attempt.name)
    (stage/'images').mkdir(parents=True)
    for name, item in candidate['images'].items():
        source = Path(item['path'])
        assert sha(source) == item['sha256']
        dest = stage/'images'/(name+'.img')
        shutil.copyfile(source, dest)
        assert dest.stat().st_size == item['size'] and sha(dest) == item['sha256']
    source = (PREVIOUS/'installation.py').read_text()
    source = source.replace(str(PREVIOUS), str(attempt)).replace(old_plan['windows_stage'], str(stage))
    source = source.replace(str(old_report.relative_to(WORK)), str(report.relative_to(WORK)))
    source = source.replace(old_plan['image_report_sha256'], sha(report))
    start = source.index('state = run(adb,')
    end = source.index("run(adb, ['reboot', 'fastboot'])", start)
    source = source[:start] + f'''previous_report = Path({str(old_report)!r})
previous = json.loads(previous_report.read_text())
assert manifest['loader_manifest_sha256'] == previous['loader_manifest_sha256']
receipt['previous_image_report'] = str(previous_report)
receipt['metadata_verification'] = 'unchanged loaders from previously verified installation; no metadata writes'
state = run(adb, ['shell', 'uname -r; getprop ro.boot.slot_suffix']).replace('\\r', '').splitlines()
assert state[0].startswith('6.18.') and state[1] == '_a', state
receipt['preflash_state'] = state
''' + source[end:]
    start = source.index("receipt['status'] = 'preflight passed; flashing'")
    source = source[:start] + '''before = stage / 'vendor-boot-before.img'
win_before = subprocess.check_output(['wslpath', '-w', str(before)], text=True).strip()
run(fastboot, ['fetch', 'vendor_boot_a', win_before])
assert sha(before) == previous['images']['vendor_boot']['sha256']
receipt['previous_vendor_boot_readback_sha256'] = sha(before)
''' + source[start:]
    source = source.replace("for name in ['system_dlkm', 'vendor_dlkm', 'dtbo', 'vendor_boot', 'boot']:\n", "for name in ['system_dlkm', 'vendor_dlkm', 'dtbo', 'vendor_boot', 'boot']:\n    if manifest['images'][name]['sha256'] == previous['images'][name]['sha256']:\n        continue\n")
    source = source.replace("five images flashed; vendor_boot readback verified; reboot pending", "changed 6.18 images flashed; vendor_boot readback verified; reboot pending")
    compile(source, 'installation.py', 'exec')
    (attempt/'installation.py').write_text(source)
    (attempt/'installation.py').chmod(0o600)
    (attempt/'rollback.py').write_text((PREVIOUS/'rollback.py').read_text().replace(str(PREVIOUS), str(attempt)))
    (attempt/'rollback.py').chmod(0o600)
    plan = {'status':'READY: update directly from live 6.18', 'attempt':str(attempt),
            'images':str(images), 'windows_stage':str(stage), 'image_report_sha256':sha(report),
            'previous_attempt':str(PREVIOUS), 'preflash_state':state,
            'changed_images':[name for name in candidate['images'] if candidate['images'][name]['sha256'] != old['images'][name]['sha256']],
            'user_instruction':'Keep USB-accessible 6.18 installed between tests; restore only when necessary.'}
    (attempt/'capture-plan.json').write_text(json.dumps(plan, indent=2)+'\n')
    (attempt/'capture-plan.json').chmod(0o600)
    Path('/tmp/onyx-capture-attempt.txt').write_text(str(attempt)+'\n')
    print(json.dumps(plan, indent=2))


if __name__ == '__main__':
    main()
