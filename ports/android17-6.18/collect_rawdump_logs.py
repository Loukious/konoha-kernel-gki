#!/usr/bin/env python3
"""Read selected Qualcomm kernel-log sections without modifying the rawdump partition."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

HEADER = struct.Struct('<8sIIQ8sIQQI')
SECTION = struct.Struct('<IIIQQQQ20s')
HEADER_OFFSETS = {0x1000: HEADER.size, 0x2000: HEADER.size + 32}
KERNEL_LOG_NAMES = frozenset([
    'MD_RST_STAT.BIN', 'md_RST_STAT.BIN', 'md_kmsg',
    'md_KCONSOLE.BIN', 'md_KLOGBUF.BIN', 'md_KBOOT_LOG.BIN',
    'md_LOG_DESC.BIN', 'md_LOG_INFO.BIN',
])


def parse_sections(data):
    """Validate packed v1 and observed v2 headers, preserving opaque v2 fields."""
    if len(data) < HEADER.size:
        raise ValueError('Truncated rawdump header')
    sig, version, valid, _, _, reset, dump_size, total_size, count = HEADER.unpack_from(data)
    if sig != b'Raw_Dmp!' or valid != 1:
        raise ValueError('No complete valid Qualcomm rawdump')
    # The Onyx v2 header adds 32 bytes before its v1-format section table.
    # Do not interpret undocumented fields in that extension.
    if version not in HEADER_OFFSETS:
        raise ValueError(f'Unsupported rawdump header version {version:#x}')
    table = HEADER_OFFSETS[version]
    table_end = table + count * SECTION.size
    if not 0 < count <= 4096 or table_end > len(data) or table_end > dump_size:
        raise ValueError('Invalid or truncated rawdump section table')
    if total_size != dump_size or dump_size > 1024 ** 3:
        raise ValueError('Incomplete or unexpectedly large rawdump')
    sections = []
    names = set()
    for i in range(count):
        svalid, sver, kind, offset, size, addr, info, name = SECTION.unpack_from(data, table + i * SECTION.size)
        name_bytes, separator, padding = name.partition(b'\0')
        if not name_bytes or (separator and any(padding)):
            raise ValueError('Malformed section name')
        try:
            name = name_bytes.decode('ascii')
        except UnicodeDecodeError as error:
            raise ValueError('Non-ASCII section name') from error
        if name in names or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-' for c in name):
            raise ValueError('Duplicate or unsafe section name')
        names.add(name)
        if svalid not in [0, 1, 2] or sver != 0x1000 or offset < table_end or size > dump_size - offset:
            raise ValueError('Invalid section version, validity or bounds')
        if offset > dump_size:
            raise ValueError('Section starts past dump end')
        sections.append({'name': name, 'valid': svalid, 'version': sver, 'kind': kind,
                         'offset': offset, 'size': size, 'paddr': addr, 'info': info})
    # Required by the packed layouts supported here. Gaps are unsupported;
    # refusing unknown formats is preferable to extracting the wrong bytes.
    previous = table_end
    for section in sections:
        if section['offset'] != previous:
            raise ValueError('Noncontiguous or out-of-order rawdump sections')
        previous += section['size']
    if previous != dump_size:
        raise ValueError('Sections do not fill declared dump')
    return {'header': {'version': version, 'valid': valid, 'reset_trigger': reset,
                       'dump_size': dump_size, 'total_size': total_size, 'count': count,
                       'section_table_offset': table,
                       'opaque_extension_hex': data[HEADER.size:table].hex()},
            'sections': sections}


def adb_read(adb, offset, size, recovery=False):
    block = 4096
    skip, start = divmod(offset, block)
    count = (start + size + block - 1) // block
    command = f'dd if=/dev/block/by-name/rawdump bs={block} skip={skip} count={count} 2>/dev/null'
    if not recovery:
        command = "su -c '" + command + "'"
    result = subprocess.run(adb + ['exec-out', command], capture_output=True, timeout=45, check=True)
    if len(result.stdout) != count * block:
        raise ValueError(f'Short rawdump read: requested {count * block}, received {len(result.stdout)}')
    return result.stdout[start:start + size]


def save_private(path, data):
    path.write_bytes(data)
    path.chmod(0o600)


def decode_console(data):
    """Decode the native 12-byte persistent RAM header and chronological ring."""
    if len(data) <= 12:
        raise ValueError('Truncated persistent RAM console')
    magic, start, size = struct.unpack_from('<III', data)
    capacity = len(data) - 12
    if magic != 0x43474244 or start > capacity or size > capacity:
        raise ValueError('Unsupported or invalid persistent RAM console header')
    first = (start - size) % capacity
    ring = data[12:]
    return (ring[first:] + ring[:first])[:size]


def read_snapshot(adb, recovery=False):
    first = adb_read(adb, 0, 4096, recovery)
    fields = HEADER.unpack_from(first)
    if fields[0] != b'Raw_Dmp!' or fields[2] != 1 or fields[1] not in HEADER_OFFSETS:
        raise ValueError('No supported complete Qualcomm rawdump')
    count = fields[-1]
    if not 0 < count <= 4096:
        raise ValueError('Invalid rawdump section count')
    size = HEADER_OFFSETS[fields[1]] + count * SECTION.size
    size = (size + 4095) & ~4095
    return first if size <= len(first) else adb_read(adb, 0, size, recovery)


def collect(output, adb, recovery=False, header_input=None):
    output.mkdir(parents=True, mode=0o700, exist_ok=False)
    snapshot = read_snapshot(adb, recovery)
    if header_input and snapshot != header_input.read_bytes()[:len(snapshot)]:
        raise ValueError('Rawdump changed since supplied header snapshot')
    table = parse_sections(snapshot)
    save_private(output / 'header-snapshot.bin', snapshot)
    save_private(output / 'sections.json', (json.dumps(table, indent=2) + '\n').encode())
    receipt = {'serial': adb[-1], 'timestamp': datetime.now(timezone.utc).isoformat(),
               'snapshot_sha256': hashlib.sha256(snapshot).hexdigest(), 'files': {},
               'limits': ['Rawdump may belong to an older boot; use the recorded kernel version, panic and build ID for attribution.',
                          'Only kernel-log sections are read; secure firmware/key sections are excluded.',
                          'Matching snapshots bound the collection but do not establish crash time or atomic firmware writes.']}
    for item in table['sections']:
        if item['name'] not in KERNEL_LOG_NAMES or item['valid'] != 1:
            continue
        if item['size'] > 16 * 1024 * 1024:
            raise ValueError('Unexpectedly large kernel-log section')
        data = adb_read(adb, item['offset'], item['size'], recovery)
        save_private(output / item['name'], data)
        receipt['files'][item['name']] = {'size': len(data), 'offset': item['offset'],
                                        'sha256': hashlib.sha256(data).hexdigest()}
        if item['name'] == 'md_KCONSOLE.BIN':
            try:
                console = decode_console(data)
            except ValueError as error:
                receipt['console_decode'] = {'status': 'unsupported', 'reason': str(error)}
            else:
                save_private(output / 'kernel-console.txt', console)
                text = console.decode(errors='replace')
                receipt['console_decode'] = {
                    'status': 'PASS', 'bytes': len(console),
                    'sha256': hashlib.sha256(console).hexdigest(),
                    'kernel_banners': re.findall(r'(?m)^.*Linux version.*$', text),
                    'panic_lines': re.findall(r'(?m)^.*Kernel panic.*$', text)}
    if not receipt['files']:
        raise ValueError('No supported kernel-log sections in rawdump')
    if adb_read(adb, 0, len(snapshot), recovery) != snapshot:
        raise ValueError('Rawdump snapshot changed during capture; do not attribute mixed sections')
    receipt['snapshot_unchanged_after_capture'] = True
    save_private(output / 'receipt.json', (json.dumps(receipt, indent=2) + '\n').encode())
    return receipt


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    p.add_argument('--adb', default='/mnt/d/platform-tools/adb.exe')
    p.add_argument('--serial', default='5493f3c6')
    p.add_argument('--header-input', type=Path)
    p.add_argument('--recovery', action='store_true')
    a = p.parse_args()
    receipt = collect(a.output, [a.adb, '-s', a.serial], a.recovery, a.header_input)
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
