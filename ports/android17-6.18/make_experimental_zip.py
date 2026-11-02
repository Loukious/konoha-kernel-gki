#!/usr/bin/env python3
"""Stage/finalize the image-audited Onyx recovery boot-test ZIP."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import stat
import zipfile

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('image_build', type=Path)
p.add_argument('bundle', type=Path)
p.add_argument('--stage', type=Path, required=True)
p.add_argument('--output-zip', type=Path)
a = p.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_text())
r = read(a.image_build / 'report.json')
if r.get('diagnostic_serial_module_loading'):
    raise ValueError('Serial-loader images require pre-staged device metadata; the generic ZIP installer cannot verify it')
b = read(a.bundle / 'report.json')
assert r['status'] == b['status'] == 'PASS'
assert Path(r['bundle']).resolve() == a.bundle.resolve()
assert r['bundle_report_sha256'] == sha(a.bundle / 'report.json')
names = ['system_dlkm', 'vendor_dlkm', 'dtbo', 'vendor_boot', 'boot']
assert set(r['images']) == set(names)
for name, item in r['images'].items():
    path = Path(item['path'])
    assert path.stat().st_size == item['size'] and sha(path) == item['sha256'], name
template = HERE.parent.parent / 'anykernel'
core = (template / 'tools/ak3-core.sh').read_text()
assert core.count('for file in $1 $1$slot; do') == 1
assert core.count('touch ${1}_flashed;') == 1
core = core.replace('for file in $1 $1$slot; do', 'for file in $1$slot $1; do')
core = core.replace('touch ${1}_flashed;', 'ak3_last_flashed_block=$imgblock;\n    touch ${1}_flashed;')
if not a.stage.exists():
    a.stage.mkdir(parents=True)
    shutil.copytree(template / 'tools', a.stage / 'tools')
    shutil.copytree(template / 'META-INF', a.stage / 'META-INF')
    (a.stage / 'tools/ak3-core.sh').write_text(core)
    shutil.copyfile(HERE / 'installer/anykernel.sh', a.stage / 'anykernel.sh')
    for name, item in r['images'].items():
        shutil.copyfile(item['path'], a.stage / (name + '.img'))
    (a.stage / 'payloads.tsv').write_text(''.join(f"{name}\t{r['images'][name]['size']}\t{r['images'][name]['sha256']}\n" for name in names))
    tools = sorted((a.stage / 'tools').iterdir())
    checks = tools + [a.stage / 'anykernel.sh', a.stage / 'META-INF/com/google/android/update-binary', a.stage / 'payloads.tsv']
    assert all(p.is_file() for p in checks)
    (a.stage / 'tools.sha256').write_text(''.join(sha(path) + '  ' + str(path.relative_to(a.stage)) + '\n' for path in checks))
    manifest = {'purpose': 'Onyx Android 17 / Linux 6.18 experimental boot test',
                'device': 'onyx', 'kernel_artifact': b['artifact'], 'release': b['release'],
                'module_bundle': str(a.bundle), 'module_bundle_report_sha256': sha(a.bundle / 'report.json'),
                'packaged_unique_modules': b['modules'],
                'module_versioned_imports_verified': b['versioned_imports_checked'],
                'images': r['images'], 'layouts': r['layouts'],
                'module_load_lists': b['stages'],
                'baseline_optional_soft_dependencies': b['baseline_optional_soft_dependency_evidence'],
                'image_audit_report_sha256': sha(a.image_build / 'report.json'),
                'boot_cmdline': r.get('boot_cmdline', 'android_arch_task_struct_size=512'),
                'diagnostic_boot': r.get('diagnostic_boot', False),
                'diagnostic_module_blacklist': r.get('diagnostic_module_blacklist', []),
                'diagnostic_boot_checkpoints': r.get('diagnostic_boot_checkpoints', False),
                'diagnostic_funnel_checkpoints': r.get('diagnostic_funnel_checkpoints', False),
                'diagnostic_gpi_checkpoints': r.get('diagnostic_gpi_checkpoints', False),
                'installer_sha256': sha(a.stage / 'anykernel.sh'),
                'core_sha256': sha(a.stage / 'tools/ak3-core.sh'),
                'requirements': ['Onyx with unlocked bootloader', 'Recovery with active-slot DLKM mappings',
                                 'AVB verification and verity already disabled (vbmeta flags 3), matching the baseline ROM',
                                 'Compatible local Android 17 ROM layout; target partition sizes are recorded in layouts'],
                'limits': ['No phone boot or hardware validation has been performed.',
                           'Unavailable optional module entries are listed explicitly; full feature parity is unfinished.',
                           'KernelSU/NetHunter completion is deferred from this first boot test.',
                           'init_boot, recovery and vbmeta are not flashed.',
                           'If installation aborts after writing begins, restore matching ROM images before rebooting.']}
    (a.stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    shutil.copyfile(a.image_build / 'report.json', a.stage / 'image-audit.json')
    (a.stage / 'README.txt').write_text('''Konoha Onyx 6.18 experimental boot-test ZIP

For Onyx / the local Android 17 ROM baseline. Flash from recovery only.
Requires an unlocked bootloader, mapped active-slot DLKM partitions, and AVB /
verity already disabled (vbmeta flags 3), as in the supplied ROM baseline.
The installer checks all five payloads before writing, uses the active A/B slot,
and reads back every image. Target partition sizes are recorded in manifest.json.

This is an experimental boot test. Hardware behavior is unvalidated and optional
module gaps remain. KernelSU/NetHunter completion is deferred. See manifest.json
for exact inputs and omitted module entries. init_boot, recovery and vbmeta are
not flashed. Preserve a working recovery and the matching ROM images for rollback.
If installation aborts after writes begin, restore the matching ROM images before
rebooting. No phone was flashed during package creation.
''')
    for path in a.stage.rglob('*'):
        if path.is_file():
            path.chmod(0o755 if 'tools' in path.relative_to(a.stage).parts or path.name in ['anykernel.sh', 'update-binary'] else 0o644)
assert (a.stage / 'tools/ak3-core.sh').read_text() == core
assert sha(a.stage / 'anykernel.sh') == sha(HERE / 'installer/anykernel.sh')
for name, item in r['images'].items():
    assert sha(a.stage / (name + '.img')) == item['sha256'], name
if a.output_zip:
    assert not a.output_zip.exists(), 'Use a new ZIP output path'
    tests = read(a.stage / 'installer-validation.json')
    assert tests['status'] == 'PASS'
    assert tests['installer_sha256'] == sha(a.stage / 'anykernel.sh')
    assert tests['core_sha256'] == sha(a.stage / 'tools/ak3-core.sh')
    files = sorted(p for p in a.stage.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
    (a.stage / 'SHA256SUMS').write_text(''.join(sha(p) + '  ' + str(p.relative_to(a.stage)) + '\n' for p in files))
    files.append(a.stage / 'SHA256SUMS')
    a.output_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(a.output_zip, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in files:
            info = zipfile.ZipInfo(str(path.relative_to(a.stage)), (2026, 11, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | stat.S_IMODE(path.stat().st_mode)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    with zipfile.ZipFile(a.output_zip) as z:
        assert z.testzip() is None
        for path in files:
            assert hashlib.sha256(z.read(str(path.relative_to(a.stage)))).hexdigest() == sha(path)
    digest = sha(a.output_zip)
    a.output_zip.with_suffix(a.output_zip.suffix + '.sha256').write_text(digest + '  ' + a.output_zip.name + '\n')
    print('PASS ZIP:', a.output_zip, digest)
else:
    print('Staged five matched images and recovery installer:', a.stage)
