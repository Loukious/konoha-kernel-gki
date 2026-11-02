"""Record applicable nodes in both already-validated pinned Onyx DT variants."""
import hashlib
import json
import re
import struct
from pathlib import Path

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
A = W / 'artifacts/20261031T232938953896Z/devicetrees'


def nodes(blob):
    magic, size, pos, strings, _, _, _, _, _, _ = struct.unpack_from('>10I', blob)
    assert magic == 0xd00dfeed and size == len(blob)
    result, stack = {}, []
    while True:
        token, = struct.unpack_from('>I', blob, pos)
        pos += 4
        if token == 1:
            end = blob.index(0, pos)
            name = blob[pos:end].decode()
            stack.append(name)
            result['/' + '/'.join(stack[1:])] = {}
            pos = (end + 4) & ~3
        elif token == 2:
            stack.pop()
        elif token == 3:
            length, offset = struct.unpack_from('>II', blob, pos)
            pos += 8
            end = blob.index(0, strings + offset)
            name = blob[strings + offset:end].decode()
            result['/' + '/'.join(stack[1:])][name] = blob[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            break
        else:
            raise ValueError(token)
    return result


compat = {}
for name in json.loads((H / 'summary.json').read_text()):
    evidence = json.loads((H / (name + '.json')).read_text())
    compat[name] = sorted(set(re.findall(r'\.compatible\s*=\s*"([^"]+)"',
        '\n'.join((H / 'original' / obj['path']).read_text() for obj in evidence['objects']))))
report = {'archive': str(A.parent), 'native_compatibles': compat, 'variants': {}}
for variant in ('tuna', 'tuna7'):
    path = A / 'checked' / (variant + '.dtb')
    data = path.read_bytes()
    tree = nodes(data)
    record = {'path': str(path), 'sha256': hashlib.sha256(data).hexdigest(), 'modules': {}}
    for module, matches in compat.items():
        entries = []
        for node, props in tree.items():
            found = [v.decode() for v in props.get('compatible', b'').split(b'\0') if v]
            if not set(found).intersection(matches):
                continue
            parent = node
            blockers = []
            while parent:
                status = tree.get(parent, {}).get('status', b'okay\0').rstrip(b'\0').decode()
                if status not in ('ok', 'okay'):
                    blockers.append({'node': parent, 'status': status})
                parent = parent.rsplit('/', 1)[0]
            properties = {}
            for key, value in props.items():
                if key == 'status' or key.startswith(('qcom,', 'reg', 'clock')):
                    properties[key] = value.hex()
            entries.append({'node': node, 'compatible': found, 'effectively_enabled': not blockers,
                            'disabled_ancestors': blockers, 'properties_hex': properties})
        record['modules'][module] = entries
    report['variants'][variant] = record
report['source_lock_sha256'] = {str(p.relative_to(A)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in [A / 'inputs/sources.json', A / 'inputs/source-hashes.json', A / 'inputs/dependency-lock.xml']}
report['limits'] = ('DT enablement is evidence of intended binding only, not physical enumeration, '
                    'module loading or tested functionality. SDCA is a service with no direct node. '
                    'ROM alternatives are retained without claiming active Onyx hardware use.')
(H / 'dt-evidence.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({v: {m: {'nodes': len(n), 'enabled': sum(x['effectively_enabled'] for x in n)}
    for m, n in d['modules'].items()} for v, d in report['variants'].items()}, indent=2))
