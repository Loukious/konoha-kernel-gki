#!/usr/bin/env python3
"""Assemble and dry-run an Onyx first-stage module tree without flashing."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('artifact', type=Path)
p.add_argument('rom_product', type=Path)
p.add_argument('--inventory', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
norm = lambda s: Path(s).name.removesuffix('.ko').replace('-', '_')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_text())
inventory = read(a.inventory)
for name, board in inventory['boards'].items():
    assert sha(a.artifact / 'devicetrees/checked' / name) == board['sha256'], name
audit = a.output.with_name(a.output.name + '-availability.json')
assert not a.output.exists() and not audit.exists(), 'Use a new output directory'
subprocess.run(['python3', str(HERE / 'audit_packaging.py'), str(a.artifact),
                str(a.rom_product), '--output', str(audit)], check=True)
catalog = read(audit)['available_modules']
native = {}
for directory in ['vendor_ramdisk', 'vendor_dlkm', 'system_dlkm']:
    for path in (a.rom_product / directory / 'lib/modules').rglob('*.ko'):
        native.setdefault(norm(path.name), path)
reverse = {}
for name, path in native.items():
    dependencies = subprocess.check_output(['modinfo', '-F', 'depends', str(path)], text=True).strip()
    for dep in dependencies.split(','):
        if dep:
            reverse.setdefault(norm(dep), []).append(name)
source = a.rom_product / 'vendor_ramdisk/lib/modules/modules.load'
requested = [s.split('#', 1)[0].strip() for s in source.read_text().splitlines()]
requested = [norm(s) for s in requested if s]
missing = set(requested) - set(catalog)
dispositions = {}
for name in sorted(missing):
    item = next(v for k, v in inventory['remaining_modules'].items() if norm(k) == name)
    category = item['assessment']
    if name == 'cqhci':
        assert set(reverse.get(name, [])) == {'sdhci_msm'}
        assert inventory['remaining_modules']['sdhci-msm']['assessment'] == 'matches disabled Onyx DT node only'
        reason = 'Only native consumer is SDHCI, disabled in both supported trees.'
    else:
        assert category in ['has OF aliases but no Onyx DT match', 'matches disabled Onyx DT node only'], (name, category)
        reason = category
    assert not (set(reverse.get(name, [])) - missing), (name, reverse.get(name))
    dispositions[name] = {'reason': reason, 'native_reverse_dependencies': reverse.get(name, []),
                          'native_module_sha256': sha(native[name])}
# Explicit references must be reviewed before excluding a variant. Standard
# modules.load files are deliberately excluded from this text search.
references = []
for directory in ['vendor_ramdisk', 'vendor']:
    for path in (a.rom_product / directory).rglob('*'):
        if not path.is_file() or path.suffix not in ['.rc', '.sh']:
            continue
        text = path.read_text(errors='replace')
        for name in missing:
            if name in text.replace('-', '_'):
                references.append({'path': str(path), 'module': name})
assert not references, references
release = (a.artifact / 'gki/kernel.release').read_text().strip()
root = a.output / 'depmod-root'
module_dir = root / 'lib/modules' / release
module_dir.mkdir(parents=True)
order, visiting, done = [], set(), set()
gki = tarfile.open(a.artifact / 'gki/modules_staging_dir.tar.gz')
def add(name):
    if name in done:
        return
    assert name not in visiting, ('dependency cycle', name)
    assert name in catalog, ('missing dependency', name)
    item = catalog[name]
    if item['kind'] == 'builtin':
        done.add(name)
        return
    visiting.add(name)
    if item['kind'] == 'platform':
        src = Path(item['path'])
        target = module_dir / src.name
        shutil.copyfile(src, target)
    else:
        target = module_dir / Path(item['member']).name
        target.write_bytes(gki.extractfile(item['member']).read())
    deps = subprocess.check_output(['modinfo', '-F', 'depends', str(target)], text=True).strip()
    for dependency in deps.split(','):
        if dependency:
            add(norm(dependency))
    softdeps = subprocess.check_output(['modinfo', '-F', 'softdep', str(target)], text=True).strip()
    soft = {'pre:': [], 'post:': []}
    group = None
    for token in softdeps.split():
        if token in soft:
            group = token
        else:
            assert group is not None, ('invalid softdep', name, softdeps)
            soft[group].append(norm(token))
    for dependency in soft['pre:']:
        add(dependency)
    visiting.remove(name)
    done.add(name)
    order.append(target.name)
    for dependency in soft['post:']:
        add(dependency)
for name in requested:
    if name not in missing:
        add(name)
gki.close()
for name in ['modules.builtin', 'modules.builtin.modinfo']:
    shutil.copyfile(a.artifact / 'gki' / name, module_dir / name)
(module_dir / 'modules.order').write_text('\n'.join(order) + '\n')
(module_dir / 'modules.load').write_text('\n'.join(order) + '\n')
run = subprocess.run(['depmod', '-C', '/dev/null', '-b', str(root), '-e', '-E',
                      str(a.artifact / 'gki/kernel_aarch64_Module.symvers'), release],
                     capture_output=True, text=True)
(a.output / 'depmod.log').write_text(run.stdout + run.stderr)
assert run.returncode == 0 and not run.stderr.strip(), run.stdout + run.stderr
for name in order:
    check = subprocess.run(['modprobe', '-C', '/dev/null', '-d', str(root), '-S', release,
                            '--show-depends', norm(name)], capture_output=True, text=True)
    assert check.returncode == 0, check.stderr
flat = a.output / 'lib/modules'
shutil.copytree(module_dir, flat)
report = {'status': 'PASS', 'artifact': str(a.artifact), 'release': release,
          'source_load_list_sha256': sha(source), 'inventory_sha256': sha(a.inventory),
          'supported_boards': inventory['boards'], 'omitted_variants': dispositions,
          'module_load_order': order, 'modules': len(order),
          'sha256': {path.name: sha(path) for path in flat.iterdir() if path.is_file()},
          'limits': 'Name/dependency/CRC metadata and modprobe dry-run only. No image install, phone boot or hardware validation. Recovery and second-stage lists remain separate gates.'}
(a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'First-stage tree: {len(order)} modules; {len(missing)} inactive variants omitted with evidence')
