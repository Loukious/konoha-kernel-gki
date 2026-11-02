from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
A = W / 'artifacts/20261031T232938953896Z'
S = W / 'onyx_audio_machine'
B = W / 'bazel-bin/onyx_audio_machine'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
read = lambda name: json.loads((H / name).read_text())
write = lambda name, value: (H / name).write_text(json.dumps(value, indent=2) + '\n')
parser = argparse.ArgumentParser()
parser.add_argument('--build-log', type=Path, required=True)
args = parser.parse_args()
assert not (H / 'frozen-source').exists(), 'Machine source is already frozen'
sources = read('sources.json')
provider_paths = read('provider-paths.json')
native = read('rom-evidence.json')
assert len(sources) == 7 and len(provider_paths) == 2
for pin in read('imports.json'):
    blob = subprocess.check_output(['git', '-C', str(W / pin['project']), 'rev-parse',
                                    pin['revision'] + ':' + pin['path']], text=True).strip()
    assert blob == pin['blob'], pin['path']
    original = subprocess.check_output(['git', '-C', str(W / pin['project']), 'cat-file', 'blob', blob])
    assert original == (H / 'original' / pin['path']).read_bytes()
for name, digest in read('provider-before-sha256.json').items():
    assert sha(H / 'provider-original' / name) == sha(W / 'onyx' / name) == digest
for name, digest in read('shared-headers.json').items():
    assert sha(W / 'onyx' / name) == digest
for handoff in ['onyx-swr-irq-handoff', 'onyx-audio-wsa-codecs-handoff', 'onyx-wcd-codecs-handoff']:
    assert (Path('/tmp') / handoff / 'STATE.md').read_text().startswith('FROZEN'), handoff
for name, item in read('final-provider-headers.json').items():
    assert sha(S / name) == sha(W / item['package'] / name) == item['sha256'], name
hashes = {name: sha(H / 'private-source' / name) for name in sources}
provider_hashes = {name: sha(H / 'provider-source' / name) for name in provider_paths}
assert hashes == {name: sha(S / name) for name in sources}
assert provider_hashes == {name: sha(S / name) for name in provider_paths}
for patch, original, names, expected in [('patch', 'original', sources, hashes),
                                          ('provider.patch', 'provider-original', provider_paths, provider_hashes)]:
    with tempfile.TemporaryDirectory(prefix='onyx-machine-replay-') as tmp:
        root = Path(tmp)
        for name in names:
            dest = root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(H / original / name, dest)
        run = subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(H / patch)],
                             cwd=root, capture_output=True, text=True, check=True)
        assert not re.search(r'fuzz|offset|FAILED', run.stdout + run.stderr, re.I)
        assert expected == {name: sha(root / name) for name in names}
        (H / (patch + '.replay.log')).write_text(run.stdout + run.stderr)
tests = {}
for name, controls in [('common', 7), ('cleanup', 7), ('smartpa', 6)]:
    report = read(name + '-validation.json')
    assert report['status'] == 'PASS' and len(report['negative_controls']) == controls
    assert all(item['compiled'] and item['rejected'] for item in report['negative_controls'])
    for path, digest in report['source_sha256'].items():
        assert sha(Path(path)) == digest
    fixture = {'common': 'common_lifetime.c', 'cleanup': 'card_cleanup.c', 'smartpa': 'smartpa_registry.c'}[name]
    assert sha(H / 'tests' / fixture) == report['fixture_sha256']
    tests[name] = report
style = {}
for name in ['patch', 'provider.patch']:
    run = subprocess.run([str(W / 'common/scripts/checkpatch.pl'), '--strict', '--no-tree',
                          '--no-signoff', str(H / name)], capture_output=True, text=True)
    log = run.stdout + run.stderr
    diagnostics = re.findall(r'^(?:ERROR|WARNING|CHECK):.*$', log, re.M)
    expected = (["WARNING: return sysfs_emit(...) formats should include a terminating newline",
                 "CHECK: Macro argument reuse 'source' - possible side-effects?"] if name == 'patch' else [])
    assert diagnostics == expected, log
    (H / (name + '.checkpatch.log')).write_text(log)
    style[name] = diagnostics
