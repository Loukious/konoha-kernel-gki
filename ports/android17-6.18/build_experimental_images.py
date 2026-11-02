#!/usr/bin/env python3
"""Build and unpack-audit matched Onyx experimental images without flashing."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('artifact', type=Path)
p.add_argument('bundle', type=Path)
p.add_argument('baseline', type=Path)
p.add_argument('rom_root', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--partition-sizes', type=Path,
               help='verified target DLKM sizes; fail rather than grow these partitions')
p.add_argument('--compact-system-erofs', action='store_true',
               help='use LZ4HC fragments/deduplication without changing file contents')
p.add_argument('--diagnostic-boot', action='store_true',
               help='retain INFO/DEBUG and initialization timing in the persistent kernel console')
p.add_argument('--boot-checkpoints', action='store_true',
               help='enable recorded probe/watchdog checkpoints and GKI lockup backtraces')
p.add_argument('--funnel-checkpoints', action='store_true',
               help='trace the adapted CoreSight funnel probes; requires --boot-checkpoints')
p.add_argument('--gpi-checkpoints', action='store_true',
               help='trace GPI DMA ring addresses; requires --boot-checkpoints')
p.add_argument('--blacklist-module', action='append', default=[],
               help='diagnostic-only module exclusion; may be repeated to isolate a boot failure')
a = p.parse_args()
assert not a.boot_checkpoints or a.diagnostic_boot, 'Boot checkpoints require --diagnostic-boot'
assert not a.funnel_checkpoints or a.boot_checkpoints, 'Funnel checkpoints require --boot-checkpoints'
assert not a.gpi_checkpoints or a.boot_checkpoints, 'GPI checkpoints require --boot-checkpoints'
assert not a.blacklist_module or a.diagnostic_boot, 'Module isolation requires --diagnostic-boot'
assert len(a.blacklist_module) == len(set(a.blacklist_module))
assert all(re.fullmatch(r'[A-Za-z0-9_]+', name) for name in a.blacklist_module)
assert not a.output.exists(), 'Use a new output directory'
a.output.mkdir(parents=True)
logs = a.output / 'logs'; logs.mkdir()
images = a.output / 'images'; images.mkdir()
tools = a.rom_root / 'out/host/linux-x86/bin'
env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ.get('PATH', ''))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
read = lambda path: json.loads(path.read_text())
commands = []
def run(command, label, stdout=None):
    command = [str(x) for x in command]
    commands.append(command)
    if stdout:
        with stdout.open('wb') as stream:
            result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.PIPE)
        output = b''
    else:
        result = subprocess.run(command, env=env, capture_output=True)
        output = result.stdout
    (logs / (label + '.log')).write_bytes(output + result.stderr)
    if result.returncode:
        raise RuntimeError(f'{label} failed: {result.stderr.decode(errors="replace")}')
    return output.decode(errors='replace')
def tool(name):
    path = tools / name
    assert path.is_file(), path
    return path
bundle = read(a.bundle / 'report.json')
assert bundle['status'] == 'PASS' and Path(bundle['artifact']).resolve() == a.artifact.resolve()
for name, digest in bundle['sha256'].items():
    assert sha(a.bundle / name) == digest, name
baseline = read(a.baseline / 'image-inputs.json')
for name, item in baseline['source_images'].items():
    assert sha(Path(item['path'])) == item['sha256'], name
build = read(a.artifact / 'report.json')
for name, digest in build['sha256'].items():
    assert sha(a.artifact / name) == digest, name
def avb_info(name):
    text = run([tool('avbtool'), 'info_image', '--image', baseline['source_images'][name]['path']], 'baseline-avb-' + name)
    field = lambda key: re.search(r'^' + re.escape(key) + r':\s*(.+)$', text, re.M).group(1).strip()
    return {'algorithm': field('Algorithm'), 'rollback_index': field('Rollback Index'),
            'size': baseline['source_images'][name]['size'],
            'salt': re.search(r'^\s+Salt:\s*(\S+)', text, re.M).group(1),
            'props': re.findall(r"^\s+Prop: (\S+) -> '(.*)'$", text, re.M),
            'fec_roots': int(re.search(r'FEC num roots:\s*(\d+)', text).group(1)) if 'FEC num roots:' in text else 0}
layouts = {n: avb_info(n) for n in ['boot', 'vendor_boot', 'dtbo', 'system_dlkm', 'vendor_dlkm']}
target_sizes = read(a.partition_sizes) if a.partition_sizes else {}
assert set(target_sizes) <= {'system_dlkm', 'vendor_dlkm'}
for name, size in target_sizes.items():
    assert isinstance(size, int) and size > 0 and size % 4096 == 0
    layouts[name]['baseline_size'] = layouts[name]['size']
    layouts[name]['size'] = size
def footer(name, hashtree=False):
    layout = layouts[name]
    command = [tool('avbtool'), 'add_hashtree_footer' if hashtree else 'add_hash_footer',
               '--partition_name', name, '--hash_algorithm', 'sha256',
               '--salt', layout['salt'], '--algorithm', layout['algorithm'],
               '--rollback_index', layout['rollback_index']]
    if layout['algorithm'] != 'NONE':
        command += ['--key', a.rom_root / 'external/avb/test/data/testkey_rsa2048.pem']
    if hashtree:
        command += ['--fec_num_roots', str(layout['fec_roots'])] if layout['fec_roots'] else ['--do_not_generate_fec']
    for key, value in layout['props']:
        command += ['--prop', key + ':' + value]
    size = layout['size']
    if hashtree:
        while True:
            maximum = int(run(command + ['--partition_size', str(size), '--calc_max_image_size'], 'maximum-' + name).strip())
            if (images / (name + '.img')).stat().st_size <= maximum:
                break
            assert name not in target_sizes, f'{name} cannot fit the verified target partition'
            size += 1048576
            assert size <= 128 * 1048576, 'Unexpected DLKM size'
    run(command + ['--partition_size', str(size), '--image', images / (name + '.img')], 'footer-' + name)
    assert (images / (name + '.img')).stat().st_size == size
    layout['output_size'] = size
    run([tool('avbtool'), 'verify_image', '--image', images / (name + '.img')], 'verify-avb-' + name)
    run([tool('avbtool'), 'info_image', '--image', images / (name + '.img')], 'candidate-avb-' + name)
def cpio_entries(data):
    entries = {}; pos = 0
    while pos + 110 <= len(data):
        start = pos
        assert data[pos:pos + 6] == b'070701', ('unsupported cpio', pos)
        fields = [int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16) for i in range(13)]
        length = fields[11]; size = fields[6]
        assert length and pos + 110 + length <= len(data)
        raw_name = data[pos + 110:pos + 110 + length]
        assert raw_name.endswith(b'\0')
        name = raw_name[:-1].decode()
        assert not name.startswith('/') and '..' not in PurePosixPath(name).parts
        name = name.removeprefix('./')
        pos = (pos + 110 + length + 3) & ~3
        payload = data[pos:pos + size]; assert len(payload) == size
        pos = (pos + size + 3) & ~3
        if name == 'TRAILER!!!':
            return entries
        assert name not in entries, name
        entries[name] = {'raw': data[start:pos], 'payload': payload, 'fields': fields}
    raise ValueError('Missing CPIO trailer')
def cpio_record(name, payload, mode, inode):
    encoded = name.encode() + b'\0'
    fields = [inode, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 1790812800,
              len(payload), 0, 0, 0, 0, len(encoded), 0]
    result = b'070701' + ''.join(f'{n:08x}' for n in fields).encode() + encoded
    result += b'\0' * (-len(result) % 4)
    result += payload
    return result + b'\0' * (-len(result) % 4)
raw = a.output / 'baseline-vendor.cpio'
run([tool('lz4'), '-d', '-c', a.baseline / 'vendor_boot/vendor_ramdisk00'], 'uncompress-baseline', raw)
before = cpio_entries(raw.read_bytes())
is_modules = lambda n: n == 'lib/modules' or n.startswith('lib/modules/')
preserved = {n: entry for n, entry in before.items() if not is_modules(n)}
assert not any(n.endswith('.ko') for n in preserved)
inode = max(entry['fields'][0] for entry in before.values()) + 1
content = b''.join(entry['raw'] for entry in preserved.values())
content += cpio_record('lib/modules', b'', stat.S_IFDIR | 0o755, inode)
module_files = a.bundle / 'vendor_ramdisk/lib/modules'
for path in sorted(module_files.iterdir()):
    assert path.is_file() and not path.is_symlink()
    inode += 1
    content += cpio_record('lib/modules/' + path.name, path.read_bytes(), stat.S_IFREG | 0o644, inode)
content += cpio_record('TRAILER!!!', b'', 0, 0)
content += b'\0' * (-len(content) % 512)
cpio = a.output / 'vendor.cpio'; cpio.write_bytes(content)
after = cpio_entries(content)
assert {n: e['raw'] for n, e in after.items() if not is_modules(n)} == {n: e['raw'] for n, e in preserved.items()}
for path in module_files.iterdir():
    assert after['lib/modules/' + path.name]['payload'] == path.read_bytes()
ramdisk = a.output / 'vendor_ramdisk.lz4'
run([tool('lz4'), '-l', '-12', str(cpio), str(ramdisk)], 'compress-vendor-ramdisk')
def replace_option(args, option, value):
    args[args.index(option) + 1] = str(value)
boot_args = read(a.baseline / 'boot-args.json')
replace_option(boot_args, '--kernel', a.artifact / 'gki/Image')
boot_parameters = a.artifact / 'inputs/boot-parameters.txt'
base_parameters = boot_parameters.read_text().split()
assert base_parameters in [['android_arch_task_struct_size=512'],
                           ['android_arch_task_struct_size=512', 'sched_proxy_exec=0']]
proxy_disabled = 'sched_proxy_exec=0' in base_parameters
if proxy_disabled:
    assert 'CONFIG_SCHED_PROXY_EXEC=y' in (a.artifact / 'gki/kernel_aarch64_dot_config').read_text().splitlines()
boot_cmdline = base_parameters[0]
if a.diagnostic_boot:
    boot_cmdline += ' ignore_loglevel initcall_debug'
if a.boot_checkpoints:
    boot_cmdline += ' printk.devkmsg=on'
    config = (a.artifact / 'gki/kernel_aarch64_dot_config').read_text()
    for option in ['CONFIG_SOFTLOCKUP_DETECTOR=y', 'CONFIG_HARDLOCKUP_DETECTOR=y',
                   'CONFIG_HARDLOCKUP_DETECTOR_BUDDY=y', 'CONFIG_DETECT_HUNG_TASK=y']:
        assert option in config.splitlines(), option
    for name in ['i2c_msm_geni', 'ipam', 'qcom_wdt_core']:
        candidates = [path for path in (a.artifact / 'platform').glob('*.ko')
                      if path.stem.replace('-', '_') == name]
        assert len(candidates) == 1, name
        info = run(['modinfo', '-F', 'parm', candidates[0]],
                   'checkpoint-parameter-' + name)
        assert re.search(r'^boot_trace:', info, re.M), name
        boot_cmdline += ' ' + name + '.boot_trace=1'
    boot_cmdline += (' watchdog_thresh=2 nmi_watchdog=panic,1 softlockup_panic=1'
                     ' sysctl.kernel.hung_task_timeout_secs=5'
                     ' sysctl.kernel.hung_task_panic=1 panic=1')
if a.funnel_checkpoints:
    info = run(['modinfo', '-F', 'parm', a.artifact / 'platform/coresight-funnel.ko'],
               'checkpoint-parameter-coresight-funnel')
    assert re.search(r'^boot_trace:', info, re.M)
    boot_cmdline += ' coresight_funnel.boot_trace=1'
if a.gpi_checkpoints:
    info = run(['modinfo', '-F', 'parm', a.artifact / 'platform/msm_gpi.ko'],
               'checkpoint-parameter-msm-gpi')
    assert re.search(r'^boot_trace:', info, re.M)
    boot_cmdline += ' msm_gpi.boot_trace=1'
if a.blacklist_module:
    boot_cmdline += ' module_blacklist=' + ','.join(a.blacklist_module)
if proxy_disabled:
    boot_cmdline += ' sched_proxy_exec=0'
replace_option(boot_args, '--cmdline', boot_cmdline)
run([tool('mkbootimg'), *boot_args, '--output', images / 'boot.img'], 'mkbootimg-boot')
footer('boot')
vendor_args = read(a.baseline / 'vendor_boot-args.json')
replace_option(vendor_args, '--dtb', a.artifact / 'devicetrees/dtb.img')
replace_option(vendor_args, '--vendor_ramdisk_fragment', ramdisk)
run([tool('mkbootimg'), *vendor_args, '--vendor_boot', images / 'vendor_boot.img'], 'mkbootimg-vendor-boot')
footer('vendor_boot')
shutil.copyfile(a.artifact / 'devicetrees/dtbo.img', images / 'dtbo.img')
footer('dtbo')
run([tool('mkdtimg'), 'dump', images / 'dtbo.img'], 'verify-dtbo')
contexts = a.rom_root / 'out/target/product/onyx/obj/ETC/file_contexts.bin_intermediates/file_contexts.bin'
def tree_files(root):
    return {str(p.relative_to(root)): ('link', os.readlink(p)) if p.is_symlink()
            else ('file', sha(p), stat.S_IMODE(p.stat().st_mode))
            for p in root.rglob('*') if p.is_file() or p.is_symlink()}
for name in ['system_dlkm', 'vendor_dlkm']:
    root = a.output / ('root-' + name)
    run([tool('fsck.erofs'), '--extract=' + str(root), '--no-preserve-owner',
         '--preserve-perms', baseline['source_images'][name]['path']], 'extract-baseline-' + name)
    previous = tree_files(root)
    assert not any(n.endswith('.ko') for n in previous if not n.startswith('lib/modules/'))
    shutil.rmtree(root / 'lib/modules')
    shutil.copytree(a.bundle / name / 'lib/modules', root / 'lib/modules')
    expected = tree_files(root)
    assert {n: v for n, v in expected.items() if not n.startswith('lib/modules/')} == {n: v for n, v in previous.items() if not n.startswith('lib/modules/')}
    compression = ['-zlz4hc,level=12', '-C262144', '-Efragments,dedupe,ztailpacking'] if name == 'system_dlkm' and a.compact_system_erofs else ['-zlz4hc,level=9', '-C65536']
    run([tool('mkfs.erofs'), *compression, '-T1790812800', '--all-root',
         '--file-contexts=' + str(contexts), '--mount-point=/' + name,
         images / (name + '.img'), root], 'mkfs-' + name)
    footer(name, True)
    extracted = a.output / ('checked-' + name)
    run([tool('fsck.erofs'), '--extract=' + str(extracted), '--no-preserve-owner',
         '--preserve-perms', images / (name + '.img')], 'verify-erofs-' + name)
    assert tree_files(extracted) == expected, name
for name in ['boot', 'vendor_boot']:
    destination = a.output / ('checked-' + name)
    args = run([tool('unpack_bootimg'), '--boot_img', images / (name + '.img'),
                '--out', destination, '--format', 'mkbootimg', '-0'], 'unpack-' + name).split('\0')
    if name == 'boot':
        assert sha(destination / 'kernel') == sha(a.artifact / 'gki/Image')
        assert (destination / 'ramdisk').read_bytes() == (a.baseline / 'boot/ramdisk').read_bytes()
        assert args[args.index('--cmdline') + 1] == boot_cmdline
    else:
        assert sha(destination / 'dtb') == sha(a.artifact / 'devicetrees/dtb.img')
        assert sha(destination / 'bootconfig') == sha(a.baseline / 'vendor_boot/bootconfig')
        assert sha(destination / 'vendor_ramdisk00') == sha(ramdisk)
result = {'status': 'PASS', 'purpose': 'Onyx experimental boot-test images; no phone flash',
          'boot_cmdline': boot_cmdline, 'diagnostic_boot': a.diagnostic_boot,
          'boot_parameters_sha256': sha(boot_parameters),
          'proxy_execution_disabled': proxy_disabled,
          'diagnostic_module_blacklist': a.blacklist_module,
          'diagnostic_boot_checkpoints': a.boot_checkpoints,
          'diagnostic_funnel_checkpoints': a.funnel_checkpoints,
          'diagnostic_gpi_checkpoints': a.gpi_checkpoints,
          'artifact': str(a.artifact), 'bundle': str(a.bundle),
          'bundle_report_sha256': sha(a.bundle / 'report.json'),
          'baseline_manifest_sha256': sha(a.baseline / 'image-inputs.json'),
          'filesystem_contexts_sha256': sha(contexts), 'layouts': layouts,
          'target_partition_sizes': target_sizes,
          'target_partition_sizes_sha256': sha(a.partition_sizes) if a.partition_sizes else None,
          'preserved_non_module_cpio_records': len(preserved),
          'images': {path.stem: {'path': str(path), 'sha256': sha(path), 'size': path.stat().st_size}
                     for path in images.iterdir()}, 'commands': commands,
          'limits': 'Offline AVB, filesystem, ramdisk, module and header checks only; no boot or hardware validation. KernelSU/NetHunter completion is deferred from this first test package.'}
(a.output / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: five matched images, AVB verification, filesystem extraction and boot unpack audits')
