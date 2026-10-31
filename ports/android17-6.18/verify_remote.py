#!/usr/bin/env python3
"""Exercise staged QMI, SMP2P and GLINK bounds; inspect lifetime contracts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b' + name + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--source-dir', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve()/args.source_dir
    paths = ['drivers/soc/qcom/qmi_encdec.c', 'drivers/soc/qcom/qmi_interface.c',
             'drivers/soc/qcom/pdr_interface.c', 'drivers/soc/qcom/smp2p.c',
             'drivers/soc/qcom/smp2p_sleepstate.c', 'drivers/rpmsg/qcom_glink_memshare.c',
             'drivers/rpmsg/qcom_glink_smem.c', 'drivers/remoteproc/qcom_common.c']
    settings = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', (root/'platform_defconfig').read_text(), re.M))
    expected = {'CONFIG_RPROC_SSR_NOTIF_TIMEOUT': '20000',
                'CONFIG_PDR_INDICATION_NOTIF_TIMEOUT': '9000',
                'CONFIG_QCOM_PANIC_ON_NOTIF_TIMEOUT': 'y',
                'CONFIG_QCOM_PANIC_ON_PDR_NOTIF_TIMEOUT': 'y',
                'CONFIG_QCOM_RPROC_BOTH_DUMPS': 'y'}
    assert all(settings.get(k) == v for k, v in expected.items())
    sources = [(root/p).read_text() for p in paths]
    decode, qmi, pdr, smp, sleep, mem, ring, rproc = sources
    release = function(qmi, 'qmi_handle_release')
    assert release.index('disable_work_sync') < release.index('sock = qmi->sock') < release.index('qmi_sock_detach') < release.index('sock_release') < release.index('destroy_workqueue')
    ready = function(qmi, 'qmi_data_ready')
    assert ready.index('read_lock_bh') < ready.index('sk_user_data') < ready.index('queue_work') < ready.index('read_unlock_bh')
    detach = function(qmi, 'qmi_sock_detach')
    assert detach.index('write_lock_bh') < detach.index('sk_user_data = NULL') < detach.index('write_unlock_bh')
    reset = function(qmi, 'qmi_handle_net_reset')
    assert reset.index('qmi_sock_detach') < reset.index('sock_release')
    release = function(pdr, 'pdr_handle_release')
    assert release.index('stopping, true') < release.index('disable_work_sync') < release.index('qmi_handle_release') < release.index('kfree(ind)') < release.index('kfree(pds)')
    assert release.count('disable_work_sync') == 3
    work = function(pdr, 'pdr_indack_work')
    assert work.index('mutex_lock(&pdr->list_lock)') < work.index('list_first_entry_or_null') < work.index('list_del') < work.index('mutex_unlock(&pdr->list_lock)') < work.index('pdr->status(')
    alloc = function(pdr, 'pdr_handle_alloc')
    assert alloc.index('qmi_handle_init(&pdr->notifier_hdl') < alloc.index('qmi_add_lookup')
    remove = function(smp, 'qcom_smp2p_remove')
    assert remove.index('devm_free_irq') < remove.index('irq_domain_remove') < remove.index('kfree')
    probe = function(sleep, 'smp2p_sleepstate_probe')
    assert probe.index('devm_qcom_smem_state_get') < probe.index('smp2p_sleepstate_unregister_wakeup') < probe.index('devm_request_threaded_irq') < probe.index('smp2p_sleepstate_unregister_notifier')
    assert '#define AWAKE_BIT BIT(12)' in sleep
    for name in ['qcom_common_init', 'qcom_common_exit']:
        body = function(rproc, name)
        assert body.index('unregister_trace_android_vh_rproc_recovery') < body.index('tracepoint_synchronize_unregister')
    assert 'size < sizeof(*toc)' in function(rproc, 'qcom_minidump')
    assert 'minidump_id >= MAX_NUM_OF_SS' in function(rproc, 'qcom_minidump')
    definitions = re.search(r'struct smp2p_smem_item \{.*?\n\} __packed;', smp, re.S)[0]+'\n'
    definitions += re.search(r'struct qcom_glink_mem_entry \{.*?\n\};', mem, re.S)[0]+'\n'
    definitions += 'static LIST_HEAD(qcom_glink_mem_entries);\n'
    for source, names in [(decode, ['qmi_decode_basic_elem']),
                          (mem, ['qcom_glink_mem_entry_init', 'qcom_glink_mem_entry_free', 'qcom_glink_prepare_da_for_cpu']),
                          (ring, ['glink_smem_size_valid', 'glink_smem_rx_avail', 'glink_smem_rx_peek', 'glink_smem_rx_advance', 'glink_smem_tx_avail', 'glink_smem_tx_write_one']),
                          (smp, ['qcom_smp2p_notify_in'])]:
        definitions += '\n'.join(function(source, n) for n in names)
    template = HERE/'tests/remote_harness.c'
    with tempfile.TemporaryDirectory(prefix='onyx-remote-') as tmp:
        p = Path(tmp)
        (p/'harness.c').write_text(template.read_text().replace('/* STAGED_DEFINITIONS */', definitions))
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-fno-sanitize-recover=all',
                        '-Wno-address-of-packed-member', '-Werror=implicit-function-declaration',
                        str(p/'harness.c'), '-o', str(p/'harness')], check=True)
        run = subprocess.run([str(p/'harness')], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(run.stderr)
    report = json.loads(run.stdout)
    report['lifetime_source_contracts'] = 12
    report['rom_notification_policy'] = expected
    report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    report['sha256'] = {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}
    report['harness_sha256'] = hashlib.sha256(template.read_bytes()).hexdigest()
    report['limits'] = 'Actual helpers with simulated kernel services; source-order lifetime checks; no real IRQs, concurrent kernel scheduling, DMA cache maintenance, remote firmware or device boot.'
    serialized = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end='')

if __name__ == '__main__':
    main()
