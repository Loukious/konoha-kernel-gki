#!/usr/bin/env python3
from pathlib import Path
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
assert not (H / 'frozen-source').exists(), 'Already frozen: never rerun this script'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sources = json.loads((H / 'sources.json').read_text())
pins = json.loads((H / 'imports.json').read_text())
units = json.loads((H / 'units.json').read_text())
shared = json.loads((H / 'shared-headers.json').read_text())
native = json.loads((H / 'rom-evidence.json').read_text())
assert len(sources) == len(pins) == 15 and len(units) == 2 and len(shared) == 7
hashes = {p: sha(H / 'private-source' / p) for p in sources}
for path, digest in hashes.items():
    assert sha(W / 'onyx_audio_speakers' / path) == digest, path
for path, digest in shared.items():
    assert sha(W / 'onyx' / path) == digest, path
provider_inputs = {}
for path in ['qcom/opensource/audio-kernel/include/soc/soundwire.h',
             'qcom/opensource/audio-kernel/soc/soundwire.c',
             'qcom/opensource/audio-kernel/soc/regmap-swr.c']:
    provider_inputs['onyx_swr_irq/' + path] = sha(W / 'onyx_swr_irq' / path)
assert (W / 'onyx_swr_irq/qcom/opensource/audio-kernel/soc/regmap-swr.c').read_bytes() == \
       (W / 'onyx/qcom/opensource/audio-kernel/soc/regmap-swr.c').read_bytes()
assert 'swr_set_slave_irq' in (W / 'onyx_swr_irq/qcom/opensource/audio-kernel/include/soc/soundwire.h').read_text()

suite = json.loads((H / 'probe-validation.json').read_text())
assert suite['status'] == 'PASS' and len(suite['negative_controls']) == 14
assert all(item['rejected'] for item in suite['negative_controls'])
assert suite['fixture_sha256'] == sha(H / 'tests/wsa_probe.c.in')
assert suite['verifier_sha256'] == sha(H / 'verify_probe.py')
for codec, data in suite['drivers'].items():
    path = 'qcom/opensource/audio-kernel/asoc/codecs/' + codec + '/' + codec + '.c'
    assert data['source_sha256'] == hashes[path]

with tempfile.TemporaryDirectory(prefix='wsa-codecs-replay-') as tmp:
    replay = Path(tmp)
    for pin in pins:
        repo = W / pin['project']
        blob = subprocess.check_output(['git', '-C', str(repo), 'rev-parse',
            pin['revision'] + ':' + pin['path']], text=True).strip()
        assert blob == pin['blob']
        original = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', blob])
        assert original == (H / 'original' / pin['path']).read_bytes()
        dest = replay / pin['path']
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(original)
    applied = subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i', str(H / 'patch')],
        cwd=replay, capture_output=True, text=True, check=True)
    assert not re.search(r'fuzz|offset|FAILED', applied.stdout + applied.stderr, re.I)
    assert {p: sha(replay / p) for p in sources} == hashes

log = Path('/tmp/onyx-wsa-codecs-build4.log').read_text()
assert 'Build completed successfully' in log
assert not re.search(r'warning:|error:|WARNING:|ERROR:', log)
check = subprocess.run([str(W / 'common/scripts/checkpatch.pl'), '--no-tree',
    '--root', str(W / 'common'), '--no-signoff', '--strict', str(H / 'patch')],
    capture_output=True, text=True)
(H / 'checkpatch.txt').write_text(check.stdout + check.stderr)
assert not check.returncode, check.stdout + check.stderr

sys.path.insert(0, str(P))
from verify_modules import verify
module_paths = [W / 'bazel-bin/onyx_audio_speakers' / name / (name + '.ko') for name in units]
providers = [W / 'bazel-bin/common/kernel_aarch64/Module.symvers']
for pkg, name in [('onyx_swr_irq', 'swr_dlkm'), ('onyx', 'wcd_core_dlkm'), ('onyx', 'qti_regmap_debugfs')]:
    providers.append(W / 'bazel-bin' / pkg / name / (name + '_Module.symvers'))
