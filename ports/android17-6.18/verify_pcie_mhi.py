#!/usr/bin/env python3
"""Exercise extracted PCIe/MHI boundary and resource paths with ASan/UBSan."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(src, name):
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    m = re.search(r'(?m)^(?:static )?(?:inline )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', src)
    if not m:
        raise ValueError(name)
    end, depth = m.end(), 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[m.start():end] + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--source-dir', default='onyx')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    base = args.workspace / args.source_dir
    sources = json.loads((HERE / 'pcie-mhi-sources.json').read_text())
    files = {Path(p).name: (base / p).read_text() for p in sources}
    groups = {
        'pdc': [('pcie-pdc.c', n) for n in ['pdc_cfg_irq', 'pcie_pdc_cfg_irq', 'qcom_pcie_pdc_probe', 'qcom_pcie_pdc_remove']],
        'pci_config': [('pci-msm.c', n) for n in ['msm_pcie_span_valid', 'msm_pcie_config_valid', 'msm_pcie_oper_conf']],
        'pci_irq': [('pci-msm.c', n) for n in ['msm_pcie_irq_deinit', 'msm_pcie_irq_init']],
        'mhi_registers': [('main.c', n) for n in ['mhi_reg_range_valid', 'mhi_read_reg', 'mhi_read_reg_field', 'mhi_poll_reg_field', 'mhi_write_reg', 'mhi_write_reg_field']] + [('internal.h', 'is_valid_ring_ptr'), ('init.c', 'mhi_alloc_aligned_ring'), ('misc.c', 'mhi_get_capability_offset')],
        'mhi_images': [('boot.c', n) for n in ['mhi_free_bhie_table', 'mhi_alloc_bhie_table', 'mhi_firmware_copy']],
        'mhi_sfr': [('misc.c', n) for n in ['mhi_process_sfr', 'mhi_find_next_file_offset', 'mhi_dump_sfr']],
        'mhi_config': [('init.c', n) for n in ['parse_ch_cfg', 'parse_ev_cfg', 'parse_config']],
        'mhi_lifetime': [('main.c', 'mhi_destroy_device')],
        'mhi_private': [('misc.c', n) for n in ['mhi_misc_sysfs_create', 'mhi_misc_sysfs_destroy', 'mhi_misc_register_controller', 'mhi_misc_unregister_controller']],
        'msi': [('pci-msm-msi.c', n) for n in ['msm_msi_client_free', 'msm_msi_domain_prepare', 'msm_msi_irq_domain_alloc', 'msm_msi_irq_domain_free', 'msm_msi_qgic_irq_setup']],
    }
    results = {}
    with tempfile.TemporaryDirectory(prefix='onyx-pcie-mhi-tests-') as tmp:
        for name, funcs in groups.items():
            src = (HERE / 'tests' / f'{name}.c').read_text().replace('/* FUNCTIONS */', '\n'.join(function(files[f], n) for f, n in funcs))
            if name == 'mhi_config':
                public = files['mhi.h']
                declarations = []
                for kind, typ in [('enum', 'mhi_ch_type'), ('enum', 'mhi_er_data_type'), ('enum', 'mhi_er_priority'), ('enum', 'mhi_db_brst_mode'), ('struct', 'mhi_channel_config'), ('struct', 'mhi_event_config'), ('struct', 'mhi_controller_config')]:
                    match = re.search(r'(?m)^' + kind + ' ' + typ + r' \{.*?^\};', public, re.S | re.M)
                    if not match:
                        raise ValueError(typ)
                    declarations.append(match.group())
                repo = args.workspace / 'baseline/kernel/xiaomi/onyx-wlan'
                revision = '6cc986b6e08783b8a14b155c1531c86959a1398a'
                rel = 'platform/cnss2/pci.c'
                blob = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', revision + ':' + rel], text=True).strip()
                consumer = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', blob], text=True)
                start = consumer.index('static const struct mhi_channel_config cnss_mhi_channels[]')
                end = consumer.index('static struct cnss_pci_reg ce_src[]')
                src = src.replace('/* PUBLIC_TYPES */', '\n'.join(declarations)).replace('/* CONSUMER_CONFIGS */', consumer[start:end])
                results['consumer_pin'] = {'project': str(repo.relative_to(args.workspace)), 'revision': revision, 'path': rel, 'blob': blob}
            path = Path(tmp) / f'{name}.c' 
            path.write_text(src)
            exe = Path(tmp) / name
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-sign-compare', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie', '-I', str(HERE / 'tests'), str(path), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
            if run.returncode:
                raise RuntimeError(name + ': ' + run.stderr)
            results[name] = json.loads(run.stdout)
    results['source_sha256'] = {p: hashlib.sha256((base / p).read_bytes()).hexdigest() for p in sources}
    results['harness_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((HERE / 'tests').iterdir()) if p.name in ['mhi_private.c', 'msi.c', 'mhi_config.c', 'mhi_lifetime.c', 'mhi_sfr.c', 'mhi_images.c', 'mhi_registers.c', 'pci_irq.c', 'pci_config.c', 'pdc.c', 'mock.h']}
    pci = files['pci-msm.c']
    init = files['init.c']
    remove = function(pci, 'msm_pcie_remove')
    register = function(init, 'mhi_register_controller')
    unregister = function(init, 'mhi_unregister_controller')
    assert remove.index('msm_pcie_sysfs_exit') < remove.index('msm_pcie_irq_deinit') < remove.index('pci_stop_root_bus') < remove.index('msm_pcie_release_resources')
    assert pci.index('INIT_KFIFO(pcie_dev->aer_fifo)') < pci.index('ret = msm_pcie_irq_init(pcie_dev)')
    assert 'return PTR_ERR(pcie_dev->crm_dev)' in function(pci, 'msm_pcie_cesta_init')
    assert 'ret = pci_host_probe(bridge)' in function(pci, 'msm_pcie_enumerate')
    assert unregister.index('mhi_misc_sysfs_destroy') < unregister.index('mhi_deinit_free_irq') < unregister.index('device_for_each_child') < unregister.index('mhi_misc_unregister_controller') < unregister.index('vfree(mhi_cntrl->mhi_chan)')
    assert register.index('mhi_misc_register_controller') < register.index('device_add')
    assert function(pci, 'pcie_exit').index('platform_driver_unregister') < function(pci, 'pcie_exit').index('destroy_workqueue')
    assert 'pci_unregister_driver' in function(pci, 'pcie_exit')
    assert '.remove = qcom_pcie_pdc_remove' in files['pcie-pdc.c']
    results['source_contracts'] = {'fifo_initialized_before_irq': True, 'remove_drains_before_mapping_release': True, 'mhi_clients_removed_before_private_free': True, 'checked_enumeration_and_crm_errors': True}
    results['limits'] = 'Extracted actual driver functions with mocked Linux memory, IRQ, device and DMA services. Covers framing, bounds and resource contracts; does not validate live IRQ/PM/PCI semantics, CESTA execution, firmware execution, WLAN operation or phone boot.'
    out = json.dumps(results, indent=2) + '\n'
    print(out, end='')
    if args.output:
        args.output.write_text(out)


if __name__ == '__main__':
    main()
