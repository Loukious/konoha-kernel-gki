#!/usr/bin/env python3
"""Create matched module trees for the explicitly requested experimental ZIP."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import re

from verify_modules import verify, versioned_imports

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('artifact', type=Path)
p.add_argument('rom_product', type=Path)
p.add_argument('--first-stage', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
assert not a.output.exists(), 'Use a new output directory'
read = lambda p: json.loads(p.read_text())
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
norm = lambda s: Path(s).name.removesuffix('.ko').replace('-', '_')
release = (a.artifact / 'gki/kernel.release').read_text().strip()
first = read(a.first_stage / 'report.json')
assert first['status'] == 'PASS' and first['release'] == release
for name, board in first['supported_boards'].items():
    assert sha(a.artifact / 'devicetrees/checked' / name) == board['sha256']
root = a.output / 'depmod-root'
directory = root / 'lib/modules' / release
directory.mkdir(parents=True)
subprocess.run(['python3', str(HERE / 'audit_packaging.py'), str(a.artifact),
                str(a.rom_product), '--output', str(a.output / 'availability.json')], check=True)
catalog = read(a.output / 'availability.json')['available_modules']
paths = {}
with tarfile.open(a.artifact / 'gki/modules_staging_dir.tar.gz') as tar:
    for name, item in catalog.items():
        if item['kind'] == 'builtin':
            continue
        target = directory / (Path(item['path']).name if item['kind'] == 'platform'
                              else Path(item['member']).name)
        if item['kind'] == 'platform':
            shutil.copyfile(item['path'], target)
        else:
            target.write_bytes(tar.extractfile(item['member']).read())
        paths[name] = target
for name in first['module_load_order']:
    assert sha(a.first_stage / 'lib/modules' / name) == sha(paths[norm(name)]), name
dependencies, softdeps = {}, {}
for name, path in paths.items():
    info = lambda field: subprocess.check_output(['modinfo', '-F', field, str(path)], text=True).strip()
    assert info('vermagic').startswith(release + ' ') and 'modversions' in info('vermagic').split(), name
    dependencies[name] = [norm(n) for n in info('depends').split(',') if n]
    assert all(n in catalog for n in dependencies[name]), (name, dependencies[name])
    soft = {'pre:': [], 'post:': []}
    group = None
    for token in info('softdep').split():
        if token in soft:
            group = token
        else:
            assert group is not None, (name, token)
            soft[group].append(norm(token))
    softdeps[name] = soft
missing_soft = {n: [d for group in soft.values() for d in group if d not in catalog]
                for n, soft in softdeps.items()}
missing_soft = {n: deps for n, deps in missing_soft.items() if deps}
legacy_optional = {'subsys_pil_tz', 'qcom_arm_smmu_mod', 'phy_msm_snps_hs'}
native_paths = {}
for partition in ['vendor_ramdisk', 'vendor_dlkm', 'system_dlkm']:
    for path in (a.rom_product / partition / 'lib/modules').rglob('*.ko'):
        native_paths.setdefault(norm(path.name), path)
native_builtin = a.rom_product / 'obj/KERNEL_OBJ/modules.builtin'
native_builtins = {norm(n) for n in native_builtin.read_text().splitlines()}
assert not legacy_optional & (set(native_paths) | native_builtins)
legacy_proof = {'native_builtin_sha256': sha(native_builtin), 'parents': {}}
for name, missing in missing_soft.items():
    allowed = set(missing) & legacy_optional
    if not allowed:
        continue
    native = native_paths[name]
    original_soft = subprocess.check_output(['modinfo', '-F', 'softdep', str(native)], text=True).strip()
    original_names = {norm(t) for t in original_soft.split() if t not in ['pre:', 'post:']}
    assert allowed <= original_names, (name, allowed, original_soft)
    legacy_proof['parents'][name] = {'native_module_sha256': sha(native),
                                    'native_softdep': original_soft,
                                    'optional_missing_names': sorted(allowed)}
blocked = {n for n, deps in missing_soft.items() if set(deps) - legacy_optional}
while True:
    additional = {n for n in paths if any(d in blocked for d in dependencies[n] +
                  softdeps[n]['pre:'] + softdeps[n]['post:'])}
    if additional <= blocked:
        break
    blocked |= additional
assert not {norm(n) for n in first['module_load_order']} & blocked
def load_order(requested):
    done, visiting, order = set(), set(), []
    def add(name, soft=False):
        if name not in catalog:
            assert soft and name in legacy_optional, name
            return
        if name in done or catalog[name]['kind'] == 'builtin':
            return
        if name in visiting:
            assert soft, ('hard dependency cycle', name)
            return
        visiting.add(name)
        for dep in dependencies[name] + softdeps[name]['pre:']:
            add(dep, dep in softdeps[name]['pre:'])
        visiting.remove(name)
        done.add(name)
        order.append(paths[name].name)
        for dep in softdeps[name]['post:']:
            add(dep, True)
    for name in requested:
        if name in catalog and name not in blocked:
            add(name)
    return order
def requests(path):
    return [norm(s.split('#', 1)[0].strip()) for s in path.read_text().splitlines()
            if s.split('#', 1)[0].strip()]
stages = {}
for partition, filename in [('vendor_ramdisk', 'modules.load'),
                            ('vendor_ramdisk', 'modules.load.recovery'),
                            ('vendor_dlkm', 'modules.load'), ('system_dlkm', 'modules.load')]:
    source = a.rom_product / partition / 'lib/modules' / filename
    requested = requests(source)
    order = first['module_load_order'] if partition == 'vendor_ramdisk' and filename == 'modules.load' else load_order(requested)
    stages[partition + '/' + filename] = {'order': order, 'source_sha256': sha(source),
        'unavailable': [n for n in requested if n not in catalog],
        'blocked_by_soft_dependency': [n for n in requested if n in blocked],
        'already_builtin': [n for n in requested if n in catalog and catalog[n]['kind'] == 'builtin']}
for name in ['modules.builtin', 'modules.builtin.modinfo']:
    shutil.copyfile(a.artifact / 'gki' / name, directory / name)
(directory / 'modules.order').write_text('\n'.join(path.name for path in paths.values()) + '\n')
verification = verify(list(paths.values()),
    [a.artifact / 'gki/kernel_aarch64_Module.symvers'] +
    sorted((a.artifact / 'platform').glob('*_Module.symvers')), release)
(a.output / 'module-verification.json').write_text(json.dumps(verification, indent=2) + '\n')
check = subprocess.run(['depmod', '-C', '/dev/null', '-b', str(root), '-e', '-E',
                        str(a.artifact / 'gki/kernel_aarch64_Module.symvers'), release],
                       capture_output=True, text=True)
(a.output / 'depmod.log').write_text(check.stdout + check.stderr)
assert check.returncode == 0, check.stdout + check.stderr
tool_disagreements = []
for warning in check.stderr.splitlines():
    # kmod 34.2's depmod -E disagrees on long Rust imports even when the ELF
    # extended CRC entries match Module.symvers. The direct verifier above
    # checks every classic/extended import before this narrow diagnostic audit.
    match = re.fullmatch(r'depmod: WARNING: .*/(rust_binder\.ko) disagrees about version of symbol (\S+)', warning)
    assert match, warning
    module, symbol = match.groups()
    assert len(symbol) > 55 and symbol in versioned_imports(directory / module), warning
    tool_disagreements.append({'module': module, 'symbol': symbol,
                               'resolution': 'Direct extended ELF CRC matches the build export table.'})
for name in {norm(n) for stage in stages.values() for n in stage['order']}:
    check = subprocess.run(['modprobe', '-C', '/dev/null', '-d', str(root), '-S', release,
                            '--show-depends', name], capture_output=True, text=True)
    assert check.returncode == 0 and not check.stderr.strip(), (name, check.stderr)
partition_modules = {}
packaged = set()
for partition in ['vendor_ramdisk', 'vendor_dlkm', 'system_dlkm']:
    roots = [norm(n) for key, stage in stages.items() if key.startswith(partition + '/')
             for n in stage['order']]
    if partition != 'system_dlkm':
        roots += [n for n, item in catalog.items() if item['kind'] == 'platform']
    selected = load_order(roots)
    packaged.update(selected)
    partition_modules[partition] = selected
    part_root = a.output / 'partition-depmod' / partition
    part_dir = part_root / 'lib/modules' / release
    part_dir.mkdir(parents=True)
    for name in selected:
        shutil.copyfile(directory / name, part_dir / name)
    for name in ['modules.builtin', 'modules.builtin.modinfo']:
        shutil.copyfile(directory / name, part_dir / name)
    (part_dir / 'modules.order').write_text('\n'.join(selected) + '\n')
    check = subprocess.run(['depmod', '-C', '/dev/null', '-b', str(part_root), '-e', '-E',
                            str(a.artifact / 'gki/kernel_aarch64_Module.symvers'), release],
                           capture_output=True, text=True)
    (a.output / (partition + '-depmod.log')).write_text(check.stdout + check.stderr)
    assert check.returncode == 0 and not check.stderr.strip(), (partition, check.stdout, check.stderr)
    for key, stage in stages.items():
        if not key.startswith(partition + '/'):
            continue
        for name in stage['order']:
            check = subprocess.run(['modprobe', '-C', '/dev/null', '-d', str(part_root),
                                    '-S', release, '--show-depends', norm(name)],
                                   capture_output=True, text=True)
            assert check.returncode == 0 and not check.stderr.strip(), (partition, name, check.stderr)
    target = a.output / partition / 'lib/modules'
    shutil.copytree(part_dir, target)
    for key, stage in stages.items():
        if key.startswith(partition + '/'):
            (target / key.split('/')[1]).write_text('\n'.join(stage['order']) + '\n')
    for path in (a.rom_product / partition / 'lib/modules').glob('modules.*'):
        if path.is_file() and (path.name.endswith('.blocklist') or path.name == 'modules.options'):
            shutil.copyfile(path, target / path.name)
result = {'status': 'PASS', 'purpose': 'experimental boot-test module bundle',
          'artifact': str(a.artifact), 'release': release, 'modules': len(packaged),
          'catalog_modules_checked': len(paths), 'partition_modules': partition_modules,
          'first_stage_report_sha256': sha(a.first_stage / 'report.json'),
          'stages': stages, 'missing_soft_dependencies': missing_soft,
          'baseline_optional_soft_dependency_evidence': legacy_proof,
          'versioned_imports_checked': sum(m['imports_checked'] for m in verification),
          'depmod_tool_disagreements': tool_disagreements,
          'sha256': {str(path.relative_to(a.output)): sha(path) for partition in
                     ['vendor_ramdisk', 'vendor_dlkm', 'system_dlkm']
                     for path in (a.output / partition).rglob('*') if path.is_file()},
          'limits': 'Matched module metadata and dry runs only; missing optional entries are omitted explicitly for the user-selected experimental boot test. No image flash or phone boot validated.'}
(a.output / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
print(f'Experimental bundle: {len(packaged)} packaged modules, {len(paths)} checked, four verified load lists')
