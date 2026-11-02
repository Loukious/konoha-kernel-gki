#!/usr/bin/env python3
"""Repack only vendor_boot for a checked, device-specific serial-loader test."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('previous', type=Path)
p.add_argument('loaders', type=Path)
p.add_argument('rom_root', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
previous = json.loads((a.previous / 'report.json').read_text())
loaders = json.loads((a.loaders / 'manifest.json').read_text())
assert previous['status'] == 'PASS' and previous['diagnostic_boot_checkpoints']
assert not previous['diagnostic_module_blacklist']
for name, record in previous['images'].items():
    assert sha(Path(record['path'])) == record['sha256'], name
for name, record in loaders['files'].items():
    assert sha(a.loaders / name) == record['sha256'], name
assert not a.output.exists()
a.output.mkdir(parents=True)
images = a.output / 'images'; images.mkdir()
tools = a.rom_root / 'out/host/linux-x86/bin'
commands = []
def run(command, label):
    commands.append(list(map(str, command)))
    result = subprocess.run(commands[-1], capture_output=True)
    (a.output / (label + '.log')).write_bytes(result.stdout + result.stderr)
    assert result.returncode == 0, label
    return result.stdout
args = run([tools / 'unpack_bootimg', '--boot_img', a.previous / 'images/vendor_boot.img',
            '--out', a.output / 'input', '--format', 'mkbootimg', '-0'], 'unpack-input').decode().split('\0')
if args[-1] == '':
    args.pop()
input_dir = a.output / 'input'
config = (input_dir / 'bootconfig').read_text()
needle = 'androidboot.load_modules_parallel=true'
assert config.splitlines().count(needle) == 1
assert not any(line.startswith(('androidboot.init_rc=', 'androidboot.konoha_serial_modules=')) for line in config.splitlines())
config = config.replace(needle, 'androidboot.load_modules_parallel=false')
config = config.rstrip('\n') + '\nandroidboot.init_rc=' + loaders['device_directory'] + '/init.rc\nandroidboot.konoha_serial_modules=1\n'
bootconfig = a.output / 'bootconfig'
bootconfig.write_text(config)
args[args.index('--vendor_bootconfig') + 1] = str(bootconfig)
run([tools / 'mkbootimg', *args, '--vendor_boot', images / 'vendor_boot.img'], 'mkbootimg')
layout = previous['layouts']['vendor_boot']
footer = [tools / 'avbtool', 'add_hash_footer', '--image', images / 'vendor_boot.img',
          '--partition_name', 'vendor_boot', '--partition_size', str(layout['size']),
          '--hash_algorithm', 'sha256', '--salt', layout['salt'],
          '--algorithm', layout['algorithm'], '--rollback_index', str(layout['rollback_index'])]
if layout['algorithm'] != 'NONE':
    footer += ['--key', a.rom_root / 'external/avb/test/data/testkey_rsa2048.pem']
for key, value in layout['props']:
    footer += ['--prop', key + ':' + value]
run(footer, 'avb-footer')
run([tools / 'avbtool', 'verify_image', '--image', images / 'vendor_boot.img'], 'avb-verify')
run([tools / 'unpack_bootimg', '--boot_img', images / 'vendor_boot.img',
     '--out', a.output / 'checked', '--format', 'mkbootimg', '-0'], 'unpack-check')
checked = a.output / 'checked'
assert (checked / 'bootconfig').read_bytes() == bootconfig.read_bytes()
for path in input_dir.rglob('*'):
    if path.is_file() and path.name != 'bootconfig':
        relative = path.relative_to(input_dir)
        assert sha(path) == sha(checked / relative), str(relative)
for name in previous['images']:
    if name != 'vendor_boot':
        shutil.copyfile(a.previous / 'images' / (name + '.img'), images / (name + '.img'))
        assert sha(images / (name + '.img')) == previous['images'][name]['sha256']
report = dict(previous)
report.update(purpose='Device-specific serial module diagnostic; requires pre-staged metadata, not a standalone ZIP',
              diagnostic_serial_module_loading=True, device_metadata_directory=loaders['device_directory'],
              loader_manifest=str(a.loaders / 'manifest.json'), loader_manifest_sha256=sha(a.loaders / 'manifest.json'),
              previous_report=str(a.previous / 'report.json'), previous_report_sha256=sha(a.previous / 'report.json'),
              bootconfig=config, images={path.stem: {'path': str(path), 'sha256': sha(path), 'size': path.stat().st_size}
                                        for path in images.iterdir()}, commands=commands)
(a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: only vendor_boot changed; DTB, ramdisk and four other images are identical')
