#!/usr/bin/env python3
"""Exercise staged charging and PMIC/UCSI wire bounds with simulated kernel services."""
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
    match = re.search(r'(?m)^(?:static\s+)?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source)
    if not match:
        raise ValueError(name)
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve() / args.package
    base = 'drivers/power/supply/mca/mca_common/'
    paths = [base + name + '.c' for name in ['mca_sysfs', 'mca_event', 'mca_qcom_smem', 'mca_adsp_glink']]
    paths += ['drivers/soc/qcom/qti_pmic_glink.c', 'drivers/usb/typec/ucsi/ucsi_qti_glink.c']
    sources = [(root / path).read_text() for path in paths]
    sysfs, event, smem, adsp, pmic, ucsi = sources
    groups = [
        (sysfs, ['mca_sysfs_create_files', 'mca_sysfs_remove_files', 'mca_sysfs_create_link_group']),
        (event, ['mca_event_report_uevent', 'mca_event_report_multiple_uevent']),
        (smem, ['mca_smem_get_battery_info', 'get_smem_battery_info', 'get_smem_battery_verify_result', 'get_smem_battery_cell_sn']),
        (adsp, ['mca_adsp_glink_callback']),
        (pmic, ['pmic_glink_rpmsg_callback']),
        (ucsi, ['ucsi_callback']),
    ]
    definitions = '\n\n'.join(function(source, name) for source, names in groups for name in names)
    structures = [('pmic_glink_hdr', (root/'include/linux/soc/qcom/qti_pmic_glink.h').read_text()),
                  ('mca_smem_battery_info', smem), ('pmic_glink_buf', pmic)]
    structures += [(name, adsp) for name in ['mca_adsp_glink_resp_msg', 'mca_adsp_glink_notify_msg']]
    structures += [(name, ucsi) for name in ['ucsi_read_buf_resp_msg', 'ucsi_write_buf_resp_msg', 'ucsi_notify_ind_msg']]
    structs = '\n'.join(re.search(r'struct '+name+r'\s*\{.*?\n\}[^;]*;', source, re.S)[0] for name, source in structures)
    # These checks document lifecycle ordering; they do not simulate RCU or device-core teardown.
    unregister = function(pmic, 'pmic_glink_unregister_client')
    assert unregister.index('idr_remove') < unregister.index('synchronize_srcu') < unregister.index('kfree(client)')
    remove = function(pmic, 'pmic_glink_rpmsg_remove')
    assert remove.index('rx_enabled = false') < remove.index('flush_workqueue')
    stop = function(adsp, 'mca_adsp_stop')
    assert stop.index('rcu_assign_pointer') < stop.index('synchronize_srcu') < stop.index('pmic_glink_unregister_client') < stop.index('destroy_workqueue')
    stop = function(ucsi, 'ucsi_qti_stop')
    assert stop.index('stopping = true') < stop.index('mutex_lock(&udev->io_lock)') < stop.index('pmic_glink_unregister_client') < stop.index('cancel_work_sync(&udev->state_work)') < stop.index('ucsi_destroy')
    template = HERE/'tests/charging_harness.c'
    with tempfile.TemporaryDirectory(prefix='onyx-charging-') as tmp:
        p = Path(tmp)
        (p/'harness.c').write_text(template.read_text().replace('/* STAGED_STRUCTS */', structs).replace('/* STAGED_DEFINITIONS */', definitions))
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-Werror=implicit-function-declaration', str(p/'harness.c'), '-o', str(p/'harness')], check=True)
        run = subprocess.run([str(p/'harness')], capture_output=True, text=True, check=True)
    report = json.loads(run.stdout)
    report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    report['lifecycle_source_contracts'] = 4
    report['source_sha256'] = {path: hashlib.sha256((root/path).read_bytes()).hexdigest() for path in paths}
    report['harness_sha256'] = hashlib.sha256(template.read_bytes()).hexdigest()
    report['limits'] = 'Extracted driver helpers with simulated kernel services; no hardware, live RCU/SRCU, GLINK firmware, UCSI negotiation or device boot.'
    serialized = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end='')


if __name__ == '__main__':
    main()
