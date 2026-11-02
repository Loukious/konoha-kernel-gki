from pathlib import Path
import argparse
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
S = W / 'onyx_wcd_codecs'
B = W / 'bazel-bin/onyx_wcd_codecs'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
read = lambda name: json.loads((H / name).read_text())
write = lambda name, data: (H / name).write_text(json.dumps(data, indent=2) + '\n')
parser = argparse.ArgumentParser()
parser.add_argument('--build-log', type=Path, required=True)
args = parser.parse_args()
assert not (H / 'frozen-source').exists(), 'The WCD handoff is already frozen'
sources = read('sources.json')
modules = read('modules.json')
native = read('rom-evidence.json')
assert len(sources) == 31 and len(modules) == 6
assert sum(len(item['units']) for item in native.values()) == 16

build_log = args.build_log.read_text()
assert 'Build completed successfully' in build_log
cache_notice = ('WARNING: Build option --//build/kernel/kleaf:user_kmi_symbol_lists has changed, '
                'discarding analysis cache (this can be expensive, see https://bazel.build/advanced/performance/iteration-speed).')
diagnostics = [line for line in build_log.splitlines()
               if re.search(r'error:|warning:|ERROR:|WARNING:', line)]
assert all(line == cache_notice for line in diagnostics), diagnostics

hashes = {name: sha(H / 'private-source' / name) for name in sources}
assert hashes == {name: sha(S / name) for name in sources}, 'Private/staged source drift'
for pin in read('imports.json'):
    blob = subprocess.check_output(['git', '-C', str(W / pin['project']), 'rev-parse',
                                    pin['revision'] + ':' + pin['path']], text=True).strip()
    assert blob == pin['blob'], pin['path']
    data = subprocess.check_output(['git', '-C', str(W / pin['project']), 'cat-file', 'blob', blob])
    assert data == (H / 'original' / pin['path']).read_bytes(), pin['path']

shared = read('shared-inputs.json')
for name, item in shared.items():
    digest = sha(W / item['package'] / name)
    if 'sha256' in item:
        assert digest == item['sha256'], name
    item['sha256'] = digest
for handoff in ['onyx-swr-irq-handoff', 'onyx-audio-wsa-codecs-handoff', 'onyx-audio-small-handoff']:
    assert (Path('/tmp') / handoff / 'STATE.md').read_text().startswith('FROZEN'), handoff

with tempfile.TemporaryDirectory(prefix='wcd-codecs-replay-') as temporary:
    root = Path(temporary)
    for name in sources:
        dest = root / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(H / 'original' / name, dest)
    run = subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(H / 'patch')],
                         cwd=root, text=True, capture_output=True, check=True)
    assert not re.search(r'fuzz|offset|FAILED', run.stdout + run.stderr, re.I)
    assert hashes == {name: sha(root / name) for name in sources}
    (H / 'replay.log').write_text(run.stdout + run.stderr)

test = read('staged-validation.json')
assert test['status'] == 'PASS' and test['source_sha256'] == hashes
assert test['negative_controls'] == 43 and len(test['suites']) == 10
assert all(item['results'][0]['status'] == 'PASS' and
           all(case['status'] == 'REJECTED' for case in item['results'][1:]) for item in test['suites'])
assert test['verifier_sha256'] == sha(H / 'verify_wcd_codecs.py')
for name, digest in test['fixtures_sha256'].items():
    assert sha(H / name) == digest

check = subprocess.run([str(W / 'common/scripts/checkpatch.pl'), '--strict', '--no-tree',
                        '--no-signoff', str(H / 'patch')], text=True, capture_output=True)
check_log = check.stdout + check.stderr
style = re.findall(r'^(?:ERROR|WARNING|CHECK):.*$', check_log, re.M)
assert style == (['WARNING: struct regmap_irq_chip should normally be const'] * 3 +
                 ['WARNING: struct regmap_config should normally be const',
                  'WARNING: line length of 106 exceeds 100 columns']), check_log
(H / 'checkpatch-final.log').write_text(check_log)

# Hardware lookup tables must remain byte-identical; changed C files retain
# native ALSA controls, routes, widgets, DAIs and regmap IRQ data as tokens.
unchanged = {}
for name in sources:
    if name.endswith(('-tables.c', '-regmap.c', '-registers.h', '-reg-masks.h', '-reg-shifts.h')):
        assert sha(H / 'original' / name) == hashes[name], name
        unchanged[name] = hashes[name]
tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\s+|.', re.S)
compact = lambda text: ''.join(item.group() for item in tokens.finditer(text)
                              if not item.group().isspace() and not item.group().startswith(('/*', '//')))
def initializer(text, offset):
    brace = text.index('{', offset)
    depth = 0
    for item in tokens.finditer(text, brace):
        if item.group() == '{':
            depth += 1
        elif item.group() == '}':
            depth -= 1
            if not depth:
                return text[brace:item.end()]
    raise AssertionError('Unterminated native initializer')

tables = {}
pattern = re.compile(r'(?m)^(?:static\s+)?(?:const\s+)?struct '
                     r'(?:snd_kcontrol_new|snd_soc_dapm_widget|snd_soc_dapm_route|snd_soc_dai_driver|regmap_irq|reg_default)'
                     r'\s+(\w+)\s*\[[^\n]*?\]\s*=\s*\{')
for name in sources:
    if not name.endswith('.c'):
        continue
    original = (H / 'original' / name).read_text()
    final = (S / name).read_text()
    for match in pattern.finditer(original):
        symbol = match.group(1)
        matches = [item for item in pattern.finditer(final) if item.group(1) == symbol]
        assert len(matches) == 1, (name, symbol)
        old = compact(initializer(original, match.start()))
        new = compact(initializer(final, matches[0].start()))
        assert old == new, (name, symbol)
        tables[name + ':' + symbol] = hashlib.sha256(new.encode()).hexdigest()
assert len(tables) >= 25, len(tables)

