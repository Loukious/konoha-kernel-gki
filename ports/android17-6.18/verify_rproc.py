#!/usr/bin/env python3
"""Exercise the staged remoteproc bounds, rollback and firmware metadata helpers."""
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
    workspace = args.workspace.resolve()
    root = workspace/args.source_dir
    paths = ['drivers/remoteproc/qcom_pil_info.c', 'drivers/remoteproc/qcom_q6v5.c',
             'drivers/remoteproc/qcom_q6v5.h', 'drivers/remoteproc/qcom_sysmon.c',
             'drivers/remoteproc/qcom_q6v5_pas.c', 'drivers/soc/qcom/qcom_ramdump.c']
    pil, q6, q6h, sysmon, pas, ramdump = [(root/p).read_text() for p in paths]
    original = (workspace/'baseline/kernel/xiaomi/sm8735/drivers/remoteproc/qcom_q6v5_pas.c').read_text()
    def hardware(source):
        return source[source.index('static const struct adsp_data adsp_resource_init'):source.index('MODULE_DEVICE_TABLE(of, adsp_of_match);')]
    assert hardware(pas) == hardware(original), 'firmware descriptors or compatibility table changed'
    config = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', (root/'platform_defconfig').read_text(), re.M))
    for key, value in [('QCOM_Q6V5_COMMON','m'),('QCOM_Q6V5_PAS','m'),('QCOM_SYSMON','m'),('QCOM_RAMDUMP','m'),('RPROC_SYSMON_NOTIF_TIMEOUT','20000'),('RPROC_SYSMON_SHUTDOWN_NOTIF_TIMEOUT','10000')]:
        assert config['CONFIG_'+key] == value
    init = function(q6, 'qcom_q6v5_init')
    assert init.index('INIT_WORK') < init.index('devm_request_threaded_irq')
    assert 'IRQF_NO_AUTOEN' in init
    assert init.count('requested_irqs |=') == 5
    release = function(q6, 'q6v5_release_resources')
    assert release.index('devm_free_irq') < release.index('cancel_work_sync') < release.index('qmp_put')
    remove = function(sysmon, 'qcom_remove_sysmon_subdev')
    assert remove.index('blocking_notifier_chain_unregister') < remove.index('qmi_handle_release') < remove.index('devm_free_irq') < remove.index('timer_shutdown_sync') < remove.index('kfree')
    probe = function(sysmon, 'qcom_add_sysmon_subdev')
    assert 'goto release_qmi' in probe and probe.index('free_irq:') < probe.index('free_sysmon:')
    for name in ['sysmon_send_event', 'sysmon_request_shutdown']:
        body = function(sysmon, name)
        assert body.index('mutex_lock') < body.index('if (!sysmon->ept)') < body.index('rpmsg_send')
    remove = function(pas, 'adsp_remove')
    assert remove.index('rproc_del') < remove.index('qcom_q6v5_deinit') < remove.index('rproc_free')
    assert 'rproc_recovery_set_fn =' not in pas
    for name in ['adsp_driver_init', 'adsp_driver_exit']:
        body = function(pas, name)
        assert body.index('unregister_trace_android_vh_rproc_recovery_set') < body.index('tracepoint_synchronize_unregister')
    assert 'rproc->ops->start != adsp_start' in function(pas, 'rproc_recovery_set')
    assert 'devm_add_action_or_reset' in function(pas, 'adsp_setup_32b_dma_allocs')
    assert 'cdev_device_add' in function(ramdump, 'qcom_create_ramdump_device')
    assert 'cdev_device_del' in function(ramdump, 'qcom_destroy_ramdump_device')
    assert '__module_get(THIS_MODULE)' in function(ramdump, 'qcom_create_ramdump_device')
    assert 'module_put(THIS_MODULE)' in function(ramdump, 'ramdump_device_release')
    assert 'dev_coredumpm(dev, THIS_MODULE,' in ramdump
    for name in ['qcom_dump', 'qcom_elf_dump']:
        body = function(ramdump, name)
        assert 'check_add_overflow' in body and 'iounmap(ptr)' in body
    definitions = re.search(r'struct qcom_q6v5 \{.*?\n\};', q6h, re.S)[0]+'\n'
    for source, names in [(q6, ['q6v5_release_resources','qcom_q6v5_init','q6v5_copy_crash_message']),
                          (pas, ['adsp_range_offset','adsp_dump_address','adsp_da_to_va','disable_regulators','enable_regulators','adsp_assign_memory_region','adsp_unassign_memory_region']),
                          (pil, ['qcom_pil_info_store']),
                          (ramdump, ['qcom_ramdump_elf32_valid','qcom_fw_elf_dump'])]:
        definitions += '\n'.join(function(source, n) for n in names)
    definitions = definitions.replace('struct elf32_hdr', 'Elf32_Ehdr').replace('struct elf32_phdr', 'Elf32_Phdr')
    template = HERE/'tests/rproc_harness.c'
    with tempfile.TemporaryDirectory(prefix='onyx-rproc-') as tmp:
        p = Path(tmp)
        (p/'harness.c').write_text(template.read_text().replace('/* STAGED_DEFINITIONS */', definitions))
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                        '-Werror=implicit-function-declaration', str(p/'harness.c'), '-o', str(p/'harness')], check=True)
        run = subprocess.run([str(p/'harness')], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(run.stderr)
    report = json.loads(run.stdout)
    report['source_lifetime_contracts'] = 14
    report['firmware_descriptors_unchanged'] = True
    report['rom_sysmon_timeouts_ms'] = [20000,10000]
    report['sanitizers'] = ['AddressSanitizer','UndefinedBehaviorSanitizer']
    report['sha256'] = {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}
    report['harness_sha256'] = hashlib.sha256(template.read_bytes()).hexdigest()
    report['limits'] = 'Actual helpers with simulated kernel services; source-order lifetime checks; no concurrent kernel scheduling, secure-world assignment, real IRQs, firmware authentication, power sequencing, device boot or hardware validation.'
    serialized = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(serialized)
    print(serialized,end='')

if __name__ == '__main__':
    main()
