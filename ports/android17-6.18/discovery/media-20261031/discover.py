"""Record exact native media/DSP compilation inputs without modifying source."""
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
R = Path('/home/loukious/Android/custom-rom')
N = R / 'kernel/xiaomi/sm8735-modules'
O = R / 'out/target/product/onyx/obj/sm8735-modules'
H = Path(__file__).resolve().parent
H.mkdir(exist_ok=True)
project = 'baseline/kernel/xiaomi/sm8735-modules'
repo = W / project
rev = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
tree = {}
for entry in subprocess.check_output(['git', '-C', str(repo), 'ls-tree', '-r', '-z', rev]).split(b'\0'):
    if entry:
        meta, path = entry.decode().split('\t', 1)
        tree[path] = meta.split()[2]
central = {i['path']: i for i in json.loads((P / 'imports.json').read_text())}
summary = {}

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def canonical(path):
    if 'sm8735-modules/' in path:
        return (N / path.split('sm8735-modules/', 1)[1]).resolve().relative_to(N).as_posix()
    return None

for rel in ['camera-kernel/camera', 'camera-kernel/cameralog', 'camera-kernel/cameramsger', 'eva-kernel/msm/msm-eva', 'video-driver/msm_video/msm_video', 'dsp-kernel/frpc-adsprpc']:
    base = O / 'qcom/opensource' / rel
    name = base.name
    objects, paths, kernel, configs, flags, includes = [], set(), set(), set(), {}, {}
    forced = set()
    for obj in base.with_suffix('.mod').read_text().splitlines():
        op = (O / obj.split('sm8735-modules/', 1)[1]).resolve()
        cp = op.parent / ('.' + op.name + '.cmd')
        cmd = cp.read_text()
        args = shlex.split(cmd.splitlines()[0].split(' := ', 1)[1])
        source = next(line.split(' := ', 1)[1] for line in cmd.splitlines() if line.startswith('source_'))
        source = canonical(source)
        assert source in tree
        objects.append({'path': source, 'object': obj, 'receipt': str(cp), 'receipt_sha256': sha(cp)})
        paths.add(source)
        defs = [x for x in args if x.startswith('-D') and not x.startswith(('-DKBUILD_', '-D__KBUILD_'))]
        key = hashlib.sha256(json.dumps(defs).encode()).hexdigest()
        flags.setdefault(key, {'defines': defs, 'sources': []})['sources'].append(source)
        incs = [x[2:] for x in args if x.startswith('-I')]
        key = hashlib.sha256(json.dumps(incs).encode()).hexdigest()
        includes.setdefault(key, {'includes': incs, 'sources': []})['sources'].append(source)
        forced.update(args[i + 1] for i, x in enumerate(args[:-1]) if x == '-include')
        for line in cmd.split('deps_', 1)[1].split(' := ', 1)[1].split('\n\n', 1)[0].splitlines():
            dep = line.strip().removesuffix('\\').strip()
            if not dep:
                continue
            if dep.startswith('$(wildcard '):
                configs.add(dep)
                continue
            path = canonical(dep)
            if path:
                paths.add(path)
            else:
                kernel.add(dep)
    missing = sorted(paths - tree.keys())
    assert not missing, missing
    pins = [{'project': project, 'revision': rev, 'path': path, 'blob': tree[path]} for path in sorted(paths) if path not in central]
    changed = []
    for path in sorted(paths):
        original = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', tree[path]])
        if original != (N / path).read_bytes():
            changed.append({'path': path, 'pinned_sha256': hashlib.sha256(original).hexdigest(), 'native_sha256': sha(N / path)})
    ko = base.with_suffix('.ko')
    data = {'project': project, 'revision': rev, 'objects': objects, 'pins': pins, 'existing_paths': sorted(paths & central.keys()), 'kernel_headers': sorted(kernel), 'config_lookups': sorted(configs), 'define_groups': flags, 'include_groups': includes, 'forced_includes': sorted(forced), 'native_source_differences': changed, 'module': str(ko), 'module_sha256': sha(ko), 'module_list_sha256': sha(base.with_suffix('.mod')), 'depends': subprocess.check_output(['modinfo', '-F', 'depends', str(ko)], text=True).strip(), 'aliases': subprocess.check_output(['modinfo', '-F', 'alias', str(ko)], text=True).splitlines(), 'imports': subprocess.check_output(['modprobe', '--show-modversions', str(ko)], text=True).splitlines()}
    (H / (name + '.json')).write_text(json.dumps(data, indent=2) + '\n')
    summary[name] = {'native_units': len(objects), 'fresh_pins': len(pins), 'existing_inputs': len(data['existing_paths']), 'changed_native_sources': len(changed), 'define_groups': len(flags), 'include_groups': len(includes), 'depends': data['depends']}
(H / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
