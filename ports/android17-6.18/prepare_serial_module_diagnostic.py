#!/usr/bin/env python3
"""Prepare device-specific serial loaders; never edits native ROM files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('rom_root', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--device-directory', required=True)
a = p.parse_args()
assert re.fullmatch(r'/metadata/ota/konoha-serial-[A-Za-z0-9_-]+', a.device_directory)
assert not a.output.exists()
a.output.mkdir(parents=True)
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
sources = {}
for name in ['vendor_modprobe.sh', 'system_dlkm_modprobe.sh']:
    source = a.rom_root / 'vendor/xiaomi/onyx/proprietary/vendor/bin' / name
    text = source.read_text()
    sources[name] = {'path': str(source), 'sha256': sha(source)}
    pattern = r'\( (\$\{MODPROBE\}[^\n]+) \) &'
    text, count = re.subn(pattern, r'\1', text)
    assert count == 1, name
    # Keep stdout suppression, all flags, fallback branches and return values.
    # stderr is captured by the original stdio_to_kmsg service option.
    text = text.replace('${MODPROBE}', 'konoha_modprobe')
    function = '''
konoha_modprobe() {
    echo "Konoha serial: begin %s $*" >&2
    ${MODPROBE} "$@"
    konoha_status=$?
    echo "Konoha serial: end %s rc=${konoha_status} $*" >&2
    return "${konoha_status}"
}
''' % (name, name)
    marker = 'MODPROBE="/vendor/bin/modprobe"\n'
    assert text.count(marker) == 1
    text = text.replace(marker, marker + function)
    assert not re.search(r'\)\s*&', text)
    destination = a.output / name
    destination.write_text(text)
    destination.chmod(0o755)
    subprocess.run(['bash', '-n', str(destination)], check=True)

mountpoint = '/dev/konoha-serial-modules'
init = a.output / 'init.rc'
init.write_text('''# Selected only by this diagnostic vendor_boot bootconfig.
on early-init
    mkdir %s 0755 root root
    mount none %s %s bind
    # Clear nosuid on the cloned mount, without changing /metadata flags.
    mount none %s %s remount bind ro
    write /dev/kmsg "Konoha serial: early-init mount prepared"

''' % (mountpoint, a.device_directory, mountpoint, mountpoint, mountpoint) +
    # Init parses imports after this file's actions. Define the two services
    # first; the native definitions later become ignored duplicates. Android
    # rejects replacing a vendor service from a platform-context config.
    ''.join('import ' + path + '\n' for path in [
        a.device_directory + '/services.rc',
        '/system/etc/init/hw/init.rc', '/system/etc/init',
        '/system_ext/etc/init', '/vendor/etc/init', '/odm/etc/init',
        '/product/etc/init']))
services = a.output / 'services.rc'
original = (a.rom_root / 'device/xiaomi/onyx/rootdir/etc/init.qti.kernel.rc').read_text()
definitions = []
for name, script in [('gki.modprobe', 'system_dlkm_modprobe.sh'),
                     ('vendor.modprobe', 'vendor_modprobe.sh')]:
    match = re.search(r'^service ' + re.escape(name) + r' [^\n]+\n(?:[ \t]+[^\n]*\n|\n)*', original, re.M)
    assert match
    block = match.group(0).rstrip() + '\n'
    assert block.splitlines()[0] == 'service ' + name + ' /vendor/bin/' + script
    block = block.replace('/vendor/bin/' + script, mountpoint + '/' + script, 1)
    definitions.append(block)
services.write_text('# Imported before standard boot configs; their two native definitions are ignored duplicates.\n' + '\n'.join(definitions))
for path in [init, services]:
    path.chmod(0o644)
    subprocess.run([str(a.rom_root / 'out/host/linux-x86/bin/host_init_verifier'), str(path)], check=True)
manifest = {
    'purpose': 'Device-specific serial module diagnostic; not a standalone ZIP',
    'device_directory': a.device_directory, 'mountpoint': mountpoint,
    'sources': sources,
    'files': {path.name: {'sha256': sha(path), 'mode': path.stat().st_mode & 0o777,
                        'label': 'u:object_r:' + ('vendor_modinstall-sh_exec' if path.suffix == '.sh' else 'system_file') + ':s0'}
              for path in [init, services, a.output / 'vendor_modprobe.sh', a.output / 'system_dlkm_modprobe.sh']},
    'directory_label': 'u:object_r:vendor_file:s0',
    'invariants': 'Same lists, flags, blocklists, first-module fallback, readiness property and return statuses; only concurrency and stderr checkpoints change.'}
(a.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('PASS: serial scripts and init overrides prepared', a.output)
