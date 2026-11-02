from pathlib import Path
import difflib
import json
import shutil
H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
assert not (H / 'frozen-source').exists(), 'Frozen handoff may not be restaged'
D = W / 'onyx_wcd_codecs'
D.mkdir(exist_ok=True)
paths = json.loads((H / 'sources.json').read_text())
for path in paths:
    dest = D / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(H / 'private-source' / path, dest)
(D / 'BUILD.bazel').write_text('load("//build/kernel/kleaf:kernel.bzl", "ddk_headers", "ddk_module")\n\n' + (H / 'BUILD.fragment').read_text())
for name in ['Kconfig', 'platform_defconfig']:
    shutil.copyfile(P / name, D / name)
(H / 'patch').write_text('Subject: ' + 'Port native WCD93xx codecs and slaves to 6.18' + '\n\nRetain exact native codec units and hardware tables while checking resource\nacquisition, draining callbacks before release, and preserving card-owned\nheadset configuration across subsystem restart only for a live card.\n\n---\n\n' + ''.join(
    ''.join(difflib.unified_diff((H / 'original' / p).read_text().splitlines(True),
                                (H / 'private-source' / p).read_text().splitlines(True),
                                fromfile='a/' + p, tofile='b/' + p)) for p in paths))
print('Staged', D.name, len(paths), 'inputs')
