from pathlib import Path
import re

H = Path(__file__).resolve().parent
S = H / 'private-source'
log = (H / 'checkpatch-first.log').read_text()
locations = re.findall(r'CHECK: Alignment should match open parenthesis\n#\d+: FILE: ([^:]+):(\d+):', log)
files = {}
for name, line in locations:
    files.setdefault(name, []).append(int(line))
for name, lines in files.items():
    path = S / name
    source = path.read_text().splitlines(True)
    for number in sorted(lines, reverse=True):
        start = number - 2
        first = source[start].expandtabs(8)
        assert '(' in first, (name, number)
        column = first.index('(') + 1
        indent = '\t' * (column // 8) + ' ' * (column % 8)
        depth = first.count('(') - first.count(')')
        index = start + 1
        while depth:
            assert index < len(source) and source[index].strip() != '{', (name, number)
            source[index] = indent + source[index].lstrip()
            depth += source[index].count('(') - source[index].count(')')
            index += 1
    path.write_text(''.join(source))

base = S / 'qcom/opensource/audio-kernel/asoc/codecs'
for variant in ['wcd9378', 'wcd938x', 'wcd939x']:
    path = base / variant / (variant + '-mbhc.c')
    source = path.read_text()
    old = '\tif (!ret)\n\t\tpriv->mbhc_config = config;\n\telse if (!priv->mbhc_config) {'
    assert source.count(old) == 1
    source = source.replace(old, '\tif (!ret) {\n\t\tpriv->mbhc_config = config;\n\t} else if (!priv->mbhc_config) {')
    path.write_text(source)
    path = base / variant / 'internal.h'
    source = path.read_text().replace('\tstruct mutex mbhc_lock;', '\t/* Serializes card-owned detection configuration and MBHC/SSR lifetime. */\n\tstruct mutex mbhc_lock;')
    source = source.replace('\tstruct regmap_irq_chip irq_chip_config;', '\t/* Per-instance copy owns its codec-specific irq_drv_data. */\n\tstruct regmap_irq_chip irq_chip_config;')
    source = source.replace('\tstruct regmap_config regmap_config;', '\t/* The identified silicon revision selects readable_reg at bind. */\n\tstruct regmap_config regmap_config;')
    path.write_text(source)
    path = base / variant / (variant + '-slave.c')
    path.write_text(path.read_text().replace('#ifdef CONFIG_DEBUG_FS\n\n\n', '#ifdef CONFIG_DEBUG_FS\n\n'))
path = base / 'wcd939x/wcd939x.c'
source = path.read_text()
old = '''\tif (id1 == 0)
\t\tpriv->version = (status1 & 0x3) ? WCD939X_VERSION_1_1 : WCD939X_VERSION_1_0;
\telse if (id1 == 1)
\t\tpriv->version = WCD939X_VERSION_2_0;
\telse {'''
assert source.count(old) == 1
source = source.replace(old, '''\tif (id1 == 0) {
\t\tpriv->version = (status1 & 0x3) ? WCD939X_VERSION_1_1 : WCD939X_VERSION_1_0;
\t} else if (id1 == 1) {
\t\tpriv->version = WCD939X_VERSION_2_0;
\t} else {''')
path.write_text(source)

path = H / 'stage.py'
source = path.read_text()
old = "'\\n\\n---\\n\\n'"
assert source.count(old) == 1
source = source.replace(old, "'\\n\\nRetain exact native codec units and hardware tables while checking resource\\nacquisition, draining callbacks before release, and preserving card-owned\\nheadset configuration across subsystem restart only for a live card.\\n\\n---\\n\\n'")
path.write_text(source)
print('Aligned changed declarations/calls, documented mutable per-device state, and balanced new branches')
