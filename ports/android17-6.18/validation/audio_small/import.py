"""Freeze the exact native inputs owned by the small audio handoff."""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
EXTERNAL = 'qcom/opensource/audio-kernel/asoc/codecs/wcd939x/wcd939x.h'


def freeze(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        assert path.read_bytes() == data, path
    else:
        path.write_bytes(data)


pins = {}
shared = set()
records = {}
for name in json.loads((H / 'summary.json').read_text()):
    d = json.loads((H / (name + '.json')).read_text())
    records[name] = d
    shared.update(d['existing_paths'])
    for pin in d['pins']:
        if pin['path'] == EXTERNAL:
            continue
        pins[pin['path']] = pin
    for item in d['objects']:
        receipt = Path(item['receipt'])
        freeze(H / 'native-receipts' / name / receipt.name, receipt.read_bytes())
    native_mod = Path(d['module']).with_suffix('.mod')
    freeze(H / 'native-receipts' / name / native_mod.name, native_mod.read_bytes())

for path, pin in sorted(pins.items()):
    blob = subprocess.check_output(['git', '-C', str(W / pin['project']),
                                    'cat-file', 'blob', pin['blob']])
    freeze(H / 'original' / path, blob)
    private = H / 'private-source' / path
    if not private.exists():
        private.parent.mkdir(parents=True, exist_ok=True)
        private.write_bytes(blob)

for name in ('Kconfig', 'platform_defconfig'):
    freeze(H / 'base-config' / name, (P / name).read_bytes())

(H / 'imports.json').write_text(json.dumps(list(pins.values()), indent=2) + '\n')
(H / 'sources.json').write_text(json.dumps(sorted(pins), indent=2) + '\n')
(H / 'shared-inputs-initial.json').write_text(json.dumps({
    path: hashlib.sha256((W / 'onyx' / path).read_bytes()).hexdigest()
    for path in sorted(shared)
}, indent=2) + '\n')
(H / 'modules.json').write_text(json.dumps([
    {'target': name, 'output': name + '.ko'} for name in records
], indent=2) + '\n')
for name in ('Kconfig.fragment', 'defconfig.fragment'):
    (H / name).write_text('# Native small-audio selections use the pinned sunautoconf.h.\n')
print(f'Imported {len(pins)} owned paths; WCD939x header remains codec-owned.')
