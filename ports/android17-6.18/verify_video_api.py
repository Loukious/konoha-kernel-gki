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
prefix = 'qcom/opensource/video-driver/driver/vidc/src/'
selection = {
    'msm_vidc_driver.c': ['msm_vidc_v4l2_fh_init', 'msm_vidc_v4l2_fh_deinit'],
    'msm_vidc_v4l2.c': ['msm_v4l2_open', 'msm_v4l2_close'],
    'resources.c': ['devm_pd_release', 'devm_pd_get', 'msm_vidc_attach_opp_domains', 'devm_pm_runtime_put_sync', 'devm_pm_runtime_get_sync'],
}

def extract(text, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', text)
    assert m, name
    start = text.index('{', m.start())
    depth, pos = 1, start + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[m.start():pos] + '\n'

source = '\n'.join(extract((root / (prefix + path)).read_text(), name) for path, names in selection.items() for name in names)
controls = {
    'missing_fh_publication': ('v4l2_fh_add(&inst->fh, filp);', '(void)filp;'),
    'missing_fh_withdrawal': ('v4l2_fh_del(&inst->fh, filp);', '(void)filp;'),
    'missing_fh_exit': ('v4l2_fh_exit(&inst->fh);', '(void)inst;'),
    'missing_required_opp': ('PD_FLAG_REQUIRED_OPP | PD_FLAG_DEV_LINK_ON', 'PD_FLAG_DEV_LINK_ON'),
    'null_domain_result': ('devm_pm_domain_attach_list(dev, &data, &domains)', 'devm_pm_domain_attach_list(dev, &data, NULL)'),
    'masked_opp_error': ('if (rc < 0)\n\t\treturn rc;', 'if (rc < 0)\n\t\treturn 0;'),
    'missing_domain_count': ('return rc == count ? 0 : -ENODEV;', 'return 0;'),
    'accepted_error_pointer': ('if (IS_ERR_OR_NULL(pd))', 'if (!pd)'),
    'leaked_failed_resume': ('pm_runtime_resume_and_get(dev)', 'pm_runtime_get_sync(dev)'),
}
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {prefix + path: hashlib.sha256((root / (prefix + path)).read_bytes()).hexdigest() for path in selection}, 'harness_sha256': {'video_api.c': hashlib.sha256((H / 'tests/video_api.c').read_bytes()).hexdigest()}}
with tempfile.TemporaryDirectory(prefix='video-api-test-') as tmp:
    for name in ['positive', *controls]:
        body = source
        if name in controls:
            old, new = controls[name]
            assert old in body, name
            body = body.replace(old, new)
        code = (H / 'tests/video_api.c').read_text().replace('/* SOURCE */', body)
        path = Path(tmp) / 'video_api.c'
        path.write_text(code)
        binary = path.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-DEPROBE_DEFER=517', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(path), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['file_power_ownership'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Production file open/close, FH init/deinit and power ownership helpers with mocked V4L2/PM/devres services. Does not cover complete session/firmware/device teardown, real VFS or power management.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
