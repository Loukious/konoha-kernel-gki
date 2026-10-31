from pathlib import Path
import argparse
import hashlib
import json
import re
import resource
import subprocess
import tempfile

H = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--source-root', type=Path, default=H / 'private-source')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
R = a.source_root / 'qcom/opensource/camera-kernel/drivers'

def fn(source, name):
    m = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;{]*\{', source, re.M)
    assert m, name
    end = m.end()
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end] + '\n'

mem = (R / 'cam_req_mgr/cam_mem_mgr.c').read_text()
compat = (R / 'cam_utils/cam_compat.c').read_text()
fragments = {
    'heaps.inc': fn(mem, 'cam_mem_mgr_put_dma_heaps') + fn(mem, 'cam_mem_mgr_get_dma_heaps'),
    'vmap.inc': fn(compat, 'cam_compat_util_get_dmabuf_va') + fn(compat, 'cam_compat_util_put_dmabuf_va'),
}
assert 'cam_mem_mgr_put_dma_heaps();' in fn(mem, 'cam_mem_mgr_deinit')
results = {}
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-memory-') as td:
    d = Path(td)
    for name, source in fragments.items():
        (d / name).write_text(source)
    commands = {}
    for group, fixture, defines in [('heaps', 'heaps', []), ('heaps_vm', 'heaps', ['-DCONFIG_ARCH_QTI_VM=1']), ('vmap', 'vmap', [])]:
        cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *defines,
               '-I', str(d), str(H / 'tests' / ('main_camera_' + fixture + '.c')),
               '-o', str(d / group)]
        commands[group] = cmd
        subprocess.run(cmd, check=True)
        run = subprocess.run([str(d / group)], check=True, text=True, capture_output=True, timeout=30)
        results[group] = json.loads(run.stdout)
        print(group, run.stdout.strip())
    if a.negative:
        mutations = [
            ('heap_reference_leak', 'heaps.inc', 'dma_heap_put(tbl.system_heap);', '(void)tbl.system_heap;', 'heaps'),
            ('heap_stale_reference', 'heaps.inc', 'tbl.system_heap = NULL;', '(void)tbl.system_heap;', 'heaps'),
            ('missing_required_heap_success', 'heaps.inc', 'tbl.system_heap ? PTR_ERR(tbl.system_heap) : -ENODEV', 'PTR_ERR(tbl.system_heap)', 'heaps'),
            ('vm_heap_reference_leak', 'heaps.inc', 'dma_heap_put(tbl.cam_device_heap);', '(void)tbl.cam_device_heap;', 'heaps_vm'),
            ('iomem_accepted_as_ram', 'vmap.inc', 'mapping.is_iomem || ', '', 'vmap'),
            ('failed_mapping_not_unwound', 'vmap.inc', 'dma_buf_vunmap_unlocked(dmabuf, &mapping);', '(void)dmabuf;', 'vmap'),
            ('failed_mapping_returns_address', 'vmap.inc', '*vaddr = 0;', '*vaddr = UINTPTR_MAX;', 'vmap'),
        ]
        for name, fragment, old, new, group in mutations:
            original = fragments[fragment]
            assert old in original, name
            (d / fragment).write_text(original.replace(old, new, 1))
            subprocess.run(commands[group], check=True, capture_output=True)
            run = subprocess.run([str(d / group)], capture_output=True, timeout=30,
                                 preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            assert run.returncode != 0, name
            controls.append({'name': name, 'rejected': True, 'returncode': run.returncode})
            (d / fragment).write_text(original)
            print('REJECTED', name)

report = {'status': 'PASS', 'groups': results, 'negative_controls': controls,
          'source_sha256': {str(p.relative_to(a.source_root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [R / 'cam_req_mgr/cam_mem_mgr.c', R / 'cam_utils/cam_compat.c']},
          'harness_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [H / 'tests/main_camera_heaps.c', H / 'tests/main_camera_vmap.c']},
          'limits': 'Extracted production functions with mocked DMA heap and mapping providers; no kernel or device execution.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
