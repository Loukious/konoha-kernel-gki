#!/usr/bin/env python3
"""Exercise extracted USB gadget helpers and verify pinned FunctionFS layouts."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
PATH = 'drivers/usb/gadget/function/'
def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'
def structure(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    return re.sub(r'\s+', '', re.search(r'(?m)^struct ' + name + r' \{.*?^\};', source, re.S)[0])
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--source-dir', default='onyx')
    ap.add_argument('--output', type=Path)
    a = ap.parse_args()
    w = a.workspace.resolve()
    root = w / a.source_dir
    cdev, ccid, ffs = [(root / PATH / name).read_text() for name in ['f_cdev.c', 'f_ccid.c', 'f_fs_ipc_log.c']]
    groups = {
        'gadget_cdev': (cdev, ['convert_acm_sigs_to_uart', 'usb_cser_free_req', 'usb_cser_free_requests', 'usb_cser_read_complete', 'usb_cser_write_complete', 'usb_cser_stop_io', 'f_cdev_open', 'f_cdev_release', 'f_cdev_read', 'f_cdev_write', 'f_cdev_poll', 'usb_cser_notify_locked', 'port_notify_serial_state', 'usb_cser_notify_complete', 'dun_cser_get_dtr', 'dun_cser_get_rts', 'f_cdev_tiocmget', 'f_cdev_tiocmset', 'usb_cser_notify_modem', 'usb_cser_disconnect']),
        'gadget_ccid': (ccid, ['ccid_release', 'ccid_device_release', 'ccid_cdev_init', 'ccid_cdev_free', 'ccid_alloc_chrdev_region', 'ccid_free_chrdev_region', 'ccid_setup', 'ccid_function_disable', 'ccid_function_set_alt', 'ccid_ctrl_ioctl', 'ccid_request_alloc']),
        'gadget_ffs': (ffs, ['ipc_inst_dev_exists', 'create_ipc_context_work', 'create_ipc_context', 'get_ipc_context', 'kretprobe_init', 'kretprobe_exit']),
    }
    result = {}
    with tempfile.TemporaryDirectory(prefix='onyx-gadget-') as tmp:
        for name, (source, names) in groups.items():
            code = '\n'.join(function(source, n) for n in names)
            template = (HERE / 'tests' / (name + '.c')).read_text()
            output = Path(tmp) / (name + '.c')
            output.write_text(template.replace('/* FUNCTIONS */', code))
            exe = Path(tmp) / name
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(output), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
            if run.returncode:
                raise RuntimeError(name + ': ' + run.stderr)
            result[name] = json.loads(run.stdout)
    common = (w / 'common' / PATH / 'f_fs.c').read_text()
    names = ['ffs_function', 'ffs_epfile', 'ffs_ep', 'ffs_io_data', 'ffs_sb_fill_data']
    for name in names:
        assert structure(ffs, name) == structure(common, name), name
    assert (root / PATH / 'u_fs.h').read_bytes() == (w / 'common' / PATH / 'u_fs.h').read_bytes()
    probes = re.findall(r'\bENTRY(?:_EXIT)?\((\w+)\)', ffs)
    probes = [n for n in probes if n != 'name']
    symbols = {line.split()[-1] for line in (w / 'bazel-bin/common/kernel_aarch64/System.map').read_text().splitlines()}
    assert all(p in symbols for p in probes)
    result['functionfs'] = {'private_structures_match_pinned_kernel': names, 'probed_symbols_present': probes}
    assert 'cdev_device_add' in function(cdev, 'f_cdev_alloc')
    assert 'cdev_device_add' in function(ccid, 'ccid_cdev_init')
    unbind = function(cdev, 'usb_cser_unbind')
    assert unbind.index('usb_cser_disable') < unbind.index('mutex_lock') < unbind.index('usb_cser_free_req')
    unbind = function(ccid, 'ccid_function_unbind')
    assert unbind.index('ccid_function_disable') < unbind.index('mutex_lock') < unbind.index('ccid_request_free')
    assert 'generation != port->connection_id' in function(cdev, 'f_cdev_read')
    assert 'generation != port->connection_id' in function(cdev, 'f_cdev_write')
    assert 'const char *name' in function(cdev, 'cser_set_inst_name') and '*str = 0' not in cdev
    assert 'scnprintf(page, PAGE_SIZE, "%s", buf)' in cdev
    assert 'atomic_cmpxchg' in function(cdev, 'cser_alloc') and 'atomic_cmpxchg' in function(ccid, 'ccid_alloc')
    # This ABI and descriptor data remains the pinned vendor representation.
    imports = json.loads((HERE / 'gadget-imports.json').read_text())
    unchanged = []
    for item in imports:
        if item['path'].endswith('.h'):
            data = subprocess.check_output(['git', '-C', str(w / item['project']), 'cat-file', 'blob', item['blob']])
            assert (root / item['path']).read_bytes() == data
            unchanged.append(item['path'])
    descriptors = []
    for name, current in [('f_ccid.c', ccid), ('f_cdev.c', cdev)]:
        item = next(x for x in imports if x['path'] == PATH + name)
        original = subprocess.check_output(['git', '-C', str(w / item['project']), 'cat-file', 'blob', item['blob']]).decode()
        pattern = r'(?m)^static struct usb_\w+ (\w+) = \{.*?^\};'
        before = {m[1]: re.sub(r'\s+', '', m[0]) for m in re.finditer(pattern, original, re.S)}
        after = {m[1]: re.sub(r'\s+', '', m[0]) for m in re.finditer(pattern, current, re.S)}
        assert before == after, name
        descriptors.extend(before)
    result['unchanged_usb_descriptors'] = descriptors
    result['unchanged_pinned_headers'] = unchanged
    result['source_sha256'] = {item['path']: hashlib.sha256((root / item['path']).read_bytes()).hexdigest() for item in imports}
    result['harness_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE / 'tests').glob('gadget_*.c')}
    result['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    result['limits'] = 'Extracted driver functions with mocked VFS/device/request/kprobe services; does not exercise live USB controller, configfs, kernel locking, kretprobe admissibility, gadget userspace or phone operation.'
    text = json.dumps(result, indent=2) + '\n'
    if a.output:
        a.output.write_text(text)
    print(text, end='')
if __name__ == '__main__':
    main()
