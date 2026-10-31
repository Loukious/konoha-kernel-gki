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
source_path = 'qcom/opensource/video-driver/driver/vidc/src/msm_vidc_memory_ext.c'

def extract(body, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', body)
    assert m, name
    pos = body.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (body[pos] == '{') - (body[pos] == '}')
        pos += 1
    return body[m.start():pos] + '\n'

source = '\n'.join(extract((root / source_path).read_text(), name) for name in ['is_non_secure_buffer', 'msm_vidc_dma_buf_attach_ext', 'msm_vidc_memory_free_ext', 'msm_vidc_memory_alloc_ext', 'msm_vidc_memory_map_ext', 'msm_vidc_memory_unmap_ext', 'msm_vidc_memory_alloc_map_ext', 'msm_vidc_memory_unmap_free_ext'])
controls = {
    'missing_heap_put': ('dma_heap_put(heap);', '(void)0;'),
    'ignored_cpu_access_failure': ('if (rc)\n\t\t\tgoto error;', 'if (false)\n\t\t\tgoto error;'),
    'missing_failed_vmap_cpu_end': ('end_cpu_access:\n\tdma_buf_end_cpu_access(mem->dmabuf, DMA_BIDIRECTIONAL);', 'end_cpu_access:\n\t(void)0;'),
    'missing_mapping_failure_free': ('msm_vidc_memory_free_ext(core, mem);\n\t\treturn rc;', '(void)0;\n\t\treturn rc;'),
    'missing_attachment_rollback': ('call_mem_op(core, dma_buf_detach, core, mem->dmabuf, attach);', '(void)0;'),
    'missing_allocated_buffer_free': ('dma_heap_buffer_free(mem->dmabuf);', '(void)0;'),
    'missing_busy_allocation_guard': ('!mem->size || mem->dmabuf || mem->refcount', '!mem->size'),
    'truncated_alignment': ('ALIGN((size_t)mem->size, SZ_4K)', 'ALIGN(mem->size, SZ_4K)'),
}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {source_path: sha(root / source_path)}, 'harness_sha256': {'video_memory.c': sha(H / 'tests/video_memory.c')}, 'common_contract_sha256': {'drivers/dma-buf/dma-heap.c': sha(a.workspace / 'common/drivers/dma-buf/dma-heap.c')}}
with tempfile.TemporaryDirectory(prefix='video-memory-test-') as tmp:
    for name in ['positive', *controls]:
        current = source
        if name in controls:
            before, after = controls[name]
            assert before in current, name
            current = current.replace(before, after)
        f = Path(tmp) / 'memory.c'
        f.write_text((H / 'tests/video_memory.c').read_text().replace('/* SOURCE */', current))
        binary = f.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(f), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['allocator_lifecycle'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Actual extended DMA-BUF allocation/map/unmap/free/attach functions; DMA heap, CPU access, vmap, security and attachment services modeled. Validates references, failure unwind, secure/nonsecure paths and size alignment, not kernel DMA scheduling or firmware security transitions.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
