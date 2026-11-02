#!/usr/bin/env python3
"""Verify two DT properties are the only change and characterize pinned OF translation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
RECIPE = HERE.parent.parent
sys.path.insert(0, str(RECIPE))
from build_devicetrees import read_fdt, digest, inspect_tree

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path)
parser.add_argument('artifact', type=Path)
args = parser.parse_args()
previous = args.workspace / 'artifacts/20261101T201353889068Z'
cvp_path = '/soc/spf_core_platform'
report = {'status': 'PASS', 'artifact': str(args.artifact), 'boards': {}}
source = (args.workspace / 'common/drivers/of/address.c').read_text()
def extract(name):
    match = re.search(r'^(?:static )?[^\n]*\b' + name + r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    end = match.end(); depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[match.start():end] + '\n'
helper = extract('of_bus_default_match') + '\n' + extract('of_translate_dma_region')
(HERE/'helper.inc').write_text(helper)
inventory_path = args.workspace / 'packaging/module-inventory-20261101-cvp-cells.json'
inventory = json.loads(inventory_path.read_text())
for board in ['tuna.dtb', 'tuna7.dtb']:
    before = read_fdt(previous / 'devicetrees/checked' / board)
    path = args.artifact / 'devicetrees/checked' / board
    after = read_fdt(path)
    assert set(before) == set(after)
    assert '#address-cells' not in before[cvp_path] and '#size-cells' not in before[cvp_path]
    expected = {k: dict(v) for k, v in before.items()}
    expected[cvp_path].update({'#address-cells': struct.pack('>I', 1), '#size-cells': struct.pack('>I', 1)})
    assert expected == after, board
    assert inspect_tree(previous / 'devicetrees/checked' / board)['active_compatible_nodes'] == inspect_tree(path)['active_compatible_nodes']
    reservation = after[cvp_path + '/qcom,msm-audio-ion/audio_cnss_resv_region']['iommu-addresses']
    cells = struct.unpack('>' + 'I'*(len(reservation)//4), reservation)
    assert len(cells) == 6
    assert not any('dma-ranges' in after[parent] for parent in ['/', '/soc', cvp_path])
    fixtures = 'static const struct { __be32 cells[4]; uint64_t start, length; } fixtures[] = {\n'
    for i in range(0, len(cells), 3):
        phandle, start, length = cells[i:i+3]
        target = next(node for node, props in after.items() if props.get('phandle') == struct.pack('>I', phandle))
        assert target.startswith(cvp_path + '/')
        fixtures += '{{0x%08xU, 0x%08xU, 0U, 0U}, 0x%xULL, 0x%xULL},\n' % (int.from_bytes(struct.pack('>I',start),'little'), int.from_bytes(struct.pack('>I',length),'little'), start, length)
    fixtures += '};\nstatic int fixture_address_cells = 1, fixture_size_cells = 1;\n'
    outputs = {}
    with tempfile.TemporaryDirectory(prefix='cvp-of-check-') as temp:
        temp = Path(temp)
        (temp/'translator_test.c').write_bytes((HERE/'translator_test.c').read_bytes())
        (temp/'helper.inc').write_text(helper)
        for name, code in [('exact', fixtures), ('wrong_address_cells', fixtures.replace('fixture_address_cells = 1', 'fixture_address_cells = 2')),
                           ('wrong_size_cells', fixtures.replace('fixture_size_cells = 1', 'fixture_size_cells = 2')),
                           ('missing_address_cells', fixtures.replace('fixture_address_cells = 1', 'fixture_address_cells = -1'))]:
            (temp/'fixtures.inc').write_text(code)
            binary = temp/name
            compiler = args.workspace/'prebuilts/clang/host/linux-x86/clang-r596125/bin/clang'
            subprocess.run([str(compiler), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            str(temp/'translator_test.c'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            assert (result.returncode == 0) == (name == 'exact'), (name, result.stderr)
            outputs[name] = {'exit_code': result.returncode, 'result': result.stdout.strip()}
    phandles = {struct.unpack('>I', props['phandle'])[0]: node
                for node, props in after.items() if 'phandle' in props}
    reservations = []
    for node, props in after.items():
        if 'iommu-addresses' not in props:
            continue
        raw = props['iommu-addresses']
        values = struct.unpack('>'+'I'*(len(raw)//4), raw)
        pos = 0
        while pos < len(values):
            target = phandles[values[pos]]
            parent = target.rsplit('/', 1)[0] or '/'
            assert '#address-cells' in after[parent], (node, target)
            assert '#size-cells' in after[parent], (node, target)
            na = struct.unpack('>I', after[parent]['#address-cells'])[0]
            ns = struct.unpack('>I', after[parent]['#size-cells'])[0]
            assert 0 < na <= 2 and 0 < ns <= 2
            end = pos + 1 + na + ns
            assert end <= len(values), node
            assert any(values[pos+1+na:end]), (node, target, 'zero reservation')
            reservations.append({'node':node, 'target':target, 'parent':parent,
                                 'address_cells':na, 'size_cells':ns,
                                 'tuple':list(values[pos:end])})
            pos = end
    report.setdefault('all_reservation_parent_audits', {})[board] = reservations
    report['boards'][board] = {'only_changes': [cvp_path + '/#address-cells=1', cvp_path+'/#size-cells=1'],
        'native_iommu_address_cells': list(cells), 'translator_checks': outputs, 'sha256': digest(path)}
    assert inventory['boards'][board]['sha256'] == digest(previous/'devicetrees/checked'/board)
    inventory['boards'][board]['sha256'] = digest(path)
    print('PASS:', board, 'two metadata properties only; reservations unchanged; three negative controls rejected')
for category in ['gki', 'platform']:
    for path in (previous/category).iterdir():
        if path.is_file(): assert digest(path) == digest(args.artifact/category/path.name), path.name
assert digest(previous/'devicetrees/dtbo.img') == digest(args.artifact/'devicetrees/dtbo.img')
inventory.update(basis=str(args.artifact), compatibility_basis=str(inventory_path),
                 compatibility_validation=str(HERE/'validation.json'))
new_inventory = args.workspace/'packaging/module-inventory-20261101-audio-cells.json'
assert not new_inventory.exists()
new_inventory.write_text(json.dumps(inventory,indent=2)+'\n')
report['module_inventory'] = str(new_inventory)
report['gki_and_platform_bytes_unchanged'] = True
report['limits'] = 'Exact pinned OF region helper and bus match with mocked identity DMA translation; full FDT property comparison. Hardware boot untested.'
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: every kernel/module file and DTBO image remains identical')
