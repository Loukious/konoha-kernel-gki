"""Freeze two integrated provider inputs; never touch the central recipe."""
import hashlib
import json
from pathlib import Path

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
P = Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18')
paths = ['qcom/opensource/mm-drivers/msm_ext_display/src/msm_ext_display.c',
         'qcom/opensource/mm-drivers/msm_ext_display/include/msm_ext_display.h']
for path in paths:
    raw = (W / 'onyx' / path).read_bytes()
    for tree in ('original', 'private-source'):
        dest = H / tree / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            assert dest.read_bytes() == raw, dest
        else:
            dest.write_bytes(raw)
(H / 'sources.json').write_text(json.dumps(paths, indent=2) + '\n')
(H / 'original-sha256.json').write_text(json.dumps({
    p: hashlib.sha256((H / 'original' / p).read_bytes()).hexdigest() for p in paths
}, indent=2) + '\n')
central = json.loads((P / 'imports.json').read_text())
(H / 'source-pins.json').write_text(json.dumps([
    pin for pin in central if pin['path'] in paths
], indent=2) + '\n')
for name in ('Kconfig', 'platform_defconfig'):
    out = H / 'base-config' / name
    out.parent.mkdir(exist_ok=True)
    out.write_bytes((P / name).read_bytes())
(H / 'STATE.md').write_text('ACTIVE: provider audio-ops withdrawal only; two private source/header inputs. No central edits.\n')
