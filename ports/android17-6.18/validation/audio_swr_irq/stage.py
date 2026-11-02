from pathlib import Path
import difflib
import hashlib
import json
import shutil
H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
assert not (H / 'frozen-source').exists(), 'Frozen handoff may not be restaged'
D = W / 'onyx_swr_irq'
D.mkdir(exist_ok=True)
paths = json.loads((H / 'sources.json').read_text())
for path in paths:
    dest = D / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(H / 'private-source' / path, dest)
for path, entry in json.loads((H / 'shared-inputs.json').read_text()).items():
    source = W / entry['package'] / path
    assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256'], path
    dest = D / path
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)
(D / 'BUILD.bazel').write_text('load("//build/kernel/kleaf:kernel.bzl", "ddk_headers", "ddk_module")\n\n' + (H / 'BUILD.fragment').read_text())
for name in ['Kconfig', 'platform_defconfig']:
    shutil.copyfile(P / name, D / name)
(H / 'patch').write_text('Subject: ' + 'Drain borrowed SoundWire IRQ domains before consumer teardown' + '\n\nRetain slave registrations and serialize threaded dispatch with checked\ndomain withdrawal. Preserve the controller pending loop and public device\nlayout.\n\n---\n\n' + ''.join(
    ''.join(difflib.unified_diff((H / 'original' / p).read_text().splitlines(True),
                                (H / 'private-source' / p).read_text().splitlines(True),
                                fromfile='a/' + p, tofile='b/' + p)) for p in paths))
print('Staged', D.name, len(paths), 'inputs')