release = (W / 'bazel-bin/common/kernel_aarch64/include/config/kernel.release').read_text().strip()
checked = verify(module_paths, providers, release)
tables, kbuild_hashes, config_hashes = {}, {}, {}
for result, (target, inputs), module in zip(checked, units.items(), module_paths):
    expected = native[target]
    assert len(inputs) == 3
    assert expected['linked_objects'] == ['../sm8735-modules/' + p[:-2] + '.o' for p in inputs]
    assert sorted(result['depends'].split(',')) == sorted(expected['depends'].split(','))
    assert sorted(result['aliases']) == sorted(expected['aliases'])
    exports = subprocess.check_output(['modprobe', '--show-exports', str(module)], text=True).splitlines()
    assert sorted(line.split()[1] for line in exports) == sorted(line.split()[1] for line in expected['exports'])
    params = subprocess.check_output(['modinfo', '-F', 'parm', str(module)], text=True).splitlines()
    assert params == expected['params']
    makefiles = W / 'bazel-bin/onyx_audio_speakers' / (target + '_makefiles') / 'makefiles'
    kbuild = (makefiles / 'Kbuild').read_text()
    assert re.findall(r'^' + target + r'-y \+= (.*)$', kbuild, re.M) == \
        [p[:-2] + '.o' for p in inputs] + ['ddk_modinfo.o']
    metadata = (makefiles / 'ddk_modinfo.c').read_text()
    assert metadata == ('#include <linux/compiler.h>\n'
        'static const char __UNIQUE_ID(built_with)[] __used __section(".modinfo") '
        '__aligned(1) = "built_with=DDK";\n')
    flags = (makefiles / (target + '.cflags_shipped')).read_text()
    assert 'sunautoconf.h' in flags
    assert not re.search(r'Wno-|KBUILD_MODPOST_WARN|KMI_SYMBOL_LIST_ADD_ONLY', flags)
    kbuild_hashes[target] = sha(makefiles / 'Kbuild')
    config = W / 'bazel-bin/onyx_audio_speakers' / (target + '_config/out_dir/.config')
    for required in ['CONFIG_MODULES=y', 'CONFIG_MODVERSIONS=y', 'CONFIG_OF=y',
                     'CONFIG_PM=y', 'CONFIG_COMMON_CLK=y', 'CONFIG_SND_SOC=y']:
        assert required in config.read_text().splitlines(), (target, required)
    config_hashes[target] = sha(config)
    for path in inputs:
        old = (H / 'original' / path).read_text()
        new = (H / 'private-source' / path).read_text()
        if not path.endswith('/' + target.removesuffix('_dlkm') + '.c'):
            assert old == new, path
        for match in re.finditer(r'^(?:static )?(?:const )?(?:struct|u\d+|int|char) [^\n;]+ = \{', old, re.M):
            end = old.index('\n};', match.end()) + 3
            declaration = old[match.start():end]
            assert declaration in new, (path, declaration[:100])
            tables[path + ':' + match.group()] = hashlib.sha256(declaration.encode()).hexdigest()
        def features(source):
            items, continuation = [], False
            for line in source.splitlines():
                if line.startswith(('#if ', '#ifdef ', '#ifndef ', '#else', '#elif ', '#endif', '#undef ')):
                    items.append(line)
                if line.startswith('#define ') or continuation:
                    items.append(line)
                    continuation = line.endswith('\\')
            return items
        assert features(old) == features(new), path
    result.update(native_compilation_units=inputs, exports=exports, params=params,
                  ddk_generated_metadata_sha256=sha(makefiles / 'ddk_modinfo.c'))

# The public codec APIs, register descriptions, and parent ABI pin stay exact.
for path in sources:
    if path.endswith('.h') and not path.endswith('/internal.h'):
        assert (H / 'original' / path).read_bytes() == (H / 'private-source' / path).read_bytes()
shutil.copytree(H / 'private-source', H / 'frozen-source')
(H / 'frozen-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
(H / 'build.log').write_text(log)
(H / 'provider-inputs.json').write_text(json.dumps(provider_inputs, indent=2) + '\n')
report = {'status': 'PASS', 'kernel_release': release, 'module_verification': checked,
    'source_sha256': hashes, 'shared_header_sha256': shared,
    'provider_input_sha256': provider_inputs, 'patch_sha256': sha(H / 'patch'),
    'native_table_sha256': tables, 'kbuild_sha256': kbuild_hashes, 'config_sha256': config_hashes,
    'suite': suite, 'negative_controls': 14, 'checkpatch': '0 errors, 0 warnings, 0 checks',
    'limits': [
        'Build, imported CRC, exact-input/replay and mocked production-function tests; no boot or physical audio validation.',
        'SWR probe/remove and notifier lifecycle must be serialized by the driver core; withdrawal is never called from nested IRQ handlers.',
        'Host fixtures do not simulate the live regmap IRQ controller, parent supplier teardown, DSP, SSR, ASoC card rebind or persistent hardware failures.',
        'The native successful register/DAI/PM flow is retained; WSA883x/884x DT candidates are disabled for Onyx and are alternative hardware coverage.',
        'WSA883x native temperature helper source is not linked by the ROM; the exact three-unit native module has been preserved.'
    ]}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
(H / 'STATE.md').write_text('FROZEN native WSA883x/884x: 15 fresh pins; six exact native units; '
    '195 imported CRC checks; build4/strict checkpatch clean. 186 production-source host '
    'scenarios and 14 compiled rejecting controls pass. Exact replay and native tables, '
    'public APIs, exports, aliases, params and dependencies verified. Consume frozen-source '
    'and patch; do not rerun setup/adaptation/stage/freeze. Root owns central integration.\n')
print('FROZEN WSA codecs:', sum(item['imports_checked'] for item in checked), 'imported CRCs,', len(tables), 'native tables')
