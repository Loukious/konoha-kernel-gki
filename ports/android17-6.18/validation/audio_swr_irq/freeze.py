from pathlib import Path
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import tempfile

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
A = W / 'artifacts/20261031T232938953896Z'
S = W / 'onyx_swr_irq'
B = W / 'bazel-bin/onyx_swr_irq'
ROM = Path('/home/loukious/Android/custom-rom/out/target/product/onyx/obj/sm8735-modules/qcom/opensource/audio-kernel/soc')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
read = lambda name: json.loads((H / name).read_text())
write = lambda name, data: (H / name).write_text(json.dumps(data, indent=2) + '\n')
assert not (H / 'frozen-source').exists(), 'Already frozen'
paths = read('sources.json')
assert len(paths) == 3
for path, digest in read('base-sha256.json').items():
    assert sha(H / 'original' / path) == digest
    assert sha(W / 'onyx' / path) == digest, 'Integrated base changed: ' + path
hashes = {path: sha(S / path) for path in paths}
assert hashes == {path: sha(H / 'private-source' / path) for path in paths}
shared = read('shared-inputs.json')
for path, item in shared.items():
    assert sha(W / item['package'] / path) == sha(S / path) == item['sha256']

with tempfile.TemporaryDirectory(prefix='swr-irq-replay-') as temp:
    root = Path(temp)
    for path in paths:
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(H / 'original' / path, dest)
    run = subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(H / 'patch')],
                         cwd=root, text=True, capture_output=True, check=True)
    assert not re.search(r'fuzz|offset|FAILED', run.stdout + run.stderr, re.I)
    assert hashes == {path: sha(root / path) for path in paths}
    (H / 'replay.log').write_text(run.stdout + run.stderr)

test = read('staged-validation.json')
assert test['status'] == 'PASS' and len(test['negative_controls']) == 6
for path, digest in test['source_sha256'].items():
    assert sha(Path(path)) == digest