spec = importlib.util.spec_from_file_location('module_verifier', P / 'verify_modules.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)
providers = [W / 'bazel-bin/common/kernel_aarch64/Module.symvers']
providers += [path for path in sorted((A / 'platform').glob('*_Module.symvers'))
              if path.name.removesuffix('_Module.symvers') not in {'swr_dlkm', 'swr_ctrl_dlkm'}]
for target in ['swr_dlkm', 'swr_ctrl_dlkm']:
    providers.append(W / 'bazel-bin/onyx_swr_irq' / target / (target + '_Module.symvers'))
providers += [B / item['target'] / (item['target'] + '_Module.symvers') for item in modules]
release = (W / 'bazel-bin/common/kernel_aarch64/include/config/kernel.release').read_text().strip()
results = verifier.verify([B / item['target'] / item['output'] for item in modules], providers, release)
modinfo = lambda field, path: subprocess.check_output(['modinfo', '-F', field, str(path)], text=True).strip().splitlines()
native_commands = read('native-commands.json')
evidence = {}
for module, result in zip(modules, results):
    target = module['target']
    reference = native[target]
    native_module = Path(reference['native_module'])
    assert sha(native_module) == reference['module_sha256']
    native_mod = H / 'native-evidence' / target / (target + '.mod')
    assert sha(native_mod) == reference['mod_sha256']
    native_units = [line.removeprefix('../sm8735-modules/').removesuffix('.o') + '.c'
                    for line in native_mod.read_text().splitlines()]
    assert native_units == reference['units']
    commands = {}
    for unit in native_units:
        command = H / 'native-evidence' / target / ('.' + Path(unit).stem + '.o.cmd')
        assert sha(command) == native_commands[unit]['sha256']
        commands[unit] = sha(command)
    assert sorted(result['depends'].split(',')) == sorted(reference['depends'].split(',')), target
    assert sorted(result['aliases']) == sorted(reference['aliases']), target
    module_path = B / target / module['output']
    for field, expected in [('parm', reference['params']), ('softdep', modinfo('softdep', native_module))]:
        assert sorted(modinfo(field, module_path)) == sorted(expected), (target, field)
    symvers = B / target / (target + '_Module.symvers')
    actual_exports = {line.split()[1]: line.split()[0] for line in symvers.read_text().splitlines()}
    assert sorted(actual_exports) == sorted(line.split()[1] for line in reference['exports']), target
    makefiles = B / (target + '_makefiles') / 'makefiles'
    kbuild = makefiles / 'Kbuild'
    metadata = makefiles / 'ddk_modinfo.c'
    generated = ['ddk_modinfo.o'] if re.search(r'^' + target + r'-y \+= ddk_modinfo\.o$', kbuild.read_text(), re.M) else []
    assert not generated or metadata.is_file()
    object_units = re.findall(r'^' + target + r'-y \+= (\S+)$', kbuild.read_text(), re.M)
    assert sorted(object_units) == sorted([name.removesuffix('.c') + '.o' for name in native_units] + generated)
    if generated:
        assert metadata.read_text() == ('#include <linux/compiler.h>\n'
            'static const char __UNIQUE_ID(built_with)[] __used __section(".modinfo") '
            '__aligned(1) = "built_with=DDK";\n')
    srcs = B / (target + '_makefiles') / 'srcs.json'
    actual_units = [str(Path(name).relative_to('onyx_wcd_codecs')) for row in json.loads(srcs.read_text())
                    for name in row.get('files', []) if name.endswith('.c')]
    assert sorted(actual_units) == sorted(native_units)
    flags = makefiles / (target + '.cflags_shipped')
    assert 'sunautoconf.h' in flags.read_text()
    assert not re.search(r'Wno-|KBUILD_MODPOST_WARN|KMI_SYMBOL_LIST_ADD_ONLY', flags.read_text())
    config = B / (target + '_config/out_dir/.config')
    for required in ['CONFIG_MODULES=y', 'CONFIG_MODVERSIONS=y', 'CONFIG_SND_SOC=y']:
        assert required in config.read_text().splitlines(), (target, required)
    destination = H / 'artifacts' / target
    destination.mkdir(parents=True, exist_ok=True)
    files = [module_path, symvers, kbuild, flags, config, srcs]
    if generated:
        files.append(metadata)
    for path in files:
        shutil.copyfile(path, destination / path.name)
    evidence[target] = {'native_units': native_units, 'native_module_sha256': sha(native_module),
                        'native_commands_sha256': commands, 'exports': actual_exports,
                        'generated_metadata': bool(generated),
                        'sha256': {path.name: sha(path) for path in files}}

shutil.copytree(H / 'private-source', H / 'frozen-source')
shutil.copyfile(args.build_log, H / 'build.log')
write('final-sha256.json', hashes)
write('final-shared-inputs.json', shared)
write('module-verification.json', results)
write('native-validation.json', {'units': 16, 'targets': evidence,
      'unchanged_hardware_files': unchanged, 'preserved_native_table_initializers': tables,
      'export_validation': 'Exact native exported-symbol sets; imported CRCs match 6.18 providers, not the native 6.6 ABI.'})
write('validation.json', {'status': 'PASS', 'kernel_release': release, 'source_sha256': hashes,
      'patch_sha256': sha(H / 'patch'), 'module_verification': results, 'shared_inputs': shared,
      'tests': test, 'codec_suites': 10, 'negative_controls': 43,
      'native_validation_sha256': sha(H / 'native-validation.json'),
      'compiler_sha256': sha(W / 'prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang'),
      'checkpatch': {'errors': 0, 'warnings': style, 'checks': 0},
      'build_cache_notices': diagnostics,
      'limits': ['Selected production functions run with host service mocks and ASan/UBSan.',
                 'No physical codec, kernel module load, regmap IRQ, DSP, phone boot or real audio validation.',
                 'Mutable per-instance regmap/IRQ configs intentionally trigger four const advisories; one aligned call is 106 columns.']})
(H / 'STATE.md').write_text('FROZEN WCD9378/WCD938x/WCD939x codec and SoundWire slave port. '
    '31 exact pinned source/header inputs, six strict module targets and 16 native units. '
    '10 production-function suites and 43 compiled rejecting controls pass, including card/SSR '
    'configuration lifetime, binding failures, slave I/O/removal and micbias regulator rollback. '
    'Native exported-symbol sets, hardware tables, module CRC providers and exact patch replay verified. '
    'Use frozen-source, final-sha256.json, PASS validation.json and existing build fragments for integration. '
    'No physical audio/module-load/phone claim; central source and old handoffs untouched.\n')
files = [path for path in H.iterdir() if path.is_file() and path.name != 'handoff-sha256.json']
files += [path for path in (H / 'tests').rglob('*') if path.is_file()]
write('handoff-sha256.json', {str(path.relative_to(H)): sha(path) for path in files})
print('Frozen WCD codecs:', len(modules), 'targets;', sum(item['imports_checked'] for item in results),
      'imported CRCs;', len(tables), 'native table initializers;', len(unchanged), 'exact hardware files;',
      '10 host suites and 43 compiled rejecting controls')
