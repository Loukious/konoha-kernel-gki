#!/usr/bin/env python3
"""Exercise extracted secure-service functions with mocked kernel/firmware APIs."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CORE = 'drivers/firmware/qcom/si_core/'
SEC = 'qcom/opensource/securemsm-kernel/'


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError(name)
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
    root = args.workspace.resolve() / args.source_dir
    files = {
        'core': CORE + 'si_core.c',
        'async': CORE + 'si_core_async.c',
        'proxy': 'drivers/misc/qseecom_proxy.c',
        'firmware': SEC + 'smcinvoke/si_core_xts/qseecom.c',
        'smci': SEC + 'smcinvoke/si_core_xts/smci.c',
        'smmu': SEC + 'smmu-proxy/qti-smmu-proxy-common.c',
        'sender': SEC + 'smmu-proxy/qti-smmu-proxy-pvm.c',
        'hdcp': SEC + 'hdcp/hdcp_main.c',
        'heap': 'drivers/dma-buf/heaps/qcom_sg_ops.c',
        'memory': CORE + 'xts/mem-object.c',
    }
    sources = {key: (root / path).read_text() for key, path in files.items()}
    groups = {
        'secure_wire': [('core', ['si_message_size', 'si_buffer_in_bounds', 'si_callback_valid']),
                        ('async', ['async_si_buffer', 'async_release_handler', '__fetch__async_reqs'])],
        'secure_firmware': [('firmware', ['split_firmware_layout', 'firmware_request_from_smcinvoke'])],
        'secure_lifetime': [('proxy', ['provide_qseecom_kernel_fun_ops', 'qseecom_start_app', 'qseecom_shutdown_app', 'qseecom_send_command', 'qseecom_process_listener_from_smcinvoke']),
                            ('core', ['si_object_invoke_ctx_init', 'si_core_init'])],
        'secure_smmu': [('sender', ['smmu_proxy_response_valid', 'sender_probe_handler']),
                        ('smmu', ['smmu_proxy_get_csf_version'])],
        'secure_hdcp': [('hdcp', ['hdcp1_count_ones', 'hdcp1_validate_aksv', 'hdcp1_set_hdcp_key_verify_retries', 'hdcp2_set_hdcp_key_verify_retries'])],
        'secure_heap': [('heap', ['smmu_proxy_callbacks_get', 'qti_smmu_proxy_register_callbacks'])],
        'secure_memory': [('core', ['init_si_object_user']), ('memory', ['init_si_mem_object_user', 'init_si_mem_object', 'rm_shm_bridge', 'detach_dma_buf', 'release_memory_obj', 'mo_shm_bridge_release'])],
        'secure_fds': [('smci', ['smci_fd_reserve', 'smci_fd_publish', 'smci_fd_batch_finish', 'u_handle_alloc', 'get_u_handle_from_si_object', 'marshal_out_req', 'marshal_in_cb_req', 'process_invoke_req', 'process_accept_req'])],
    }
    results = {}
    with tempfile.TemporaryDirectory(prefix='onyx-secure-tests-') as tmp:
        for name, group in groups.items():
            code = '\n'.join(function(sources[key], method) for key, methods in group for method in methods)
            template = (HERE / 'tests' / (name + '.c')).read_text()
            if name == 'secure_fds':
                definitions = re.search(r'(?m)^#define SMCI_MAX_EXPORTED_FDS .*?^};', sources['smci'], re.S)[0]
                template = template.replace('/* BATCH STRUCTS */', definitions)
            if name == 'secure_wire':
                definitions = re.search(r'(?m)^struct async_msg_info \{.*?^#define ASYNC_OP_RELEASE_MSG_SIZE\(n\) \\\n[^\n]+', sources['async'], re.S)[0]
                template = template.replace('/* ASYNC STRUCTS */', definitions)
            if name == 'secure_firmware':
                code = code.replace('struct elf32_hdr', 'Elf32_Ehdr').replace('struct elf64_hdr', 'Elf64_Ehdr')
            path = Path(tmp) / (name + '.c')
            path.write_text(template.replace('/* FUNCTIONS */', code))
            executable = Path(tmp) / name
            subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(path), '-o', str(executable)], check=True)
            run = subprocess.run([str(executable)], text=True, capture_output=True, timeout=45)
            if run.returncode:
                raise RuntimeError(name + ': ' + run.stderr)
            results[name] = json.loads(run.stdout)
    # Runtime tests cover bounded protocol functions; these contracts connect them
    # to the large ioctl and firmware consumers without claiming live kernel coverage.
    assert 'if (!si_callback_valid(oic, msg))' in function(sources['core'], 'prepare_args')
    assert 'msg->counts != counts' in function(sources['core'], 'update_args')
    assert 'msg->args[ob].b.size > u[i].b.size' in function(sources['core'], 'update_args')
    assert 'u_req.counts & ~0xffffU' in function(sources['smci'], 'process_invoke_req')
    assert 'ret == -E2BIG' in function(sources['smci'], 'process_invoke_req')
    assert 'smp_store_release(&smcinvoke_ready, true)' in function(sources['smci'], 'smcinvoke_probe')
    assert 'cdev_alloc()' in function(sources['smci'], 'smcinvoke_probe')
    assert 'nodp->i_cdev != READ_ONCE(smcinvoke_cdev)' in function(sources['smci'], 'root_open')
    assert 'accept->buf_len < cb_txn->u_args_sz' in function(sources['smci'], 'process_accept_req')
    assert 'needed > accept->buf_len' in function(sources['smci'], 'process_accept_req')
    assert 'suppress_bind_attrs = true' in sources['smci'] and '__module_get(THIS_MODULE)' in sources['smci']
    qsee = (root / SEC / 'qseecom/qseecom.c').read_text()
    cleanup = function(qsee, 'qseecom_deinit_bus')
    assert cleanup.index('timer_shutdown_sync') < cleanup.index('cancel_work_sync') < cleanup.index('icc_put')
    assert 'file->private_data = NULL' in function(qsee, 'qseecom_open')
    assert 'server_id < 0 || server_id > INT_MAX' in function(sources['smci'], 'cb_object_alloc')
    assert 'u_handle > INT_MAX' in function(sources['smci'], 'get_si_object_from_u_handle')
    assert '!is_cb_object(object)' in function(sources['smci'], 'get_u_handle_from_si_object')
    results['source_contracts'] = 16
    results['source_sha256'] = {str(path): hashlib.sha256((root / path).read_bytes()).hexdigest()
                                for path in list(files.values()) + [SEC + 'qseecom/qseecom.c']}
    results['harness_sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (HERE / 'tests').glob('secure_*.c')}
    results['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    results['limits'] = 'Extracted driver functions with mocked kernel allocation, module references, sysfs, SMC, firmware, Gunyah and locking APIs. Does not validate live VFS/RCU/SRCU, secure firmware, memory isolation, HDCP or phone operation.'
    output = json.dumps(results, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')


if __name__ == '__main__':
    main()