log = args.build_log.read_text()
assert 'Build completed successfully' in log
diagnostics = '\n'.join(line for line in log.splitlines() if not line.startswith(
    'WARNING: Build option --//build/kernel/kleaf:user_kmi_symbol_lists has changed, discarding analysis cache'))
assert not re.search(r'error:|warning:|ERROR:|WARNING:', diagnostics)
sys.path.insert(0, str(P))
from verify_modules import verify
replacement_targets = {'swr_dlkm', 'swr_ctrl_dlkm', 'snd_event_dlkm'}
providers = [W / 'bazel-bin/common/kernel_aarch64/Module.symvers']
providers += [path for path in sorted((A / 'platform').glob('*_Module.symvers'))
              if path.name.removesuffix('_Module.symvers') not in replacement_targets]
for package, handoff in [('onyx_wcd_codecs', 'onyx-wcd-codecs-handoff'),
                         ('onyx_audio_speakers', 'onyx-audio-wsa-codecs-handoff'),
                         ('onyx_swr_irq', 'onyx-swr-irq-handoff')]:
    for item in json.loads((Path('/tmp') / handoff / 'modules.json').read_text()):
        target = item['target']
        providers.append(W / 'bazel-bin' / package / target / (target + '_Module.symvers'))
providers += [B / name / (name + '_Module.symvers') for name in ['machine_dlkm', 'snd_event_dlkm']]
release = (W / 'bazel-bin/common/kernel_aarch64/include/config/kernel.release').read_text().strip()
modules = [B / name / (name + '.ko') for name in ['machine_dlkm', 'snd_event_dlkm']]
results = verify(modules, providers, release)
modinfo = lambda field, path: subprocess.check_output(['modinfo', '-F', field, str(path)], text=True).strip().splitlines()
exports = lambda path: {line.split()[1]: line.split()[0] for line in path.read_text().splitlines()}
result = results[0]
assert result['module'] == 'machine_dlkm.ko'
assert sha(Path(native['module_path'])) == native['module_sha256']
assert sorted(result['depends'].split(',')) == sorted(native['depends'].split(','))
assert sorted(result['aliases']) == sorted(native['aliases'])
assert modinfo('parm', modules[0]) == native['params']
assert modinfo('softdep', modules[0]) == [native['softdep']]
machine_exports = exports(B / 'machine_dlkm/machine_dlkm_Module.symvers')
assert sorted(machine_exports) == sorted([line.split()[1] for line in native['exports']] +
                                         ['register_smartpa_codec', 'unregister_smartpa_codec'])
old_exports = exports(A / 'platform/snd_event_dlkm_Module.symvers')
new_exports = exports(B / 'snd_event_dlkm/snd_event_dlkm_Module.symvers')
assert all(new_exports.get(name) == crc for name, crc in old_exports.items())
assert sorted(set(new_exports) - set(old_exports)) == ['snd_event_clients_free']
assert sorted(modinfo('depends', modules[1])) == sorted(modinfo('depends', A / 'platform/snd_event_dlkm.ko'))
for path, digest in native['native_evidence_sha256'].items():
    assert sha(Path(path)) == digest
    shutil.copyfile(path, H / 'native-evidence' / Path(path).name)
