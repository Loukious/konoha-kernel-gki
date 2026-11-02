from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
A = W / 'artifacts/20261031T232938953896Z'
S = W / 'onyx_mac80211'
B = W / 'bazel-bin/onyx_mac80211'
K = W / 'bazel-bin/common/kernel_aarch64'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
read = lambda p: json.loads(p.read_text())
write = lambda name, value: (H / name).write_text(json.dumps(value, indent=2) + '\n')
parser = argparse.ArgumentParser()
parser.add_argument('--build-log', type=Path, required=True)
args = parser.parse_args()
assert not (H / 'frozen-source').exists(), 'Already frozen'
pins = read(H / 'imports.json')
sources = read(H / 'sources.json')
units = read(H / 'units.json')
assert len(pins) == len(sources) == 74 and len(units) == 45
assert {item['path'] for item in pins} == set(sources)
for pin in pins:
    repo = W / pin['project']
    blob = subprocess.check_output(['git', '-C', str(repo), 'rev-parse',
                                    pin['revision'] + ':' + pin['path']], text=True).strip()
    assert blob == pin['blob']
    data = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', blob])
    for tree in [H / 'original', H / 'private-source', S]:
        assert (tree / pin['path']).read_bytes() == data, (tree, pin['path'])
hashes = {name: sha(S / name) for name in sources}
config = B / 'mac80211_config/out_dir/.config'
conf = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', config.read_text(), re.M))
selection = read(H / 'configuration-evidence.json')['feature_selection']
for key, value in selection.items():
    assert conf.get(key, 'n') == value, (key, conf.get(key), value)
for key in ['CONFIG_MODULES', 'CONFIG_MODVERSIONS', 'CONFIG_TRIM_UNUSED_KSYMS']:
    assert conf[key] == 'y', key
assert conf['CONFIG_MAC80211_RC_DEFAULT'] == '"minstrel_ht"'
makefiles = B / 'mac80211_makefiles/makefiles'
kbuild = makefiles / 'Kbuild'
objects = re.findall(r'^mac80211-y \+= (.*)$', kbuild.read_text(), re.M)
assert sorted(objects) == sorted([name.removesuffix('.c') + '.o' for name in units] + ['ddk_modinfo.o'])
metadata = makefiles / 'ddk_modinfo.c'
assert metadata.read_text() == ('#include <linux/compiler.h>\n'
    'static const char __UNIQUE_ID(built_with)[] __used __section(".modinfo") '
    '__aligned(1) = "built_with=DDK";\n')
flags = makefiles / 'mac80211.cflags_shipped'
assert not re.search(r'Wno-|KBUILD_MODPOST_WARN', flags.read_text())
assert '-DDEBUG' in flags.read_text()
exports = lambda path: {line.split()[1]: line.split() for line in path.read_text().splitlines()}
old = exports(A / 'gki/kernel_aarch64_Module.symvers')
new = exports(K / 'Module.symvers')
assert all(new.get(name) == fields for name, fields in old.items()), 'Original GKI exports changed'
assert set(new) - set(old) == {'led_trigger_blink', 'led_trigger_blink_oneshot'}, set(new) - set(old)
sys.path.insert(0, str(P))
from verify_modules import verify
release = (K / 'include/config/kernel.release').read_text().strip()
module = B / 'mac80211/mac80211.ko'
symvers = B / 'mac80211/mac80211_Module.symvers'
cfg_symvers = W / 'bazel-bin/onyx/cfg80211/cfg80211_Module.symvers'
results = verify([module], [K / 'Module.symvers', cfg_symvers, symvers], release)
assert set(results[0]['depends'].split(',')) == {'cfg80211', 'libarc4'}
log = args.build_log.read_text()
assert 'Build completed successfully' in log
diagnostics = re.findall(r'^(?:WARNING:|ERROR:|.*\berror:|.*\bwarning:).*$', log, re.M)
assert all(line.startswith('WARNING: Build option --//build/kernel/kleaf:user_kmi_symbol_lists has changed,')
           for line in diagnostics), diagnostics
artifacts = H / 'artifacts'
artifacts.mkdir(exist_ok=True)
for label, path in [('mac80211.ko', module), ('mac80211_Module.symvers', symvers),
                    ('gki_Module.symvers', K / 'Module.symvers'),
                    ('cfg80211_Module.symvers', cfg_symvers), ('module.config', config),
                    ('kernel.config', W / 'bazel-bin/common/kernel_aarch64_config/out_dir/.config'), ('Kbuild', kbuild),
                    ('ddk_modinfo.c', metadata), ('mac80211.cflags_shipped', flags)]:
    shutil.copyfile(path, artifacts / label)
shutil.copytree(H / 'private-source', H / 'frozen-source')
shutil.copyfile(args.build_log, H / 'build.log')
write('final-sha256.json', hashes)
write('validation.json', {
    'status': 'PASS', 'module_verification': results, 'source_sha256': hashes,
    'unmodified_upstream_inputs': len(pins), 'native_units': units,
    'feature_selection': selection, 'kernel_release': release,
    'original_gki_exports_preserved': len(old),
    'retained_gki_exports': {name: new[name] for name in sorted(set(new) - set(old))},
    'bazel_flag': '--//build/kernel/kleaf:user_kmi_symbol_lists=//onyx_mac80211:kmi-symbols',
    'kmi_checks': 'Original symbol lists, strict mode, MODVERSIONS and trimming remain enabled; two existing upstream LED exports are additionally retained.',
    'artifacts_sha256': {path.name: sha(path) for path in artifacts.iterdir()},
    'limits': 'No source behavior changes or new host tests; strict build, config, native-unit and imported-CRC validation only. No physical adapter or RF test.'})
(H / 'STATE.md').write_text('FROZEN upstream MAC80211: 74 unchanged pinned inputs, 45 native units. '
    'Preserves booted NetHunter Minstrel and LED settings. Requires the supplied KMI symbol-list '
    'extension retaining two existing upstream LED exports; original GKI exports and strict checks preserved. '
    'No source patch. Root integration must carry the symbol-list flag into all matching builds.\n')
write('handoff-sha256.json', {path.name: sha(path) for path in H.iterdir()
                            if path.is_file() and path.name != 'handoff-sha256.json'})
print('Frozen MAC80211:', results[0]['imports_checked'], 'CRCs;', len(old), 'original GKI exports preserved')
