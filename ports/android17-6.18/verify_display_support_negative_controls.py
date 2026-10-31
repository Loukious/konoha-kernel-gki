#!/usr/bin/env python3
"""Confirm display-support regressions fail against their original pinned bodies."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import tempfile
from verify_display_support import function

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('workspace', type=Path)
parser.add_argument('--source-dir', default='onyx')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
items = {i['path']: i for i in json.loads((here / 'display-support-imports.json').read_text())}
mmrm = 'qcom/opensource/mmrm-driver/driver/src/'
ext = 'qcom/opensource/mm-drivers/msm_ext_display/src/msm_ext_display.c'
groups = {
    'display_support_mmrm': [
        (mmrm + 'msm_mmrm.c', ['sysfs_get_param', 'msm_mmrm_probe_init']),
        (mmrm + 'mmrm_res_parse.c', ['mmrm_count_clk_clients_frm_dt', 'mmrm_load_mm_rail_corner_table']),
        (mmrm + 'mmrm_clk_rsrc_mgr_sw.c', ['mmrm_sw_get_req_level', 'mmrm_sw_get_max_crm_rate']),
    ],
    'display_support_ext': [
        (ext, ['msm_ext_disp_codec_valid', 'msm_ext_disp_extcon_register',
               'msm_ext_disp_extcon_unregister', 'msm_ext_disp_probe']),
    ],
}
replaced = {'display_support_mmrm': 'mmrm_sw_get_req_level',
            'display_support_ext': 'msm_ext_disp_probe'}
report = {}
with tempfile.TemporaryDirectory(prefix='onyx-display-negative-controls-') as temp:
    directory = Path(temp)
    for group, parts in groups.items():
        definitions = []
        for path, names in parts:
            current = (args.workspace / args.source_dir / path).read_text()
            original = subprocess.check_output(['git', '-C', str(args.workspace / items[path]['project']),
                                                'cat-file', 'blob', items[path]['blob']], text=True)
            for name in names:
                source = original if name == replaced[group] else current
                definitions.append(function(source, name))
        harness = here / 'tests' / (group + '.c')
        code = harness.read_text().replace('/* FUNCTIONS */', '\n'.join(definitions))
        source = directory / (group + '.c')
        source.write_text(code)
        binary = directory / group
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', str(source), '-o', str(binary)], check=True)
        def no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        run = subprocess.run([str(binary)], capture_output=True, text=True,
                             timeout=30, preexec_fn=no_core)
        assert run.returncode and ('Assertion' in run.stderr or 'runtime error:' in run.stderr), run.stderr
        report[group] = {'original_function': replaced[group], 'expected_failure': True,
                         'exit': run.returncode, 'harness_sha256': hashlib.sha256(harness.read_bytes()).hexdigest()}
text = json.dumps(report, indent=2) + '\n'
if args.output:
    args.output.write_text(text)
print(text, end='')