spec = importlib.util.spec_from_file_location('module_verifier', P / 'verify_modules.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)
modules = read('modules.json')
targets = {module['target'] for module in modules}
providers = [A / 'gki/kernel_aarch64_Module.symvers'] + [path for path in sorted((A / 'platform').glob('*_Module.symvers'))
             if path.name.removesuffix('_Module.symvers') not in targets]
providers += [B / name / (name + '_Module.symvers') for name in sorted(targets)]
results = verifier.verify([B / module['target'] / module['output'] for module in modules], providers,
                          (A / 'gki/kernel.release').read_text().strip())
evidence = {}
for module, result in zip(modules, results):
    name = module['target']
    old_symvers = A / 'platform' / (name + '_Module.symvers')
    new_symvers = B / name / (name + '_Module.symvers')
    parse_exports = lambda p: {line.split()[1]: line.split()[0] for line in p.read_text().splitlines()}
    old_exports = parse_exports(old_symvers)
    new_exports = parse_exports(new_symvers)
    assert all(new_exports.get(symbol) == crc for symbol, crc in old_exports.items()), name
    additions = sorted(set(new_exports) - set(old_exports))
    assert additions == (['swr_handle_slave_irq', 'swr_set_slave_irq'] if name == 'swr_dlkm' else [])
    native = ROM / module['output']
    modinfo = lambda field, path: subprocess.check_output(['modinfo', '-F', field, str(path)], text=True).strip().splitlines()
    for field in ['depends', 'alias', 'parm']:
        assert sorted(modinfo(field, native)) == sorted(modinfo(field, B / name / module['output'])), (name, field)
    native_units = [line.removeprefix('../sm8735-modules/').removesuffix('.o') + '.c'
                    for line in (ROM / (name + '.mod')).read_text().splitlines()]
    srcs = json.loads((B / (name + '_makefiles') / 'srcs.json').read_text())
    units = [str(Path(path).relative_to('onyx_swr_irq')) for entry in srcs
             for path in entry.get('files', []) if path.endswith('.c')]
    assert sorted(units) == sorted(native_units), name
    kbuild = B / (name + '_makefiles') / 'makefiles/Kbuild'
    object_units = re.findall(re.escape(name) + r'-y \+= (\S+)\.o', kbuild.read_text())
    generated_units = ['ddk_modinfo'] if name == 'swr_dlkm' else []
    assert sorted(object_units) == sorted(
        [path.removesuffix('.c') for path in native_units] + generated_units), (name, object_units)
    metadata = kbuild.parent / 'ddk_modinfo.c'
    if generated_units:
        assert metadata.read_text() == ('#include <linux/compiler.h>\n'
            'static const char __UNIQUE_ID(built_with)[] __used __section(".modinfo") '
            '__aligned(1) = "built_with=DDK";\n')
    command_hashes = {}
    dest = H / 'artifacts' / name
    dest.mkdir(parents=True)
    if generated_units:
        shutil.copyfile(metadata, dest / metadata.name)
    for path in native_units:
        cmd = ROM / ('.' + Path(path).stem + '.o.cmd')
        command_hashes[path] = sha(cmd)
        shutil.copyfile(cmd, dest / ('native' + cmd.name))
    config = B / (name + '_config') / 'out_dir/.config'
    flags = B / (name + '_makefiles') / 'makefiles' / (name + '.cflags_shipped')
    for path, filename in [(B / name / module['output'], module['output']), (new_symvers, 'Module.symvers'),
                           (config, '.config'), (flags, 'cflags_shipped'), (kbuild, 'Kbuild'),
                           (ROM / (name + '.mod'), 'native.mod'),
                           (B / (name + '_makefiles') / 'srcs.json', 'srcs.json')]:
        shutil.copyfile(path, dest / filename)
    evidence[name] = {'native_module_sha256': sha(native), 'native_units': native_units,
                      'native_command_sha256': command_hashes, 'preserved_export_crcs': old_exports,
                      'added_exports': additions, 'config_sha256': sha(config), 'module': result,
                      'ddk_generated_metadata_sha256': sha(metadata) if generated_units else None}

build = Path('/tmp/onyx-swr-irq-build5.log')
assert 'Build completed successfully' in build.read_text()
assert not re.search(r'error:|warning:|ERROR:|WARNING:', build.read_text())
shutil.copyfile(build, H / 'build.log')
check = subprocess.run([str(W / 'common/scripts/checkpatch.pl'), '--strict', '--no-tree', '--no-signoff', str(H / 'patch')],
                        text=True, capture_output=True)
(H / 'checkpatch.txt').write_text(check.stdout + check.stderr)
assert check.returncode == 0, check.stdout + check.stderr
for path in paths + list(shared):
    dest = H / 'frozen-source' / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(S / path, dest)
write('final-sha256.json', {path: sha(S / path) for path in paths + list(shared)})
write('module-verification.json', results)
write('native-evidence.json', evidence)
write('validation.json', {'status': 'PASS', 'module_verification': results, 'source_sha256': hashes,
      'shared_inputs': shared, 'patch_sha256': sha(H / 'patch'), 'test': test,
      'existing_export_crcs': 'unchanged', 'native_units': 'exact Kbuild and srcs.json equality',
      'checkpatch': '0 errors, 0 warnings, 0 checks',
      'limits': test['limits']})
(H / 'STATE.md').write_text('FROZEN synchronized SoundWire IRQ provider extension. Three existing source/header edits, one exact unchanged regmap companion. '
    'Two modules preserve every existing export CRC and native unit, and add two checked IRQ APIs. Nine host scenarios and six compiled rejecting controls pass. '
    'No central source, baseline or prior handoff was modified. Root integrates this patch before codec consumers.\n')
files = ['patch', 'sources.json', 'imports.json', 'existing-pins.json', 'base-sha256.json',
         'modules.json', 'BUILD.fragment', 'Kconfig.fragment', 'defconfig.fragment', 'shared-inputs.json',
         'stage.py', 'verify_swr_irq.py', 'tests/swr_irq.c', 'staged-validation.json', 'final-sha256.json',
         'module-verification.json', 'native-evidence.json', 'validation.json', 'build.log',
         'checkpatch.txt', 'replay.log', 'swr-irq-port.md', 'STATE.md']
write('handoff-sha256.json', {name: sha(H / name) for name in files})
print('Frozen SoundWire IRQ extension:', sum(result['imports_checked'] for result in results), 'CRCs; native units/exports preserved')
