from pathlib import Path
import hashlib
import json
import struct
import sys
import tempfile
from unittest.mock import patch

H = Path(__file__).resolve().parent
P = H.parent.parent
sys.path.insert(0, str(P))
import collect_rawdump_logs as collector

recorded = (H / 'recorded-section-table.bin').read_bytes()
provenance = json.loads((H / 'fixture-provenance.json').read_text())
assert hashlib.sha256(recorded).hexdigest() == provenance['table_sha256']
table = collector.parse_sections(recorded)
assert table['header']['count'] == 124 and table['header']['section_table_offset'] == 88
assert table['header']['dump_size'] == 43664404
assert next(s for s in table['sections'] if s['name'] == 'md_KCONSOLE.BIN')['size'] == 2097152
cases = ['recorded_v2_table']


def rejects(name, fn):
    try:
        fn()
    except ValueError:
        cases.append(name)
    else:
        raise AssertionError(name)


changes = {
    'bad_signature': (0, b'Bad_Dmp!'),
    'invalid_dump': (12, struct.pack('<I', 0)),
    'unsupported_version': (8, struct.pack('<I', 0x3000)),
    'oversized_count': (52, struct.pack('<I', 5000)),
    'wrong_first_offset': (88 + 12, struct.pack('<Q', 8025)),
    'out_of_bounds_size': (88 + 20, struct.pack('<Q', 2**63)),
    'unsafe_name': (88 + 44, b'../secret\0' + b'\0' * 10),
    'duplicate_name': (88 + 64 + 44, recorded[88 + 44:88 + 64]),
}
for name, (offset, value) in changes.items():
    bad = bytearray(recorded)
    bad[offset:offset + len(value)] = value
    rejects(name, lambda: collector.parse_sections(bad))
rejects('truncated_table', lambda: collector.parse_sections(recorded[:1000]))

assert collector.decode_console(struct.pack('<III', 0x43474244, 3, 3) + b'abc__') == b'abc'
assert collector.decode_console(struct.pack('<III', 0x43474244, 2, 5) + b'ABCDE') == b'CDEAB'
cases += ['unwrapped_console', 'wrapped_console']
rejects('bad_console_magic', lambda: collector.decode_console(struct.pack('<III', 0, 0, 0) + b'x'))
rejects('bad_console_bounds', lambda: collector.decode_console(struct.pack('<III', 0x43474244, 5, 5) + b'x'))

# Exercise transport bounds and evidence publication with an independent v1
# container. The recorded v2 fixture above is not synthesized from the parser.
payload = b'kernel\n'
console = struct.pack('<III', 0x43474244, len(payload), len(payload)) + payload
first = 56 + 64
size = first + len(console)
container = collector.HEADER.pack(b'Raw_Dmp!', 0x1000, 1, 0, b'\0' * 8, 0, size, size, 1)
container += collector.SECTION.pack(1, 0x1000, 3, first, len(console), 0, 0, b'md_KCONSOLE.BIN')
container += console
container += b'\0' * (4096 - len(container))
assert collector.parse_sections(container)['header']['section_table_offset'] == 56
cases.append('v1_packed_container')

with tempfile.TemporaryDirectory(prefix='rawdump-collector-test-') as tmp:
    root = Path(tmp)
    reads = []

    def read(adb, offset, size, recovery=False):
        reads.append((offset, size))
        return container[offset:offset + size]

    with patch.object(collector, 'adb_read', side_effect=read):
        report = collector.collect(root / 'stable', ['adb', '-s', 'test-serial'])
    assert reads == [(0, 4096), (first, len(console)), (0, 4096)]
    assert report['console_decode']['status'] == 'PASS'
    assert (root / 'stable/kernel-console.txt').read_bytes() == payload
    assert (root / 'stable/md_KCONSOLE.BIN').stat().st_mode & 0o777 == 0o600
    cases.append('bounded_capture_private_files')
    reads.clear()

    def changing(adb, offset, size, recovery=False):
        reads.append((offset, size))
        data = container[offset:offset + size]
        return data[:-1] + b'X' if len(reads) == 3 else data

    with patch.object(collector, 'adb_read', side_effect=changing):
        rejects('changed_snapshot', lambda: collector.collect(root / 'changed', ['adb', '-s', 'test-serial']))
    assert not (root / 'changed/receipt.json').exists()

report = {'status': 'PASS', 'cases': cases,
          'collector_sha256': hashlib.sha256((P / 'collect_rawdump_logs.py').read_bytes()).hexdigest(),
          'fixture_sha256': provenance['table_sha256'],
          'limits': 'Recorded table, console-ring examples and mocked read transport; real-device capture is separately recorded.'}
(H / 'host-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS:', len(cases), 'rawdump parser, ring and capture cases')
