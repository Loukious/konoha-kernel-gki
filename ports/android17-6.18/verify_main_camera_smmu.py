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
ap.add_argument('--provider-root', type=Path, default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/onyx'))
ap.add_argument('--common-root', type=Path, default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/common'))
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
p = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_smmu/cam_smmu_api.c'
s = p.read_text()
provider_paths = [a.provider_root / 'drivers/iommu/qcom-iommu-util.c', a.provider_root / 'drivers/iommu/arm/arm-smmu/arm-smmu.c', a.common_root / 'drivers/iommu/iommu.c']
providers = [p.read_text() for p in provider_paths]

def fn(s, name):
    m = re.search(r'^(?:static[ \t]+)?(?:inline[ \t]+)?(?:\w+[ \t]+)*\w+[ \t*]+(?:\n[ \t]*)?' + name + r'\([^;{]*\{', s, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[m.start():end] + '\n'

names = ['cam_smmu_dispatch_fault', 'cam_smmu_set_client_page_fault_handler',
         'cam_smmu_unset_client_page_fault_handler', 'cam_smmu_remove_client',
         'cam_smmu_iommu_fault_handler', 'cam_smmu_reset_cb_page_fault_cnt',
         'cam_smmu_reset_iommu_table', 'cam_smmu_deinit_cb', 'cam_smmu_release_cb',
         'cam_smmu_setup_cb', 'cam_alloc_smmu_context_banks', 'cam_populate_smmu_context_banks',
         'cam_smmu_cb_component_bind', 'cam_smmu_cb_component_unbind',
         'cam_smmu_cb_qsmmu_component_bind', 'cam_smmu_component_bind', 'cam_smmu_component_unbind']
code = ''.join(fn(s, name) for name in names)
provider = fn(providers[1], 'arm_smmu_set_fault_handler') + fn(providers[0], 'qcom_iommu_set_fault_handler') + fn(providers[2], 'iommu_set_fault_handler') + fn(providers[2], 'report_iommu_fault')
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-smmu-') as td:
    d = Path(td)
    camera = d / 'smmu_camera.inc'
    native = d / 'smmu_provider.inc'
    camera.write_text(code)
    native.write_text(provider)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
           '-Wno-unused-variable', '-Wno-sign-compare', '-g', '-pthread', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
           '-I', str(d), str(H / 'tests/main_camera_smmu.c'), '-o', str(d / 'smmu')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'smmu')], check=True, capture_output=True, text=True, timeout=20)
    result = json.loads(r.stdout)
    print(r.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('camera', 'legacy_dma_cookie_registration', 'rc = qcom_iommu_set_fault_handler(cb->domain,', 'rc = legacy_registration(cb->domain,'),
            ('camera', 'registration_error_ignored', 'if (rc)\n\t\t\tgoto cb_init_fail;\n\t\tcb->fault_handler_registered', 'if (false)\n\t\t\tgoto cb_init_fail;\n\t\tcb->fault_handler_registered'),
            ('camera', 'child_failure_frees_parent', 'CAM_ERR(CAM_SMMU, "Error: populating context banks");', 'cam_smmu_release_cb(pdev);'),
            ('camera', 'pool_allocation_false_success', 'rc = -ENOMEM;\n\t\t\t\tgoto end;', 'rc = 0;\n\t\t\t\tgoto end;'),
            ('camera', 'pool_cleanup_leaked', 'gen_pool_destroy(cb->shared_mem_pool[i]);', '(void)cb;'),
            ('camera', 'dma_parameters_false_success', 'rc = -ENOMEM;\n\t\tgoto cb_init_fail;', 'rc = 0;\n\t\tgoto cb_init_fail;'),
            ('camera', 'wrong_multi_client_comparison', 'if (!strcmp(cb->name[i], cb->multi_region_clients[j]))', 'if (strcmp(cb->name[i], cb->multi_region_clients[j]))'),
            ('camera', 'provider_callback_left_installed', 'qcom_iommu_set_fault_handler(cb->domain, NULL, NULL);', '(void)cb;'),
            ('camera', 'client_withdrawal_missing', 'void cam_smmu_remove_client(void *token)\n{', 'void cam_smmu_remove_client(void *token)\n{\n\treturn;'),
            ('camera', 'duplicate_registration_count', 'cb->handler[i] = handler_cb;', 'cb->cb_count++;\n\t\t\tcb->handler[i] = handler_cb;'),
            ('camera', 'parent_count_not_reset', 'iommu_cb_set.cb_init_count = 0;', '(void)pdev;'),
            ('camera', 'csf_failure_array_leak', 'CAM_ERR(CAM_SMMU, "Failed to fetch CSF version: %d", rc);\n\t\tcam_smmu_release_cb(pdev);', 'CAM_ERR(CAM_SMMU, "Failed to fetch CSF version: %d", rc);'),
            ('provider', 'provider_drain_lock_missing', 'mutex_lock(&smmu_domain->fault_handler_lock);', '(void)smmu_domain;'),
        ]
        for target, name, old, new in variants:
            original, path = (code, camera) if target == 'camera' else (provider, native)
            assert old in original, name
            modified = original.replace(old, new, 1)
            if name == 'provider_drain_lock_missing':
                modified = modified.replace('mutex_unlock(&smmu_domain->fault_handler_lock);', '(void)smmu_domain;', 1)
            path.write_text(modified)
            subprocess.run(cmd, check=True, capture_output=True)
            try:
                r = subprocess.run([str(d / 'smmu')], capture_output=True, timeout=10,
                                   preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert r.returncode, name
                returncode = r.returncode
            except subprocess.TimeoutExpired:
                returncode = 'bounded-deadlock-timeout'
            controls.append({'name': name, 'rejected': True, 'returncode': returncode})
            print('REJECTED', name, flush=True)
            path.write_text(original)
report = {'status': 'PASS', 'tests': result, 'negative_controls': controls,
          'source_sha256': {str(p.relative_to(a.source_root)): hashlib.sha256(p.read_bytes()).hexdigest()},
          'provider_sha256': {str(p.relative_to(a.provider_root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in provider_paths[:2]},
          'common_sha256': {str(provider_paths[2].relative_to(a.common_root)): hashlib.sha256(provider_paths[2].read_bytes()).hexdigest()},
          'harness_sha256': {'main_camera_smmu.c': hashlib.sha256((H / 'tests/main_camera_smmu.c').read_bytes()).hexdigest()},
          'limits': 'Actual camera SMMU setup, registration, fault and teardown functions; actual Qualcomm/ARM registration and generic core cookie functions. Pthread locks; mocked DT, DMA pools/mapping and device services. No hardware, actual DMA mappings, kernel execution or secure firmware.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
