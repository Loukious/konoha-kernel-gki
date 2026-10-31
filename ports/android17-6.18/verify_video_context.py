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
probe = 'qcom/opensource/video-driver/driver/vidc/src/msm_vidc_probe.c'
header = 'qcom/opensource/video-driver/driver/vidc/inc/resources.h'
smmu = 'drivers/iommu/arm/arm-smmu/arm-smmu.c'
util = 'drivers/iommu/qcom-iommu-util.c'
dma = 'include/linux/dma-mapping.h'

def extract(body, name):
    m = re.search(r'(?m)^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\n\{', body)
    assert m, name
    pos = body.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (body[pos] == '{') - (body[pos] == '}')
        pos += 1
    return body[m.start():pos] + '\n'

provider = (a.workspace / 'onyx' / smmu).read_text()
source = (root / probe).read_text()
provider_functions = '\n'.join(extract(provider, name) for name in ['arm_smmu_set_fault_handler', 'report_iommu_fault_helper'])
util_function = extract((a.workspace / 'onyx' / util).read_text(), 'qcom_iommu_set_fault_handler')
video_functions = '\n'.join(extract(source, name) for name in ['msm_vidc_release_context_bank', 'msm_vidc_setup_context_bank', 'msm_vidc_component_unbind'])
layout = '\n'.join(re.search(r'struct ' + name + r' \{.*?\n\};', (root / header).read_text(), re.S).group() for name in ['addr_range', 'context_bank_info'])
dma_functions = '\n'.join(extract((a.workspace / 'common' / dma).read_text(), name) for name in ['dma_set_max_seg_size', 'dma_set_seg_boundary'])
controls = {
    'missing_withdrawal': ('qcom_iommu_set_fault_handler(cb->domain, NULL, NULL);', '(void)0;'),
    'missing_owned_clear': ('dev->dma_parms = NULL;', '(void)0;'),
    'missing_context_clear': ('cb->dev = NULL;', '(void)0;'),
    'missing_alloc_check': ('if (!dev->dma_parms)\n\t\t\treturn -ENOMEM;', 'if (false)\n\t\t\treturn -ENOMEM;'),
    'ignored_fault_registration_error': ('if (rc)\n\t\tgoto release;', 'if (false)\n\t\tgoto release;'),
    'missing_segment_configuration': ('dma_set_max_seg_size(dev, (unsigned int)DMA_BIT_MASK(32));', '(void)0;'),
    'missing_parameter_restore': ('dev->dma_parms->max_segment_size = cb->saved_max_segment_size;', '(void)0;'),
    'duplicate_context': ('if (cb->dev)\n\t\treturn -EBUSY;', 'if (false)\n\t\treturn -EBUSY;'),
    'provider_missing_drain': ('mutex_lock(&smmu_domain->fault_handler_lock);', '(void)0;'),
}
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
report = {'tests': {}, 'negative_controls': {}, 'source_sha256': {x: sha(root / x) for x in [probe, header]}, 'provider_sha256': {x: sha(a.workspace / 'onyx' / x) for x in [smmu, util]}, 'harness_sha256': {'video_context.c': sha(H / 'tests/video_context.c')}}
report['common_sha256'] = {dma: sha(a.workspace / 'common' / dma)}
with tempfile.TemporaryDirectory(prefix='video-context-test-') as tmp:
    for name in ['positive', *controls]:
        body = (H / 'tests/video_context.c').read_text().replace('/* LAYOUT */', layout).replace('/* PROVIDER */', provider_functions).replace('/* UTIL */', util_function).replace('/* VIDEO */', video_functions).replace('/* DMA */', dma_functions)
        if name in controls:
            before, after = controls[name]
            assert before in body, name
            body = body.replace(before, after, 1)
        path = Path(tmp) / 'video_context.c'
        path.write_text(body)
        binary = path.with_suffix('')
        build = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration', str(path), '-o', str(binary)], capture_output=True, text=True)
        assert not build.returncode, build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
        if name == 'positive':
            assert not run.returncode, run.stderr
            report['tests']['context_bank'] = {'passed': True, 'output': run.stdout.strip()}
        else:
            assert run.returncode, name
            report['negative_controls'][name] = {'rejected': True, 'returncode': run.returncode}
report['limits'] = 'Actual video setup/release/unbind and Qualcomm dispatch/registration functions; DMA allocation, MMIO, IOMMU core and kernel mutex services mocked. Pthread test covers in-flight callback withdrawal; no hardware IOMMU or Linux IRQ execution claimed.'
a.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report['tests']))
