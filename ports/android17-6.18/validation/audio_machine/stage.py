from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
S = W / 'onyx_audio_machine'
assert not (H / 'frozen-source').exists(), 'Machine source is already frozen'
S.mkdir(exist_ok=True)
subprocess.run([sys.executable, str(H / 'write_patches.py')], check=True)
providers = {}
for name in json.loads((H / 'shared-headers.json').read_text()):
    dest = S / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    package = 'onyx_swr_irq' if name.endswith('/soc/soundwire.h') else 'onyx'
    shutil.copyfile(W / package / name, dest)
    providers[name] = {'package': package}
for name in json.loads((H / 'pending-provider-headers.json').read_text()):
    owner = 'onyx_wcd_codecs' if '/wcd' in name else 'onyx_audio_speakers'
    src = W / owner / name
    assert src.is_file(), 'Missing provider header: ' + str(src)
    shutil.copyfile(src, H / 'external-headers' / name)
    providers[name] = {'package': owner}
for tree in ['private-source', 'external-headers', 'provider-source']:
    for src in sorted((H / tree).rglob('*')):
        if src.is_file():
            dest = S / src.relative_to(H / tree)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dest)
for name in json.loads((H / 'provider-paths.json').read_text()):
    providers[name] = {'package': 'onyx_audio_machine'}
for name, item in providers.items():
    item['sha256'] = hashlib.sha256((S / name).read_bytes()).hexdigest()
(H / 'final-provider-headers.json').write_text(json.dumps(providers, indent=2) + '\n')
(S / 'BUILD.bazel').write_text('load("//build/kernel/kleaf:kernel.bzl", "ddk_headers", "ddk_module")\n\n' + (H / 'BUILD.fragment').read_text())
for name, fragment in [('Kconfig', 'Kconfig.fragment'), ('platform_defconfig', 'defconfig.fragment')]:
    (S / name).write_text((P / name).read_text() + (H / fragment).read_text())
