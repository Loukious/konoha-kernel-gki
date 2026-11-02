#!/usr/bin/env python3
"""Compare ROM load lists with matching built modules before image assembly."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifact', type=Path)
parser.add_argument('rom_product', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
normalize = lambda s: Path(s).name.removesuffix('.ko').replace('-', '_')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
available = {}
for name in (args.artifact / 'gki/modules.builtin').read_text().splitlines():
    available[normalize(name)] = {'kind': 'builtin', 'path': name}
for path in (args.artifact / 'platform').glob('*.ko'):
    if normalize(path.name) in available:
        raise ValueError('Platform module duplicates a builtin: ' + path.name)
    available[normalize(path.name)] = {'kind': 'platform', 'path': str(path), 'sha256': sha(path)}
recipe = json.loads((args.artifact / 'inputs/modules.json').read_text())
overrides = {normalize(item['output']): item['replaces_gki']
             for item in recipe if 'replaces_gki' in item}
assert all(value.startswith('kernel/') and '..' not in Path(value).parts
           and normalize(value) == name for name, value in overrides.items())
applied_overrides = set()
archive = args.artifact / 'gki/modules_staging_dir.tar.gz'
with tarfile.open(archive) as tar:
    for member in tar.getmembers():
        if member.isfile() and member.name.endswith('.ko'):
            name = normalize(member.name)
            if name in available:
                expected = 'lib/modules/' + (args.artifact / 'gki/kernel.release').read_text().strip() + '/' + overrides.get(name, '')
                if available[name]['kind'] != 'platform' or name not in overrides or str(Path(member.name)) != expected or name in applied_overrides:
                    raise ValueError('Duplicate platform/GKI module: ' + name)
                available[name]['replaced_gki_member'] = member.name
                available[name]['replaced_gki_sha256'] = hashlib.sha256(tar.extractfile(member).read()).hexdigest()
                applied_overrides.add(name)
                continue
            available[name] = {'kind': 'gki', 'archive': str(archive), 'member': member.name}
assert applied_overrides == set(overrides), 'Every explicit GKI replacement must match exactly one archive member'
lists = {}
for rel in ['vendor_ramdisk/lib/modules/modules.load',
            'vendor_ramdisk/lib/modules/modules.load.recovery',
            'vendor_dlkm/lib/modules/modules.load',
            'system_dlkm/lib/modules/modules.load']:
    path = args.rom_product / rel
    entries = [s.split('#', 1)[0].strip() for s in path.read_text().splitlines()]
    entries = [s for s in entries if s]
    missing = [s for s in entries if normalize(s) not in available]
    lists[rel] = {'sha256': sha(path), 'entries': len(entries),
                  'missing': missing, 'missing_count': len(missing)}
result = {'status': 'BLOCKED_BY_MISSING_LOAD_ENTRIES' if any(x['missing'] for x in lists.values()) else 'LOAD_NAMES_COVERED',
          'artifact': str(args.artifact), 'gki_archive_sha256': sha(archive),
          'load_lists': lists, 'available_modules': available,
          'limits': ['This is a module-name availability audit, not image or runtime verification.',
                     'Missing entries require porting or an explicit hardware/service-backed disposition.',
                     'Do not package native 6.6 modules with the 6.18 kernel.',
                     'Packaging must also verify module CRCs, dependency order, partition format/size, AVB and installer behavior.']}
args.output.write_text(json.dumps(result, indent=2) + '\n')
for name, item in lists.items():
    print(f"{name}: {item['missing_count']}/{item['entries']} names missing")
