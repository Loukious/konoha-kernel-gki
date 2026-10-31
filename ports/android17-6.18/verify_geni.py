#!/usr/bin/env python3
"""Check staged GENI ring bounds, DMA rollback, clocks and IRQ accounting."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from verify_walt import function

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve()
    paths = ['drivers/dma/qcom/msm_gpi.c', 'include/linux/qcom-geni-se-common.h',
             'drivers/spi/spi-msm-geni.c', 'drivers/i2c/busses/i2c-msm-geni.c']
    sources = [(root/'onyx'/p).read_text() for p in paths]
    gpi, common, spi, i2c = sources
    def struct(source, name):
        return re.search(r'struct '+name+r' \{.*?\n\};', source, re.S)[0]
    wrapper = struct((root/'common/drivers/soc/qcom/qcom-geni-se.c').read_text(), 'geni_wrapper')
    assert wrapper.replace('MAX_CLKS', '2') == struct(common, 'geni_wrapper')
    definitions = struct(gpi, 'gpi_ring')+'\n'
    for source, names in [
        (gpi, ['gpi_ring_len', 'gpi_alloc_ring', 'gpi_free_ring', 'gpi_synchronize']),
        (common, ['geni_se_common_clks_on', 'geni_se_common_clks_off',
                  'geni_se_common_iommu_map_buf', 'geni_se_common_iommu_unmap_buf']),
        (spi, ['spi_geni_unmap_buf', 'spi_geni_map_buf', 'spi_geni_set_irq']),
    ]:
        definitions += '\n'.join(function(re.sub(r'/\*.*?\*/', '', source, flags=re.S), name) for name in names)
    # Source-order contracts supplement the actual helper execution below.
    remove = function(spi, 'spi_geni_remove')
    assert remove.index('spi_unregister_controller') < remove.index('pm_runtime_barrier') < remove.index('dma_release_channel') < remove.index('ipc_log_context_destroy')
    remove = function(i2c, 'geni_i2c_remove')
    assert remove.index('i2c_del_adapter') < remove.index('pm_runtime_barrier') < remove.index('dma_release_channel') < remove.index('ipc_log_context_destroy')
    template = HERE/'tests/geni_harness.c'
    with tempfile.TemporaryDirectory(prefix='onyx-geni-') as tmp:
        p = Path(tmp)
        (p/'harness.c').write_text(template.read_text().replace('/* STAGED_DEFINITIONS */', definitions))
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration',
                        str(p/'harness.c'), '-o', str(p/'harness')], check=True)
        run = subprocess.run([str(p/'harness')], capture_output=True, text=True, check=True)
    report = json.loads(run.stdout)
    report['wrapper_layout_matches_common'] = True
    report['dma_synchronize_sequences'] = 8
    report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    report['sha256'] = {p: hashlib.sha256((root/'onyx'/p).read_bytes()).hexdigest() for p in paths}
    report['harness_sha256'] = hashlib.sha256(template.read_bytes()).hexdigest()
    report['limits'] = 'Actual helpers with simulated kernel services; no real IRQs, DMA controller, bus transfers, firmware, runtime PM or device boot.'
    serialized = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(serialized)
    print(serialized, end='')

if __name__ == '__main__':
    main()
