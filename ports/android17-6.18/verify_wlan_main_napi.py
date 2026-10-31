#!/usr/bin/env python3
"""Exercise the ported production NAPI ownership functions under fault injection."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

H = Path(__file__).resolve().parent

def function(path, name):
    text = (S / path).read_text()
    match = re.search(r'(?m)^(?:static )?(?:inline )?(?:struct \w+ \*|void|int|QDF_STATUS)\s*\n?' + re.escape(name) + r'\([^;]*?\)\n\{', text)
    assert match, (path, name)
    start = match.start()
    brace = text.index('{', match.start())
    depth = 1
    pos = brace + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos] + '\n'

p = argparse.ArgumentParser()
p.add_argument('workspace', type=Path)
p.add_argument('--source-dir', default='onyx')
p.add_argument('--negative', action='store_true')
p.add_argument('--output', type=Path, default=H / 'napi-validation.json')
a = p.parse_args()
S = a.workspace / a.source_dir
sources = [
    ('qca-wifi-host-cmn/qdf/linux/src/i_qdf_net_if.h', '__qdf_net_if_create_dummy_if'),
    ('qca-wifi-host-cmn/qdf/linux/src/i_qdf_net_if.h', '__qdf_net_if_free_dummy_if'),
    ('qca-wifi-host-cmn/qdf/inc/qdf_net_if.h', 'qdf_net_if_create_dummy_if'),
    ('qca-wifi-host-cmn/qdf/inc/qdf_net_if.h', 'qdf_net_if_free_dummy_if'),
    ('qca-wifi-host-cmn/hif/src/hif_napi.c', 'hif_rxthread_napi_poll'),
    ('qca-wifi-host-cmn/hif/src/hif_napi.c', 'hif_init_rx_thread_napi'),
    ('qca-wifi-host-cmn/hif/src/hif_napi.c', 'hif_deinit_rx_thread_napi'),
    ('qca-wifi-host-cmn/hif/src/hif_napi.c', 'hif_napi_create'),
    ('qca-wifi-host-cmn/hif/src/hif_napi.c', 'hif_napi_destroy'),
    ('qca-wifi-host-cmn/hif/src/hif_exec.c', 'hif_exec_napi_kill'),
    ('qca-wifi-host-cmn/hif/src/hif_exec.c', 'hif_exec_napi_create'),
    ('qcacld-3.0/components/dp/core/src/wlan_dp_rx_thread.c', 'dp_rx_tm_thread_napi_poll'),
    ('qcacld-3.0/components/dp/core/src/wlan_dp_rx_thread.c', 'dp_rx_tm_thread_napi_init'),
    ('qcacld-3.0/components/dp/core/src/wlan_dp_rx_thread.c', 'dp_rx_tm_thread_napi_deinit'),
    ('qcacld-3.0/components/dp/core/src/wlan_dp_rx_thread.c', 'dp_rx_tm_shutdown'),
]
production = '\n'.join(function(*item) for item in sources)
mock = (H / 'tests/wlan_main_napi_mock.h').read_text()
tests = (H / 'tests/wlan_main_napi.c').read_text()
controls = {
    'dummy_device_leak': ('free_netdev((struct net_device *)nif);', '(void)nif;'),
    'missing_rx_disable': ('qdf_napi_disable(&napii->rx_thread_napi);', '(void)napii;'),
    'lost_cpu_init_error': ('goto hnc_err;', 'rc = 0; goto hnc_err;'),
    'partial_allocation_hook_leak': ('if (cpu_ready)\n\t\thif_napi_cpu_deinit(hif_ctx);', '(void)cpu_ready;'),
    'live_main_napi_destroyed': ('hif->napi_data.state & HIF_NAPI_CONF_UP', 'hif->napi_data.state == HIF_NAPI_CONF_UP'),
    'exec_allocation_leak': ('qdf_mem_free(ctx);\n\t\treturn NULL;', 'return NULL;'),
    'skip_started_partial_thread': ('!rx_tm_hdl->rx_thread[i]->task)', 'rx_tm_hdl->state == DP_RX_THREADS_INVALID)'),
}
report = {'suites': {}, 'negative_controls': {}, 'source_sha256': {path: hashlib.sha256((S / path).read_bytes()).hexdigest() for path, _ in sources}, 'limits': 'Production functions run with mocked kernel allocation, NAPI, and notifier APIs. Does not establish real concurrent IRQ/GRO/receive-thread quiescence or hardware behavior.'}
with tempfile.TemporaryDirectory(prefix='wlan-napi-test-') as tmp:
    tmp = Path(tmp)
    def run(name, code):
        src = tmp / (name + '.c')
        exe = tmp / name
        src.write_text(mock + '\n' + code + '\n' + tests)
        compile_result = subprocess.run(['cc', '-no-pie', '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', str(src), '-o', str(exe)], capture_output=True, text=True)
        assert compile_result.returncode == 0, compile_result.stderr
        return subprocess.run([str(exe)], capture_output=True, text=True)
    result = run('positive', production)
    assert result.returncode == 0, result.stderr
    report['suites']['napi_ownership'] = {'passed': True, 'output': result.stdout.strip()}
    if a.negative:
        for name, (old, new) in controls.items():
            assert old in production, name
            result = run(name, production.replace(old, new, 1))
            assert result.returncode != 0, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': result.returncode}
report['harness_sha256'] = {x.name: hashlib.sha256(x.read_bytes()).hexdigest() for x in (H / 'tests').glob('wlan_main_napi*')}
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['suites']))
