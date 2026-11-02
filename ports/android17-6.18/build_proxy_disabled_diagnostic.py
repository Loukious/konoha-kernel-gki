#!/usr/bin/env python3
"""Repack boot with the upstream proxy-execution runtime switch disabled."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('previous', type=Path)
p.add_argument('workspace', type=Path)
p.add_argument('rom_root', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
old = json.loads((a.previous / 'report.json').read_text())
assert old['status'] == 'PASS' and not old['diagnostic_module_blacklist']
for name, item in old['images'].items():
    path = Path(item['path'])
    assert path.stat().st_size == item['size'] and sha(path) == item['sha256'], name
core = a.workspace / 'common/kernel/sched/core.c'
source = core.read_text()
assert 'DEFINE_STATIC_KEY_TRUE(__sched_proxy_exec);' in source
assert '__setup("sched_proxy_exec", setup_proxy_exec);' in source
assert 'static_branch_disable(&__sched_proxy_exec);' in source
assert 'CONFIG_SCHED_PROXY_EXEC=y' in (Path(old['artifact']) / 'gki/kernel_aarch64_dot_config').read_text().splitlines()
assert 'sched_proxy_exec' not in old['boot_cmdline']
assert not a.output.exists()
(a.output / 'images').mkdir(parents=True)
tools = a.rom_root / 'out/host/linux-x86/bin'
commands = []


def run(command, label):
    commands.append(list(map(str, command)))
    result = subprocess.run(commands[-1], capture_output=True)
    (a.output / (label + '.log')).write_bytes(result.stdout + result.stderr)
    assert result.returncode == 0, label
    return result.stdout


args = run([tools / 'unpack_bootimg', '--boot_img', a.previous / 'images/boot.img',
            '--out', a.output / 'input', '--format', 'mkbootimg', '-0'], 'unpack-input').decode().split('\0')
if not args[-1]:
    args.pop()
assert args[args.index('--cmdline') + 1] == old['boot_cmdline']
cmdline = old['boot_cmdline'] + ' sched_proxy_exec=0'
args[args.index('--cmdline') + 1] = cmdline
boot = a.output / 'images/boot.img'
run([tools / 'mkbootimg', *args, '--output', boot], 'mkbootimg')
layout = old['layouts']['boot']
footer = [tools / 'avbtool', 'add_hash_footer', '--image', boot,
          '--partition_name', 'boot', '--partition_size', str(layout['size']),
          '--hash_algorithm', 'sha256', '--salt', layout['salt'],
          '--algorithm', layout['algorithm'], '--rollback_index', str(layout['rollback_index'])]
if layout['algorithm'] != 'NONE':
    footer += ['--key', a.rom_root / 'external/avb/test/data/testkey_rsa2048.pem']
for key, value in layout['props']:
    footer += ['--prop', key + ':' + value]
run(footer, 'avb-footer')
run([tools / 'avbtool', 'verify_image', '--image', boot], 'avb-verify')
checked = run([tools / 'unpack_bootimg', '--boot_img', boot,
               '--out', a.output / 'checked', '--format', 'mkbootimg', '-0'], 'unpack-check').decode().split('\0')
assert checked[checked.index('--cmdline') + 1] == cmdline
assert {p.name for p in (a.output / 'input').iterdir()} == {p.name for p in (a.output / 'checked').iterdir()}
for path in (a.output / 'input').iterdir():
    assert sha(path) == sha(a.output / 'checked' / path.name), path.name
assert sha(a.output / 'checked/kernel') == sha(Path(old['artifact']) / 'gki/Image')
result = dict(old)
result['images'] = {}
for name in old['images']:
    target = a.output / 'images' / (name + '.img')
    if name != 'boot':
        shutil.copyfile(Path(old['images'][name]['path']), target)
        assert sha(target) == old['images'][name]['sha256']
    result['images'][name] = {'path': str(target), 'size': target.stat().st_size, 'sha256': sha(target)}
assert [name for name in old['images'] if old['images'][name]['sha256'] != result['images'][name]['sha256']] == ['boot']
result.update(purpose='WALT compatibility diagnostic with upstream proxy execution disabled',
              boot_cmdline=cmdline, diagnostic_proxy_execution_disabled=True,
              proxy_switch_source=str(core), proxy_switch_source_sha256=sha(core),
              previous_report=str(a.previous / 'report.json'),
              previous_report_sha256=sha(a.previous / 'report.json'), commands=commands,
              limits='Only the boot command line changes from the checked candidate; kernel and ramdisk bytes match. Hardware validation pending.')
(a.output / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: only boot cmdline changes; kernel/ramdisk and four image hashes identical')
