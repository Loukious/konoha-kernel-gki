#!/usr/bin/env python3
"""Run the real installer/core with mocked device nodes and recovery tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('stage', type=Path)
a = p.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
source = (a.stage / 'anykernel.sh').read_text()
core = (a.stage / 'tools/ak3-core.sh').read_text()
subprocess.run(['bash', '-n', str(a.stage / 'anykernel.sh')], check=True)
subprocess.run(['bash', '-n', str(a.stage / 'tools/ak3-core.sh')], check=True)
names = ['system_dlkm', 'vendor_dlkm', 'dtbo', 'vendor_boot', 'boot']
stub = '''#!/usr/bin/env python3
import os,sys
from pathlib import Path
r=Path(os.environ['TEST_ROOT']);n=Path(sys.argv[0]).name;args=sys.argv[1:];case=os.environ['TEST_CASE'];slot=os.environ['TEST_SLOT']
if n=='getprop':
 values={'ro.boot.slot_suffix':slot,'ro.product.device':'other' if case=='wrong_device' else 'onyx','ro.boot.flash.locked':'1' if case=='locked' else '0','ro.boot.vbmeta.device_state':'locked' if case=='locked' else 'unlocked','ro.boot.verifiedbootstate':'green' if case=='locked' else 'orange'}
 print(values.get(args[0],''));sys.exit(0)
if n=='ps':
 print('zygote64' if case=='android_running' else 'recovery');sys.exit(0)
if n=='blockdev':
 path=Path(args[-1]);assert str(path).startswith(str(r/'dev/block'))
 if args[0]=='--getsize64':print(path.stat().st_size)
 elif args[0]=='--getro':print(0)
 sys.exit(0)
if n=='httools_static':
 if args[0]=='avb':print('vbmeta')
 elif args[0]=='disable-flags':print('disabled')
 elif args[0] not in ['mount','umount']:sys.exit(3)
 sys.exit(0)
if n=='lptools_static':
 with (r/'helper.log').open('a') as f:f.write(' '.join(args)+'\\n')
 cmd=args[0]
 if cmd=='remove':(r/'dev/block/mapper'/args[1]).unlink(missing_ok=True)
 elif cmd=='create':
  if case in ['resize_fallback','resize_failure']:sys.exit(1)
  path=r/'dev/block/mapper'/args[1];path.write_bytes(b'x'*int(args[2]))
 elif cmd=='resize':
  if case=='resize_failure':sys.exit(1)
  path=r/'dev/block/mapper'/args[1]
  with path.open('r+b') as f:f.truncate(int(args[2]))
 sys.exit(0)
sys.exit(0)
'''
results = []
cases = ['active_a', 'active_b', 'slot_preference', 'resize_create', 'resize_fallback',
         'wrong_device', 'locked', 'bad_slot', 'bad_avb', 'android_running',
         'missing_partition', 'physical_too_small', 'tampered_payload', 'missing_payload',
         'duplicate_payload', 'missing_tool', 'corrupt_tool', 'stale_mapping',
         'multiple_resizes', 'write_failure', 'readback_corruption', 'resize_failure',
         'regression_unslotted_preference', 'regression_missing_readback_target']
positive = {'active_a', 'active_b', 'slot_preference', 'resize_create', 'resize_fallback'}
late_failure = {'write_failure', 'readback_corruption', 'resize_failure', 'regression_missing_readback_target'}
with tempfile.TemporaryDirectory(prefix='onyx-installer-audit-') as temporary:
    for case in cases:
        root = Path(temporary) / case
        stage = root / 'zip'; stage.mkdir(parents=True)
        (stage / 'tools').mkdir()
        (root / 'bin').mkdir()
        mapper = root / 'dev/block/mapper'; mapper.mkdir(parents=True)
        byname = root / 'dev/block/by-name'; byname.mkdir(parents=True)
        slot = '_b' if case == 'active_b' else '_a'
        for suffix in ['_a', '_b']:
            for name in names:
                directory = mapper if name.endswith('dlkm') else byname
                size = 4096 if name.endswith('dlkm') else 8192
                if suffix == slot and name == 'system_dlkm' and case in ['resize_create','resize_fallback','resize_failure','multiple_resizes']:
                    size = 2048
                if suffix == slot and name == 'vendor_dlkm' and case == 'multiple_resizes':
                    size = 2048
                (directory / (name + suffix)).write_bytes((name + suffix).encode().ljust(size, b'x'))
            vbmeta = bytearray(512); vbmeta[:4] = b'AVB0'
            struct.pack_into('>I', vbmeta, 120, 0 if case == 'bad_avb' else 3)
            (byname / ('vbmeta' + suffix)).write_bytes(vbmeta)
        if case in ['slot_preference','regression_unslotted_preference']:
            for name in names:
                directory = mapper if name.endswith('dlkm') else byname
                (directory / name).write_bytes(b'UNSLOTTED'.ljust(4096, b'x'))
        inactive = {path: sha(path) for path in (root / 'dev').rglob('*') if path.is_file() and path.name.endswith('_a' if slot == '_b' else '_b')}
        unslotted = {path: sha(path) for path in (root / 'dev').rglob('*') if path.is_file() and path.name in names}
        runtime = source.replace('/dev/block', str(root / 'dev/block'))
        runtime = runtime.replace('is_block_device() { [ -b "$1" ]; }', 'is_block_device() { [ -f "$1" ]; }')
        runtime_core = core.replace('/dev/block', str(root / 'dev/block'))
        if case == 'regression_unslotted_preference':
            runtime_core = runtime_core.replace('for file in $1$slot $1; do', 'for file in $1 $1$slot; do')
        if case == 'regression_missing_readback_target':
            runtime_core = runtime_core.replace('ak3_last_flashed_block=$imgblock;', ':;')
        (stage / 'tools/ak3-core.sh').write_text(runtime_core)
        prefix = '''cat() {
 if [ "$#" -eq 2 ] && [ "$2" = /dev/zero ]; then
  printf '%s\\n' "$(basename "$1" .img)" >> "$TEST_ROOT/writes.log"
  if [ "$TEST_CASE" = write_failure ]; then return 1; fi
  if [ "$TEST_CASE" = readback_corruption ] && [ "$(basename "$1")" = vendor_boot.img ]; then
   printf 'bad-readback'
  else /bin/cat "$1"; fi
 else /bin/cat "$@"; fi
}
sync() { :; }
'''
        (stage / 'anykernel.sh').write_text(prefix + runtime)
        for name in ['getprop','ps','blockdev']:
            path = root / 'bin' / name; path.write_text(stub); path.chmod(0o755)
        for name in ['busybox','httools_static','lptools_static','snapshotupdater_static']:
            path = stage / 'tools' / name; path.write_text(stub); path.chmod(0o755)
        rows = []
        for name in names:
            path = stage / (name + '.img'); path.write_bytes((name + '-IMAGE').encode().ljust(4096, b'0'))
            rows.append(f'{name}\t4096\t{sha(path)}\n')
        (stage / 'payloads.tsv').write_text(''.join(rows))
        checks = list((stage / 'tools').iterdir()) + [stage / 'anykernel.sh', stage / 'payloads.tsv']
        (stage / 'tools.sha256').write_text(''.join(sha(path) + '  ' + str(path.relative_to(stage)) + '\n' for path in checks))
        if case == 'missing_partition': (byname / ('vendor_boot' + slot)).unlink()
        if case == 'physical_too_small': (byname / ('vendor_boot' + slot)).write_bytes(b'x')
        if case == 'tampered_payload': (stage / 'boot.img').write_bytes(b'wrong'.ljust(4096, b'0'))
        if case == 'missing_payload': (stage / 'boot.img').unlink()
        if case == 'duplicate_payload':
            (stage / 'payloads.tsv').write_text(''.join(rows[:-1] + [rows[0]]))
            # Check business-level duplicate validation after a valid manifest hash.
            (stage / 'tools.sha256').write_text(''.join(sha(path) + '  ' + str(path.relative_to(stage)) + '\n' for path in checks))
        if case == 'missing_tool': (stage / 'tools/lptools_static').unlink()
        if case == 'corrupt_tool': (stage / 'tools/lptools_static').write_text(stub + '\n# corrupted\n')
        if case == 'stale_mapping': (mapper / 'system_dlkm_ak3').write_bytes(b'stale')
        env = dict(os.environ, PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'],
                   TEST_ROOT=str(root), TEST_CASE=case,
                   TEST_SLOT='' if case == 'bad_slot' else slot, AKHOME=str(stage), OUTFD='1')
        run = subprocess.run(['bash', str(stage / 'anykernel.sh')], cwd=stage, env=env,
                             capture_output=True, text=True, timeout=30)
        writes = (root / 'writes.log').read_text().splitlines() if (root / 'writes.log').exists() else []
        inactive_unchanged = all(sha(path) == digest for path, digest in inactive.items())
        unslotted_unchanged = all(sha(path) == digest for path, digest in unslotted.items())
        if case in positive:
            assert run.returncode == 0, (case, run.stdout, run.stderr)
            assert writes == names and inactive_unchanged and unslotted_unchanged, case
            for name in names:
                target = mapper / 'system_dlkm_ak3' if name == 'system_dlkm' and case == 'resize_create' else (mapper if name.endswith('dlkm') else byname) / (name + slot)
                assert target.read_bytes()[:4096] == (stage / (name + '.img')).read_bytes(), (case, name)
        elif case == 'regression_unslotted_preference':
            assert not unslotted_unchanged, 'Slot-preference regression was not exercised'
        else:
            assert run.returncode != 0, (case, run.stdout, run.stderr)
            assert inactive_unchanged, case
            if case not in late_failure:
                assert not writes, (case, writes)
            if case == 'readback_corruption':
                assert writes == names[:-1] and 'readback failed' in run.stdout + run.stderr
        results.append({'case': case, 'status': 'PASS' if case in positive else 'REJECTED',
                        'writes': writes, 'inactive_slot_unchanged': inactive_unchanged})
report = {'status': 'PASS', 'installer_sha256': sha(a.stage / 'anykernel.sh'),
          'core_sha256': sha(a.stage / 'tools/ak3-core.sh'), 'cases': results,
          'limits': 'Real installer and patched AK3 core run under Bash with regular-file device nodes and mocked Android properties/blockdev/HT/lp tools. AArch64 helper binaries and real partition/AVB/resize behavior require recovery runtime validation. No phone flashed.'}
(a.stage / 'installer-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'PASS {len(results)} installer scenarios, active-slot selection, resize paths and preflight/readback failures')
