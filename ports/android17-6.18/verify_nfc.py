#!/usr/bin/env python3
"""Exercise extracted NFC code using simulated kernel and transport services."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:inline )?(?:const )?(?:enum \w+|struct \w+|unsigned(?: int| long)?|[A-Za-z_]\w*)[ \t\n*]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--package', default='onyx')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    root = args.workspace.resolve() / args.package
    base = root / 'nxp/opensource/driver/nfc'
    sources = {p.name: p.read_text() for p in base.glob('*.[ch]')}
    groups = {
        'nfc_wire': [('common_nxp.c', ['get_nfcc_chip_type_dl', 'get_nfcc_session_state_dl', 'nfc_reset_notification_valid', 'get_nfcc_chip_type'])],
        'nfc_reset': [('ese_cold_reset.c', ['read_cold_reset_rsp_locked', 'nfc_cold_reset_response', 'ese_cold_reset_ioctl'])],
        'nfc_lifetime': [
            ('common.c', ['nfc_dev_release_ref', 'nfc_dev_put', 'nfc_dev_publish', 'nfc_dev_unpublish', 'configure_gpio', 'gpio_free_all', 'nfc_misc_unregister', 'nfc_misc_register', 'nfc_post_init', 'nfc_dev_open', 'nfc_dev_flush', 'nfc_dev_close']),
            ('i2c_drv.c', ['nfc_i2c_cleanup', 'nfc_i2c_dev_probe', 'nfc_i2c_dev_remove']),
        ],
        'nfc_power': [('common_qcom.c', ['nfc_ldo_vote', 'nfc_ldo_config', 'nfc_ldo_unvote']), ('common.c', ['nfc_ioctl_power_states']), ('i2c_drv.c', ['nfc_i2c_dev_suspend', 'nfc_i2c_dev_resume'])],
    }
    result = {}
    with tempfile.TemporaryDirectory(prefix='onyx-nfc-') as tmp:
        for name, inputs in groups.items():
            functions = '\n'.join(function(sources[src], n) for src, names in inputs for n in names)
            text = (HERE / 'tests' / (name + '.c')).read_text().replace('/* FUNCTIONS */', functions)
            if name == 'nfc_wire':
                defines = '\n'.join(line for line in sources['common_nxp.h'].splitlines() if line.startswith('#define '))
                text = text.replace('/* DEFINES */', defines)
            if name == 'nfc_reset':
                header = sources['ese_cold_reset.h']
                types = '\n'.join(re.findall(r'enum \w+\s*\{[^}]*\};', header))
                types += '\n'.join(re.findall(r'struct (?:ese_cold_reset_arg|ese_ioctl_arg|cold_reset) \{.*?\n\};', header, re.S))
                defines = '\n'.join(line for line in header.splitlines() if line.startswith('#define ') and '__ESE_COLD_RESET_H' not in line)
                text = text.replace('/* TYPES */', defines + '\n' + types)
            if name == 'nfc_power':
                header = sources['common.h']
                types = '\n'.join(re.findall(r'enum (?:nfcc_ioctl_request|nfc_state_flags) \{.*?\n\};', header, re.S))
                defines = '\n'.join(line for line in header.splitlines() if line.startswith('#define NFC_LDO_') or line.startswith('#define NFC_VDDIO_'))
                text = text.replace('/* TYPES */', defines + '\n' + types)
            source, exe = Path(tmp) / (name + '.c'), Path(tmp) / name
            source.write_text(text)
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', '-pthread', str(source), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
            if run.returncode:
                raise RuntimeError(name + ': ' + run.stderr)
            result[name] = json.loads(run.stdout)
    result['source_sha256'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(base.glob('*.[ch]'))}
    result['harness_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE / 'tests').glob('nfc_*.c')}
    contracts = []
    def contract(description, condition):
        if not condition:
            raise AssertionError(description)
        contracts.append(description)
    for source, name in [('common.c', 'nfc_dev_open'), ('common.c', 'nfc_dev_close'), ('common.c', 'nfc_dev_ioctl'), ('common.c', 'nfc_dev_flush'), ('i2c_drv.c', 'nfc_i2c_dev_read'), ('i2c_drv.c', 'nfc_i2c_dev_write')]:
        contract(name + ' holds SRCU through hardware access', 'guard(srcu)(&nfc_io_srcu);' in function(sources[source], name))
    cleanup = function(sources['i2c_drv.c'], 'nfc_i2c_cleanup')
    contract('remove withdraws and drains file operations before IRQ, GPIO and regulator teardown', cleanup.index('nfc_dev_unpublish') < cleanup.index('nfc_misc_unregister') < cleanup.index('free_irq') < cleanup.index('gpio_free_all'))
    withdraw = function(sources['common.c'], 'nfc_dev_unpublish')
    contract('remove wakes both reader and reset waiters before SRCU drain', withdraw.index('WRITE_ONCE(dev->removing, true)') < withdraw.index('wake_up_interruptible(&dev->cold_reset.read_wq)') < withdraw.index('synchronize_srcu'))
    probe = function(sources['i2c_drv.c'], 'nfc_i2c_dev_probe')
    contract('probe initializes locks and controller before userspace publication', probe.index('mutex_init(&dev->cold_reset.lock)') < probe.index('request_irq') < probe.index('nfc_post_init') < probe.index('nfc_misc_register') < probe.index('nfc_dev_publish'))
    contract('IRQ starts disabled until a ready controller enables it', 'IRQF_NO_AUTOEN' in probe)
    for name in ['nfc_i2c_dev_read', 'nfc_i2c_dev_write']:
        body = function(sources['i2c_drv.c'], name)
        contract(name + ' handles zero length without waiting for another operation', body.index('if (!count)') < body.index('mutex_lock'))
    for name in ['nfcinfo.h', 'sn_uapi.h']:
        path = 'nxp/opensource/driver/include/uapi/linux/nfc/' + name
        item = next(i for i in json.loads((HERE / 'nfc-imports.json').read_text()) if i['path'] == path)
        original = subprocess.check_output(['git', '-C', str(args.workspace / item['project']), 'cat-file', 'blob', item['blob']])
        contract('pinned NFC userspace ABI unchanged: ' + name, (root / path).read_bytes() == original)
    result['source_contracts'] = contracts
    result['limits'] = 'Extracted driver functions with mocked kernel, user-copy, I2C and pthread locking; does not validate live VFS/SRCU/IRQ semantics, NFC firmware, secure peripheral mode, electrical behavior or phone operation.'
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')

if __name__ == '__main__':
    main()
