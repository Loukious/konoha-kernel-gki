"""Snapshot the native Onyx amplifier translation units and exact source pins."""
from pathlib import Path
import hashlib
import json
import re
import shlex
import shutil
import subprocess

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
R = Path('/home/loukious/Android/custom-rom')
N = R / 'kernel/xiaomi/sm8735-modules'
O = R / 'out/target/product/onyx/obj/sm8735-modules'
project = 'baseline/kernel/xiaomi/sm8735-modules'
revision = '8500748dae32f10507c02f1c94bc971553cb525b'
repo = W / project
assert not (H / 'imports.json').exists(), 'Initial snapshot already created'
assert subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() == revision
tree = {}
for entry in subprocess.check_output(['git', '-C', str(repo), 'ls-tree', '-r', '-z', revision]).split(b'\0'):
    if entry:
        meta, name = entry.decode().split('\t', 1)
        tree[name] = meta.split()[2]
central = {item['path']: item for item in json.loads((P / 'imports.json').read_text())}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
write = lambda name, data: (H / name).write_text(json.dumps(data, indent=2) + '\n')

def canonical(path):
    if 'sm8735-modules/' in path:
        return (N / path.split('sm8735-modules/', 1)[1]).resolve().relative_to(N).as_posix()
    return None

records = {}
all_paths = set()
for relative in ['aw882xx/aw882xx_dlkm', 'sia91xx/sia91xx_dlkm', 'sia91xx/sia91xx_tuning_dlkm']:
    base = O / 'qcom/opensource/audio-kernel/asoc/codecs' / relative
    name = base.name
    units, inputs, kernel, definitions, includes = [], set(), set(), {}, {}
    receipts = H / 'native-receipts' / name
    receipts.mkdir(parents=True, exist_ok=True)
    for obj in base.with_suffix('.mod').read_text().splitlines():
        object_path = O / obj.split('sm8735-modules/', 1)[1]
        receipt = object_path.parent / ('.' + object_path.name + '.cmd')
        command = receipt.read_text()
        args = shlex.split(command.splitlines()[0].split(' := ', 1)[1])
        source = canonical(next(line.split(' := ', 1)[1] for line in command.splitlines() if line.startswith('source_')))
        assert source in tree, source
        units.append(source)
        inputs.add(source)
        definitions[source] = [arg for arg in args if arg.startswith('-D') and not arg.startswith(('-DKBUILD_', '-D__KBUILD_'))]
        includes[source] = [arg[2:] for arg in args if arg.startswith('-I')]
        for line in command.split('deps_', 1)[1].split(' := ', 1)[1].split('\n\n', 1)[0].splitlines():
            dep = line.strip().removesuffix('\\').strip()
            if not dep or dep.startswith('$(wildcard '):
                continue
            path = canonical(dep)
            if path:
                assert path in tree, path
                inputs.add(path)
            else:
                kernel.add(dep)
        shutil.copyfile(receipt, receipts / receipt.name)
    shutil.copyfile(base.with_suffix('.mod'), receipts / (name + '.mod'))
    module = base.with_suffix('.ko')
    info = lambda field: subprocess.check_output(['modinfo', '-F', field, str(module)], text=True).splitlines()
    symbols = subprocess.check_output(['readelf', '-sW', str(module)], text=True)
    export_names = sorted(re.findall(r'\b__ksymtab_(\w+)\s*$', symbols, re.M))
    exports = subprocess.run(['modprobe', '--show-exports', str(module)], text=True, capture_output=True)
    assert exports.returncode == 0 or not export_names, module
    records[name] = {'units': units, 'inputs': sorted(inputs), 'kernel_headers': sorted(kernel),
        'defines': definitions, 'include_paths': includes,
        'native_module': str(module), 'native_module_sha256': sha(module),
        'depends': info('depends'), 'aliases': info('alias'), 'params': info('parm'),
        'exports': exports.stdout.splitlines(), 'export_names': export_names,
        'imports': subprocess.check_output(['modprobe', '--show-modversions', str(module)], text=True).splitlines(),
        'receipts_sha256': {path.name: sha(path) for path in receipts.iterdir()}}
    all_paths |= inputs
pins = []
differences = {}
for path in sorted(all_paths):
    blob = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', tree[path]])
    if (N / path).read_bytes() != blob:
        differences[path] = {'pinned': hashlib.sha256(blob).hexdigest(), 'native': sha(N / path)}
    if path in central:
        continue
    pins.append({'project': project, 'revision': revision, 'path': path, 'blob': tree[path]})
    for directory in ['original', 'private-source']:
        target = H / directory / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
write('imports.json', pins)
write('sources.json', [pin['path'] for pin in pins])
write('modules.json', [{'target': name, 'output': name + '.ko'} for name in records])
write('units.json', {name: record['units'] for name, record in records.items()})
write('native-evidence.json', records)
write('native-source-differences.json', differences)
write('shared-inputs.json', {path: sha(W / 'onyx' / path) for path in sorted(all_paths & central.keys())})
for name in ['Kconfig', 'platform_defconfig']:
    target = H / 'base-config' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(P / name, target)
for name in ['Kconfig.fragment', 'defconfig.fragment']:
    (H / name).write_text('# Native amplifier selections retain the pinned sunautoconf.h.\n')
(H / 'STATE.md').write_text('ACTIVE native AW882xx/SIA91xx amplifier handoff. Discovery/import already applied once; do not rerun. Three modules, 15 exact native units. Root owns machine/provider and central integration. No functional adaptation or completed build yet.\n')
print('Imported', len(pins), 'new source/header pins;', len(all_paths & central.keys()), 'shared inputs; native differences', len(differences))
