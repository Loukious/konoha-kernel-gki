import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent
p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
p.add_argument('--source-dir', default='onyx')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = a.workspace / a.source_dir
prefix = 'qcom/opensource/video-driver/driver/vidc/'
source_path = prefix + 'src/venus_hfi_queue.c'
header_path = prefix + 'inc/venus_hfi_queue.h'
core_path = prefix + 'inc/msm_vidc_core.h'
internal_path = prefix + 'inc/msm_vidc_internal.h'

def extract(body, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', body)
    assert m, name
    pos = body.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (body[pos] == '{') - (body[pos] == '}')
        pos += 1
    return body[m.start():pos] + '\n'

body = (root / source_path).read_text()
source = '\n'.join(extract(body, name) for name in ['__set_queue_hdr_defaults', 'venus_hfi_queue_deinit', 'venus_hfi_reset_queue_header', 'venus_hfi_iommu_map_registers', 'venus_hfi_queue_init'])
header = (root / header_path).read_text()
constants = header[header.index('#define HFI_MASK'):header.index('struct msm_vidc_core;')]
constants = re.search(r'^#define VIDC_IFACEQ_MAX_PKT_SIZE\s+.*$', (root / internal_path).read_text(), re.M).group() + '\n' + constants
layout = '\n'.join(re.search(r'struct ' + name + r' \{.*?\n\};', (root / core_path).read_text(), re.S).group() for name in ['msm_vidc_mem_addr', 'msm_vidc_iface_q_info'])
controls = {
    'missing_failure_rollback': ('venus_hfi_queue_deinit(core);', '(void)0;'),
    'masked_native_error': ('return rc;', 'return -ENOMEM;'),
    'missing_synx_ownership': ('core->synx_queue_mapped = true;', '(void)0;'),
    'unowned_synx_unmap': ('if (core->synx_queue_mapped)', 'if (core->capabilities[SUPPORTS_SYNX_FENCE].value)'),
    'missing_queue_clear': ('core->iface_q_table.align_virtual_addr = NULL;', '(void)0;'),
    'missing_register_rollback': ('call_mem_op(core, iommu_unmap, core, &core->aon_reg.mem);', '(void)0;'),
    'missing_sfr_rollback': ('call_mem_op(core, memory_unmap_free, core, &core->sfr.mem);', '(void)0;'),
    'missing_mmap_rollback': ('call_mem_op(core, memory_unmap_free, core, &core->mmap_buf.mem);', '(void)0;'),
}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {x: sha(root / x) for x in [source_path, header_path, core_path, internal_path]}, 'harness_sha256': {'video_queues.c': sha(H / 'tests/video_queues.c')}}
with tempfile.TemporaryDirectory(prefix='video-queues-test-') as tmp:
    for name in ['positive', *controls]:
        current = source
        if name in controls:
            before, after = controls[name]
            assert before in current, name
            current = current.replace(before, after)
        f = Path(tmp) / 'queues.c'
        f.write_text((H / 'tests/video_queues.c').read_text().replace('/* SOURCE */', current).replace('/* LAYOUT */', layout).replace('/* CONSTANTS */', constants))
        binary = f.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(f), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['queue_lifecycle'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Actual queue initialization, register-map wrapper, reset and cleanup with native header constants/layouts. Allocation/IOMMU/SYNX services are models; covers all register-presence combinations and allocation failure points, preserved borrowed SYNX references, rollback and retry. No DMA/IOMMU hardware claim.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
