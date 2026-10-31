#!/usr/bin/env python3
"""Exercise staged display-support helpers with sanitizer instrumentation."""
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
    assert match, name
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
    base = args.workspace / args.source_dir
    used = {}
    def read(path):
        data = (base / path).read_bytes()
        used[path] = hashlib.sha256(data).hexdigest()
        return data.decode()
    mmrm = 'qcom/opensource/mmrm-driver/driver/src/'
    driver = read(mmrm + 'msm_mmrm.c')
    parse = read(mmrm + 'mmrm_res_parse.c')
    software = read(mmrm + 'mmrm_clk_rsrc_mgr_sw.c')
    ext = read('qcom/opensource/mm-drivers/msm_ext_display/src/msm_ext_display.c')
    contracts = []
    def check(name, condition):
        assert condition, name
        contracts.append(name)
    probe = function(driver, 'msm_mmrm_probe_init')
    check('MMRM global state publishes only after manager and sysfs initialization',
          probe.index('mmrm_init(data)') < probe.index('sysfs_create_group') < probe.index('drv_data = data;') < probe.index('smp_store_release'))
    check('MMRM client storage keeps successful provider resident',
          '__module_get(THIS_MODULE)' in probe and '.suppress_bind_attrs = true' in driver)
    check('MMRM manager allocation uses overflow-safe count allocation',
          'kcalloc' in function(software, 'mmrm_init_sw_clk_mgr'))
    check('MMRM null manager is checked before dereference',
          function(software, 'mmrm_destroy_sw_clk_mgr').index('if (!sw_clk_mgr)') < function(software, 'mmrm_destroy_sw_clk_mgr').index('sinfo ='))
    check('MMRM active voltage table exactly matches fixed array size',
          'num_corners != MMRM_VDD_LEVEL_MAX' in function(parse, 'mmrm_load_mm_rail_corner_table'))
    check('MMRM missing throttle client cannot select default row zero',
          'tbl_entry_id = sinfo->tot_clk_clients' in function(software, 'mmrm_init_sw_clk_mgr') and
          'tbl_entry_id >= sinfo->tot_clk_clients' in function(software, 'mmrm_sw_throttle_low_priority_client'))
    probe = function(ext, 'msm_ext_disp_probe')
    check('External display locks and list exist before child devices can probe',
          probe.index('mutex_init') < probe.index('platform_set_drvdata') < probe.index('of_platform_populate') and
          probe.index('INIT_LIST_HEAD') < probe.index('platform_set_drvdata'))
    check('External display failed child population drains children before extcon withdrawal',
          probe.index('of_platform_depopulate') < probe.index('msm_ext_disp_extcon_unregister'))
    check('External display extcon unwind touches only registered devices',
          'test_and_clear_bit' in function(ext, 'msm_ext_disp_extcon_unregister'))
    check('External codec callback provider remains resident',
          '__module_get(THIS_MODULE)' in probe and '.suppress_bind_attrs = true' in ext)
    groups = {
        'display_support_mmrm': [
            (driver, ['sysfs_get_param', 'msm_mmrm_probe_init']),
            (parse, ['mmrm_count_clk_clients_frm_dt', 'mmrm_load_mm_rail_corner_table']),
            (software, ['mmrm_sw_get_req_level', 'mmrm_sw_get_max_crm_rate']),
        ],
        'display_support_ext': [
            (ext, ['msm_ext_disp_codec_valid', 'msm_ext_disp_extcon_register',
                   'msm_ext_disp_extcon_unregister', 'msm_ext_disp_probe']),
        ],
    }
    results, harnesses = {}, {}
    with tempfile.TemporaryDirectory(prefix='onyx-display-support-tests-') as temp:
        root = Path(temp)
        for name, parts in groups.items():
            path = HERE / 'tests' / (name + '.c')
            data = path.read_bytes()
            harnesses[path.name] = hashlib.sha256(data).hexdigest()
            definitions = '\n'.join(function(source, name) for source, names in parts for name in names)
            output = root / (name + '.c')
            output.write_text(data.decode().replace('/* FUNCTIONS */', definitions))
            binary = root / name
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration',
                            str(output), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
            assert not run.returncode, run.stdout + run.stderr
            results.update(json.loads(run.stdout))
    report = {'tests': results, 'source_contracts': contracts,
              'source_sha256': used, 'harness_sha256': harnesses,
              'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
              'limits': 'Extracted source bodies with simulated allocator, OF, clock, sysfs and platform services; no real driver binding, DRM atomic commit, display firmware, kernel callback concurrency, suspend/resume or hardware validation.'}
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')

if __name__ == '__main__':
    main()
