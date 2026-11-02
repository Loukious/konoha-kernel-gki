#!/usr/bin/env python3
"""Compile the staged memory helpers with DMA/SCM fault injection and controls."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path)
args = parser.parse_args()
source = (HERE/'candidate.c').read_text()
start = source.index('struct msm_sharedmem {')
end = source.index('static const struct of_device_id')
helper = source[start:end]
assert (HERE/'helper.inc').read_text() == helper
controls = {
    'DMA_handle_as_physical_address': helper.replace('sg_phys(table.sgl)', 'shared->dma_addr', 1),
    'missing_registration_failure_cleanup': helper.replace('free_memory:\n\tsharedmem_free(shared);', 'free_memory:', 1),
    'guard_offset_omitted': helper.replace('vma->vm_pgoff = shared->guard_pages;', 'vma->vm_pgoff = 0;', 1),
    'freeing_remote_owned_pages': helper.replace('ret);\n\t\t\treturn;', 'ret);', 1),
    'wrong_DMA_free_handle': helper.replace('shared->allocation, shared->dma_addr);', 'shared->allocation, shared->info.mem[0].addr);', 1),
}
compiler = args.workspace/'prebuilts/clang/host/linux-x86/clang-r596125/bin/clang'
results = {}
with tempfile.TemporaryDirectory(prefix='onyx-sharedmem-') as temp:
    temp = Path(temp)
    (temp/'lifecycle_test.c').write_bytes((HERE/'lifecycle_test.c').read_bytes())
    for name, code in {'exact': helper, **controls}.items():
        assert name == 'exact' or code != helper, name
        (temp/'helper.inc').write_text(code)
        output = temp/name
        subprocess.run([str(compiler), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(temp/'lifecycle_test.c'), '-o', str(output)], check=True)
        run = subprocess.run([str(output)], cwd=temp, capture_output=True, text=True, timeout=15)
        assert (run.returncode == 0) == (name == 'exact'), (name, run.stdout, run.stderr)
        results[name] = {'exit_code': run.returncode, 'stdout': run.stdout.strip(),
                         'stderr': run.stderr[:1200]}
        print('PASS:', name, 'accepted' if name == 'exact' else 'regression rejected')
report = {'status': 'PASS', 'checks': results,
          'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in [HERE/'candidate.c', HERE/'helper.inc', HERE/'lifecycle_test.c']},
          'limits': 'Exact staged source helpers with mocked UIO, DMA and SCM; live allocation, secure sharing and modem completion still need hardware validation.'}
(HERE/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
