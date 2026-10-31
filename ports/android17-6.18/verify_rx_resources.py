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
ap.add_argument('workspace', type=Path)
ap.add_argument('--source-root', type=Path, default=H / 'private-source')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
source_name = 'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-rx-macro.c'
p = a.source_root / source_name
s = p.read_text()
headers = ['qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-registers.h',
           'qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc.h',
           'qcom/opensource/audio-kernel/include/bindings/qcom,lpass-cdc-clk-rsc.h']

def function(name):
    match = re.search(r'^static (?:int|void|bool) ' + name + r'\([^;{]*\{', s, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[match.start():end] + '\n'

def structure(name):
    return re.search(r'^struct ' + name + r' \{.*?^\};', s, re.M | re.S)[0] + '\n'

defines = []
continued = False
for line in s.splitlines():
    if line.startswith('#define ') or continued:
        defines.append(line)
        continued = line.endswith('\\')
layout = '\n'.join(defines) + '\n'
layout += '\n'.join(re.findall(r'^enum(?: \w+)? \{.*?^\};', s, re.M | re.S)) + '\n'
layout += '\n'.join(re.findall(r'^enum(?: \w+)? \{.*?^\};',
    (a.workspace / 'onyx' / headers[1]).read_text(), re.M | re.S)) + '\n'
layout += (a.workspace / 'onyx' / headers[0]).read_text() + '\n'
layout += (a.workspace / 'onyx' / headers[2]).read_text() + '\n'
layout += ''.join(structure(n) for n in ['rx_swr_ctrl_data', 'rx_swr_ctrl_platform_data',
             'lpass_cdc_rx_macro_idle_detect_config', 'lpass_cdc_rx_macro_priv'])
code = ''.join(function(n) for n in ['lpass_cdc_rx_macro_get_data',
            'lpass_cdc_rx_macro_core_vote', 'lpass_cdc_rx_macro_mclk_enable',
            'lpass_cdc_rx_macro_mclk_event', 'rx_swrm_clock'])
controls = [
    ('PM_failure_ignored', 'if (ret < 0)\n\t\t\treturn ret;', 'if (false)\n\t\t\treturn ret;'),
    ('failed_core_vote_leaked', 'ret = -ENOTSYNC;\n\t} else', 'return -ENOTSYNC;\n\t} else'),
    ('MCLK_count_overflow', 'enable && rx_priv->rx_mclk_users == INT_MAX', 'enable && false'),
    ('MCLK_rollback_owner_lost', 'rx_priv->failed_clock_owned = true;', 'rx_priv->failed_clock_owned = false;'),
    ('DAPM_duplicate_vote', 'if (rx_priv->dapm_mclk_enable)\n\t\t\tbreak;', 'if (false)\n\t\t\tbreak;'),
    ('SWR_vote_error_lost', 'if (ret)\n\t\t\tgoto out;\n\t\tvoted = true;',
                           'if (false)\n\t\t\tgoto out;\n\t\tvoted = true;'),
    ('SWR_count_overflow', 'enable && rx_priv->swr_clk_users == INT_MAX', 'enable && false'),
    ('SWR_incomplete_disable_published', 'if (!ret)\n\t\t\t\tret = cleanup;\n\t\t\tgoto vote;',
        'if (!ret)\n\t\t\t\tret = cleanup;\n\t\t\trx_priv->swr_clk_users = 0;\n\t\t\tgoto vote;'),
]
rejections = []
with tempfile.TemporaryDirectory(prefix='rx-resources-') as td:
    d = Path(td)
    (d / 'rx_resource_layout.inc').write_text(layout)
    fragment = d / 'rx_resource_functions.inc'
    fragment.write_text(code)
    command = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-misleading-indentation',
               '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I', str(d),
               str(H / 'tests/audio_rx_resources.c'), '-o', str(d / 'resources')]
    def run():
        build = subprocess.run(command, text=True, capture_output=True)
        assert build.returncode == 0, build.stderr
        return subprocess.run([str(d / 'resources')], text=True, capture_output=True, timeout=30,
            preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
    result = run()
    assert not result.returncode, result.stdout + result.stderr
    tests = json.loads(result.stdout)
    print(result.stdout.strip(), flush=True)
    if a.negative:
        for name, old, new in controls:
            assert old in code, name
            fragment.write_text(code.replace(old, new, 1))
            result = run()
            assert result.returncode, name
            rejections.append({'name': name, 'rejected': True, 'diagnostic': result.stderr[:500]})
            print('REJECTED', name, flush=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
report = {'status': 'PASS', 'tests': tests, 'negative_controls': rejections,
          'source_sha256': {source_name: sha(p)},
          'header_sha256': {n: sha(a.workspace / 'onyx' / n) for n in headers},
          'harness_sha256': {'audio_rx_resources.c': sha(H / 'tests/audio_rx_resources.c')},
          'verifier_sha256': {Path(__file__).name: sha(Path(__file__))},
          'limits': 'Actual core-vote, MCLK, DAPM and SWR clock functions and private layout; '
                    'real source/provider constants, mocked PM/clock/pinctrl/regmap/ALSA services. '
                    'Register failures can occur after a write. FS provider has a native void '
                    'interface. No hardware, kernel PM or full ALSA/SSR validation.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
