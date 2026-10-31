import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b' + name + r'\([^;]*?\)\n\{', source)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

H = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('workspace', type=Path)
parser.add_argument('--source-dir', default='onyx')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
W = args.workspace
P = 'platform/cnss_utils/cnss_plat_ipc_qmi.c'
source = (W / args.source_dir / P).read_text()
header = (W / args.source_dir / 'platform/cnss_utils/cnss_plat_ipc_service_v01.h').read_text()
header = re.sub(r'^#include.*\n', '', header, flags=re.M)
header = re.sub(r'^extern struct qmi_elem_info (.*)\[\];', r'struct qmi_elem_info \1[1];', header, flags=re.M)
header += '\n#define CNSS_PLAT_IPC_MAX_QMI_CLIENTS 1\ntypedef void (*cnss_plat_ipc_connection_update)(void *,bool);\n'
private = (W / args.source_dir / 'platform/cnss_utils/cnss_plat_ipc_qmi.h').read_text()
header += re.search(r'struct cnss_plat_ipc_daemon_config \{.*?\n\};', private, re.S).group(0)
structures = source[source.index('struct cnss_plat_ipc_file_data {'):source.index('static struct cnss_plat_ipc_qmi_svc_ctx plat_ipc_qmi_svc;')]
names = ['cnss_plat_ipc_init_file_data', 'cnss_plat_ipc_deinit_file_data',
         'cnss_plat_ipc_finish_file', 'cnss_plat_ipc_file_peer',
         'cnss_plat_ipc_qmi_file_upload', 'cnss_plat_ipc_qmi_file_upload_req_handler',
         'cnss_plat_ipc_qmi_file_download', 'cnss_plat_ipc_qmi_file_download_req_handler',
         'cnss_plat_ipc_qmi_update_user', 'cnss_plat_ipc_qmi_disconnect_cb',
         'cnss_plat_ipc_register', 'cnss_plat_ipc_unregister', 'cnss_plat_ipc_clear_users',
         'cnss_plat_ipc_qmi_init_setup_req_handler', 'cnss_plat_ipc_qmi_daemon_config',
         'cnss_plat_ipc_init_fn', 'cnss_plat_ipc_qmi_svc_exit']
harness = (H / 'tests/wlan_qmi_transfer.c').read_text()
code = harness.replace('/* HEADERS */', header).replace('/* STRUCTURES */', structures).replace('/* FUNCTIONS */', '\n'.join(function(source, name) for name in names))
with tempfile.TemporaryDirectory(prefix='onyx-wlan-qmi-transfer-') as tmp:
    c = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    c.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    '-Werror=implicit-function-declaration', str(c), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=45)
    assert not result.returncode, result.stdout + result.stderr
report = {'qmi_transfer': json.loads(result.stdout),
          'source_sha256': {P: hashlib.sha256(source.encode()).hexdigest(),
                            'platform/cnss_utils/cnss_plat_ipc_service_v01.h': hashlib.sha256((W / args.source_dir / 'platform/cnss_utils/cnss_plat_ipc_service_v01.h').read_bytes()).hexdigest(),
                            'platform/cnss_utils/cnss_plat_ipc_qmi.h': hashlib.sha256(private.encode()).hexdigest()},
          'harness_sha256': {'wlan_qmi_transfer.c': hashlib.sha256(harness.encode()).hexdigest()},
          'sanitizers': ['AddressSanitizer', 'UndefinedBehaviorSanitizer'],
          'limits': 'Extracted actual file APIs, handlers, callbacks, config publication and service init/exit with mock IDR/QMI transport, pthread SRCU/locking and simulated completion. Verifies bounds, peer/direction isolation, transport errors and callback/file drain. Does not execute native kernel SRCU, QMI socket/work scheduling, firmware, IRQ or WLAN hardware.'}
(args.output or H / 'qmi-transfer-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
