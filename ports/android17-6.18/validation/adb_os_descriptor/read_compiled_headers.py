#!/usr/bin/env python3
"""Read the two emitted MS OS headers from a local unstripped adbd build."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
blob = args.binary.read_bytes()
header = struct.unpack_from('<16sHHIQQQIHHHHHH', blob)
assert header[0][:6] == b'\x7fELF\x02\x01' and header[2] == 183
section_offset, section_size, section_count = header[6], header[11], header[12]
assert section_size == 64
sections = [struct.unpack_from('<IIQQQQIIQQ', blob, section_offset + i * section_size)
            for i in range(section_count)]
targets = {'_ZL14os_desc_header', '_ZL14os_prop_header'}
headers = {}
for section in sections:
    if section[1] != 2:
        continue
    strings = sections[section[6]]
    table = blob[strings[4]:strings[4] + strings[5]]
    assert section[9] == 24
    for offset in range(section[4], section[4] + section[5], section[9]):
        name_offset, info, other, section_index, value, size = struct.unpack_from('<IBBHQQ', blob, offset)
        if name_offset >= len(table):
            continue
        name = table[name_offset:table.index(b'\0', name_offset)].decode()
        if name not in targets:
            continue
        assert size == 11 and section_index < len(sections)
        containing = sections[section_index]
        position = containing[4] + value - containing[3]
        assert containing[4] <= position < containing[4] + containing[5]
        data = blob[position:position + size]
        assert len(data) == size
        headers[name] = {'file_offset': position, 'size': size, 'hex': data.hex(),
                         'bcd_version': int.from_bytes(data[5:7], 'little')}
assert headers.keys() == targets
result = {'binary': str(args.binary.resolve()), 'sha256': hashlib.sha256(blob).hexdigest(),
          'headers': headers, 'scope': 'Local AArch64 ELF static descriptor data; no runtime USB validation'}
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(headers, indent=2))
