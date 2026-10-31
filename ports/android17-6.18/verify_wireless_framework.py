#!/usr/bin/env python3
"""Verify the pinned cfg80211 framework, Konoha policy and built certificates."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
POLICY = '\tif (!cfg80211_has_monitors_only(rdev))\n\t\treturn -EBUSY;\n'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def function(source, name):
    match = re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b' +
                      re.escape(name) + r'\([^;]*?\)\n\{', source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def elf_symbol(data, name):
    """Read one defined symbol from a little-endian ELF64 relocatable module."""
    assert data[:6] == b'\x7fELF\x02\x01'
    assert struct.unpack_from('<H', data, 16)[0] == 1
    table = struct.unpack_from('<Q', data, 40)[0]
    stride, count = struct.unpack_from('<HH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, table + i * stride)
                for i in range(count)]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        strings = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            key, _, _, index, value, size = struct.unpack_from('<IBBHQQ', data, offset)
            if strings[key:strings.find(b'\0', key)].decode() == name:
                assert 0 < index < len(sections), name
                sec = sections[index]
                assert value + size <= sec[5]
                return data[sec[4] + value:sec[4] + value + size]
    raise ValueError('missing ELF symbol: ' + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.workspace / args.source_dir
    fixture = HERE / 'wireless-framework-imports.json'
    if not fixture.exists():
        fixture = HERE / 'imports.json'
    pins = json.loads(fixture.read_text())
    assert len(pins) == 30 and {p['project'] for p in pins} == {'common'}
    hashes, originals = {}, {}
    for pin in pins:
        repo = args.workspace / pin['project']
        assert subprocess.check_output(['git', '-C', str(repo), 'rev-parse',
                                        pin['revision'] + ':' + pin['path']], text=True).strip() == pin['blob']
        original = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', pin['blob']])
        originals[pin['path']] = original
        candidate = (root / pin['path']).read_bytes()
        expected = original
        if pin['path'] == 'net/wireless/chan.c':
            assert original.decode().count(POLICY) == 1
            expected = original.decode().replace(POLICY, '').encode()
        assert candidate == expected, pin['path']
        hashes[pin['path']] = sha(candidate)

    # All other nl80211, channel validation and regulatory source is pinned unchanged.
    chan = (root / 'net/wireless/chan.c').read_text()
    bodies = [function((root / 'net/wireless/core.h').read_text(), 'cfg80211_has_monitors_only'),
              function((root / 'net/wireless/rdev-ops.h').read_text(), 'rdev_set_monitor_channel'),
              function(chan, 'cfg80211_set_monitor_channel')]
    template = (HERE / 'tests/wireless_monitor.c').read_text()
    with tempfile.TemporaryDirectory(prefix='onyx-wireless-test-') as tmp:
        tmp = Path(tmp)
        results = {}
        for control in [False, True]:
            units = bodies.copy()
            if control:
                units[-1] = function(originals['net/wireless/chan.c'].decode(),
                                     'cfg80211_set_monitor_channel')
            source = tmp / 'monitor.c'
            binary = tmp / 'monitor'
            source.write_text(template.replace('/* FUNCTIONS */', '\n'.join(units)))
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie',
                            '-Werror=implicit-function-declaration', str(source), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if control:
                assert run.returncode and 'Assertion' in run.stderr
                results['original_policy_control_rejected'] = True
            else:
                assert not run.returncode, run.stderr
                results.update(json.loads(run.stdout))

    output = args.workspace / 'bazel-bin' / args.package
    config = (output / 'cfg80211_config/out_dir/.config').read_text()
    settings = ['CONFIG_CFG80211=m', 'CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y',
                'CONFIG_CFG80211_USE_KERNEL_REGDB_KEYS=y', 'CONFIG_CFG80211_DEFAULT_PS=y',
                '# CONFIG_CFG80211_DEBUGFS is not set', '# CONFIG_CFG80211_WEXT is not set',
                '# CONFIG_CFG80211_KUNIT_TEST is not set', 'CONFIG_OF=y']
    assert all(item in config.splitlines() for item in settings)
    # Derive the enabled native composite, rather than accepting a shortened DDK list.
    makefile = (root / 'net/wireless/Makefile').read_text()
    native = []
    for condition, objects in re.findall(r'^cfg80211-(y|\$\(CONFIG_[A-Z0-9_]+\)) \+= (.*)$', makefile, re.M):
        if condition == 'y' or condition[2:-1] + '=y' in config.splitlines():
            # extra-certs is under a Make conditional and disabled by the empty key directory.
            native += [obj[:-2] + '.c' for obj in objects.split() if obj != 'extra-certs.o']
    build = (root / 'BUILD.bazel').read_text()
    block = re.search(r'ddk_module\(\s*name = "cfg80211",(.*?)\n\)', build, re.S).group(1)
    units = re.findall(r'"(:?net/wireless/[^"\n]+\.c)"', block)
    assert sorted(Path(unit).name for unit in units) == sorted(native)
    assert 'CONFIG_CFG80211_EXTRA_REGDB_KEYDIR=' not in config or 'CONFIG_CFG80211_EXTRA_REGDB_KEYDIR=""' in config
    cert_names = ['sforshee', 'wens']
    certs = [bytes(int(byte, 16) for byte in re.findall(r'0x([0-9a-fA-F]{2})',
                 (root / ('net/wireless/certs/' + name + '.hex')).read_text())) for name in cert_names]
    blob = b''.join(certs)
    generated = (output / 'net/wireless/shipped-certs.c').read_text()
    assert bytes(int(byte, 16) for byte in re.findall(r'0x([0-9a-fA-F]{2})', generated)) == blob
    module = (output / 'cfg80211/cfg80211.ko').read_bytes()
    assert elf_symbol(module, 'shipped_regdb_certs') == blob
    assert int.from_bytes(elf_symbol(module, 'shipped_regdb_certs_len'), 'little') == len(blob)
    report = {'tests': results, 'source_sha256': hashes,
              'harness_sha256': {'wireless_monitor.c': sha(template.encode())},
              'configuration': settings, 'native_composite_units': sorted(native),
              'regdb_certificates': {name: sha(cert) for name, cert in zip(cert_names, certs)},
              'certificates_verified_in_generated_source_and_ELF': True,
              'module_sha256': sha(module),
              'limits': 'Pinned 6.18 cfg80211 with the existing Konoha monitor-channel dispatch policy. Host test mocks driver/trace/lockdep calls; native channel/regulatory validation stays unchanged. No main WLAN driver, firmware, wireless runtime or phone test.'}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