evidence = {}
for target, units in [('machine_dlkm', native['units']),
                      ('snd_event_dlkm', ['qcom/opensource/audio-kernel/soc/snd_event.c'])]:
    makefiles = B / (target + '_makefiles') / 'makefiles'
    kbuild = makefiles / 'Kbuild'
    metadata = makefiles / 'ddk_modinfo.c'
    generated = ['ddk_modinfo.o'] if re.search(r'^' + target + r'-y \+= ddk_modinfo\.o$', kbuild.read_text(), re.M) else []
    assert not generated or metadata.is_file()
    objects = re.findall(r'^' + target + r'-y \+= (.*)$', kbuild.read_text(), re.M)
    assert sorted(objects) == sorted([name.removesuffix('.c') + '.o' for name in units] + generated)
    if generated:
        assert metadata.read_text() == ('#include <linux/compiler.h>\n'
            'static const char __UNIQUE_ID(built_with)[] __used __section(".modinfo") '
            '__aligned(1) = "built_with=DDK";\n')
    srcs = json.loads((B / (target + '_makefiles/srcs.json')).read_text())
    actual = [str(Path(name).relative_to('onyx_audio_machine')) for row in srcs
              for name in row.get('files', []) if name.endswith('.c')]
    assert sorted(actual) == sorted(units)
    flags = makefiles / (target + '.cflags_shipped')
    assert 'sunautoconf.h' in flags.read_text()
    assert not re.search(r'Wno-|KBUILD_MODPOST_WARN|KMI_SYMBOL_LIST_ADD_ONLY', flags.read_text())
    config = B / (target + '_config/out_dir/.config')
    for required in ['CONFIG_MODULES=y', 'CONFIG_MODVERSIONS=y', 'CONFIG_SND_SOC=y']:
        assert required in config.read_text().splitlines()
    dest = H / 'artifacts' / target
    dest.mkdir(parents=True, exist_ok=True)
    inputs = [B / target / (target + '.ko'), B / target / (target + '_Module.symvers'), kbuild, flags, config]
    if generated:
        inputs.append(metadata)
    for path in inputs:
        shutil.copyfile(path, dest / path.name)
    evidence[target] = {'native_units': units, 'generated_metadata': bool(generated),
                        'sha256': {path.name: sha(path) for path in inputs}}
fragment = (H / 'BUILD.fragment').read_text()
fragment = fragment[:fragment.index('ddk_headers(\n    name = "audio_machine_event_headers",')]
fragment = fragment.replace(':audio_machine_event_headers', ':audio_foundation_headers')
fragment = fragment.replace('pending_codec_headers', 'audio_machine_dependency_headers')
for package in ['onyx', 'onyx_wcd_codecs', 'onyx_audio_speakers']:
    fragment = fragment.replace('//' + package + ':', ':')
(H / 'integration-BUILD.fragment').write_text(fragment)
shutil.copytree(H / 'private-source', H / 'frozen-source')
shutil.copytree(H / 'provider-source', H / 'frozen-provider-source')
shutil.copyfile(args.build_log, H / 'build.log')
write('final-sha256.json', hashes)
write('provider-final-sha256.json', provider_hashes)
write('module-verification.json', results)
write('validation.json', {'status': 'PASS', 'kernel_release': release, 'module_verification': results,
      'source_sha256': hashes, 'provider_source_sha256': provider_hashes,
      'provider_input_sha256': read('final-provider-headers.json'), 'native_evidence': evidence,
      'patch_sha256': sha(H / 'patch'), 'provider_patch_sha256': sha(H / 'provider.patch'),
      'tests': tests, 'host_cases': 118, 'negative_controls': 20, 'checkpatch': style,
      'preserved_snd_event_export_crcs': old_exports,
      'limits': ['Build and selected production-function host validation; no phone boot, DSP or physical audio test.',
                 'Native card_state has no newline; the fixed-array DAI append macro evaluates its array only outside ARRAY_SIZE.',
                 'Third-party amplifier consumers must unregister copied registry entries before device removal.']})
(H / 'STATE.md').write_text('FROZEN native SUN machine audio and separate SND event client-list cleanup. '
    'Seven fresh inputs, two existing provider edits, two exact native machine units. '
    '118 production-function host cases and 20 compiled rejecting controls pass. '
    'Final codec/IRQ providers, module CRCs, native metadata and exact patch replay verified. '
    'Root integrates provider.patch before patch; use integration-BUILD.fragment and retain the existing SND event target.\n')
files = [path for path in H.iterdir() if path.is_file() and path.name not in ['handoff-sha256.json']]
files += list((H / 'tests').rglob('*'))
write('handoff-sha256.json', {str(path.relative_to(H)): sha(path) for path in files if path.is_file()})
print('Frozen SUN machine audio:', sum(item['imports_checked'] for item in results), 'imported CRCs; 118 host cases and 20 controls')
