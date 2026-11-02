#!/usr/bin/env python3
"""Compile exact staged pointer and event-walk functions with behavioral mocks."""
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
source = args.workspace / 'onyx/drivers/dma/qcom/msm_gpi.c'
text = source.read_text()

def function(name):
    import re
    match = re.search(r'^static[^\n]*\b' + name + r'\([^;{]*\)\s*\{', text, re.M)
    assert match, name
    start = match.start()
    brace = match.end() - 1
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

names = ['to_physical', 'to_virtual', 'gpi_ring_ptr_lsb', 'gpi_read_ev_ring_rp',
         'gpi_process_events', 'gpi_mark_stale_events']
helper = '\n'.join(function(name) for name in names)
(HERE / 'helper.inc').write_text(helper)
controls = {
    'old_full_base_subtraction': ('u32 offset = addr - lower_32_bits(ring->phys_addr);',
                                 'phys_addr_t offset = (phys_addr_t)addr - ring->phys_addr;'),
    'missing_alignment_check': (' || offset % ring->el_size', ''),
    'one_past_end_allowed': ('offset >= ring->len', 'offset > ring->len'),
    'missing_zero_size_check': (' || !ring->el_size', ''),
    'broken_event_rp_conversion': ('void *rp = gpi_ring_ptr_lsb(ring, lsb);',
                                   'void *rp = to_virtual(ring, lsb);'),
}
report = {'staged_source': str(source),
          'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'functions': names, 'negative_controls': {}}
with tempfile.TemporaryDirectory(prefix='gpi-ring-check-') as temp:
    temp = Path(temp)
    (temp / 'helper_test.c').write_bytes((HERE / 'helper_test.c').read_bytes())
    for name, code in [('exact', helper)] + [
        (name, helper.replace(old, new)) for name, (old, new) in controls.items()]:
        (temp / 'helper.inc').write_text(code)
        binary = temp / name
        compiler = args.workspace / 'prebuilts/clang/host/linux-x86/clang-r596125/bin/clang'
        compiled = subprocess.run([str(compiler), '-std=gnu11', '-Wall', '-Wextra',
            '-Werror', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
            '-g', '-fsanitize=address,undefined',
            '-fno-sanitize-recover=all', str(temp / 'helper_test.c'), '-o', str(binary)],
            capture_output=True, text=True)
        assert compiled.returncode == 0, compiled.stderr
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        if name == 'exact':
            assert result.returncode == 0, result.stderr
            report['result'] = result.stdout.strip()
            print(report['result'])
        else:
            assert result.returncode != 0, name
            report['negative_controls'][name] = {'rejected': True, 'exit_code': result.returncode}
    report['status'] = 'PASS'
    report['limits'] = 'Exact pointer conversion/event-walk functions; mocked MMIO/callbacks. No hardware DMA address captured yet.'
    (HERE / 'helper-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: five compiled negative controls rejected')
