#!/usr/bin/env python3
"""Audit the USB provider correction against the pinned ICC graph and final DTBs."""
import argparse
from collections import deque
import json
from pathlib import Path
import re
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent))
from build_devicetrees import read_fdt, digest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path)
parser.add_argument('artifact', type=Path)
args = parser.parse_args()
previous = args.workspace/'artifacts/20261101T203629038944Z'
driver = args.workspace/'onyx/drivers/interconnect/qcom/tuna.c'
header = args.workspace/'onyx/include/dt-bindings/interconnect/qcom,tuna.h'
source = driver.read_text()
ids = {name: int(value) for name, value in re.findall(r'^#define\s+(\w+)\s+(\d+)\s*$', header.read_text(), re.M)}
graph = {}
names = {}
for name, body in re.findall(r'static struct qcom_icc_node (\w+) = \{(.*?)\n\};', source, re.S):
    node_id = ids[re.search(r'\.id\s*=\s*(\w+)', body)[1]]
    links = re.search(r'\.links\s*=\s*\{([^}]*)\}', body)
    graph[node_id] = [ids[x] for x in re.findall(r'\b(?:MASTER|SLAVE)_\w+', links[1])] if links else []
    assert len(graph[node_id]) == int(re.search(r'\.num_links\s*=\s*(\d+)', body)[1])
    names[node_id] = name
arrays = {name: {ids[key]: value for key, value in re.findall(r'\[(\w+)\]\s*=\s*&(\w+)', body)}
          for name, body in re.findall(r'static struct qcom_icc_node \*(\w+)\[\] = \{(.*?)\n\};', source, re.S)}
descs = {name: re.search(r'\.nodes\s*=\s*(\w+)', body)[1]
         for name, body in re.findall(r'static struct qcom_icc_desc (\w+) = \{(.*?)\n\};', source, re.S)}
providers = {compat: arrays[descs[desc]] for compat, desc in
             re.findall(r'\.compatible\s*=\s*"([^"]+)"\s*,\s*\.data\s*=\s*&(\w+)', source)}
assert providers and graph

def words(raw):
    assert len(raw) % 4 == 0
    return list(struct.unpack('>'+'I'*(len(raw)//4), raw))

def path(src, dst):
    queue = deque([[src]])
    seen = {src}
    while queue:
        route = queue.popleft()
        if route[-1] == dst:
            return route
        for nxt in graph[route[-1]]:
            if nxt not in seen:
                seen.add(nxt)
                queue.append(route+[nxt])
    raise ValueError('ICC endpoints have no directed path')

usb = '/soc/ssusb@a600000'
report = {'status': 'PASS', 'artifact': str(args.artifact), 'boards': {},
          'source_sha256': {str(driver): digest(driver), str(header): digest(header)}}
inventory_path = args.workspace/'packaging/module-inventory-20261101-audio-cells.json'
inventory = json.loads(inventory_path.read_text())
for board in ['tuna.dtb', 'tuna7.dtb']:
    old_path = previous/'devicetrees/checked'/board
    new_path = args.artifact/'devicetrees/checked'/board
    before, after = read_fdt(old_path), read_fdt(new_path)
    phandles = {words(props['phandle'])[0]: node for node, props in after.items() if 'phandle' in props}
    main = next(node for node, props in after.items() if b'qcom,tuna-cnoc_main\0' in props.get('compatible', b''))
    values = words(before[usb]['interconnects'])
    assert len(values) == 12 and values[7] == ids['SLAVE_IPA_CFG']
    values[6] = words(after[main]['phandle'])[0]
    expected = {node: dict(props) for node, props in before.items()}
    expected[usb]['interconnects'] = struct.pack('>12I', *values)
    assert expected == after, board
    assert inventory['boards'][board]['sha256'] == digest(old_path)

    def routes(raw):
        cells = words(raw)
        assert len(cells) == 12
        labels = after[usb]['interconnect-names'].decode().strip('\0').split('\0')
        result = []
        for index in range(0, len(cells), 4):
            src_provider, src, dst_provider, dst = cells[index:index+4]
            nodes = [phandles[src_provider], phandles[dst_provider]]
            for node, endpoint in zip(nodes, [src, dst]):
                assert words(after[node]['#interconnect-cells']) == [1]
                compatibles = after[node]['compatible'].decode().strip('\0').split('\0')
                provider = next(providers[c] for c in compatibles if c in providers)
                if endpoint not in provider:
                    raise ValueError('Endpoint absent from provider: '+node+' '+str(endpoint))
                assert names[endpoint] == provider[endpoint]
            route = path(src, dst)
            result.append({'name': labels[index//4], 'providers': nodes,
                           'ids': route, 'nodes': [names[x] for x in route]})
        return result

    checked = routes(after[usb]['interconnects'])
    controls = {'original_wrong_provider': before[usb]['interconnects'],
                'wrong_endpoint': struct.pack('>12I', *[999 if i == 7 else x for i, x in enumerate(values)]),
                'unreachable_endpoint': struct.pack('>12I', *[values[2] if i == 4 else ids['SLAVE_EBI1'] if i == 5 else x for i, x in enumerate(values)])}
    failures = {}
    for name, raw in controls.items():
        try:
            routes(raw)
        except (ValueError, KeyError, AssertionError) as error:
            failures[name] = str(error) or 'invalid source provider'
        else:
            raise AssertionError('Control accepted: '+name)
    report['boards'][board] = {'sha256': digest(new_path), 'routes': checked, 'negative_controls': failures,
                              'only_change': 'usb-ipa destination provider from cnoc_cfg to cnoc_main'}
    inventory['boards'][board]['sha256'] = digest(new_path)
    print('PASS:', board, 'one phandle correction; all three ICC routes connected; controls rejected')
for category in ['gki', 'platform']:
    old_dir, new_dir = previous/category, args.artifact/category
    assert {p.name for p in old_dir.iterdir() if p.is_file()} == {p.name for p in new_dir.iterdir() if p.is_file()}
    for file in old_dir.iterdir():
        if file.is_file():
            assert digest(file) == digest(new_dir/file.name), file.name
assert digest(previous/'devicetrees/dtbo.img') == digest(args.artifact/'devicetrees/dtbo.img')
report['gki_platform_dtbo_bytes_unchanged'] = True
report['limits'] = 'Static provider membership and directed path audit. Hardware USB and modem initialization remain untested.'
new_inventory = args.workspace/'packaging/module-inventory-20261101-usb-ipa.json'
inventory.update(basis=str(args.artifact), compatibility_basis=str(inventory_path),
                 compatibility_validation=str(HERE/'validation.json'))
if new_inventory.exists():
    assert json.loads(new_inventory.read_text()) == inventory
new_inventory.write_text(json.dumps(inventory, indent=2)+'\n')
report['module_inventory'] = str(new_inventory)
(HERE/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
