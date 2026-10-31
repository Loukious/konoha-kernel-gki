#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent;PATH='qcom/opensource/audio-kernel/dsp/adsp-loader.c'
def main():
    p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args();source=(a.workspace/a.source_dir/PATH).read_bytes();fixture=(HERE/'tests/audio_adsp.c').read_bytes();body=source.decode();results={}
    if a.negative:body=re.sub(r'static int adsp_loader_remove\(struct platform_device \*pdev\)(.*?)(?=\nstatic int adsp_loader_probe)',lambda m:m.group(0).replace('static int','static void',1).replace('return 0;','return;'),body,flags=re.S)
    body='\n'.join(l for l in body.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_platform_driver|module_init|module_exit)',l))
    with tempfile.TemporaryDirectory(prefix='onyx-audio-adsp-tests-') as tmp:
        path=Path(tmp)/'test.c';binary=Path(tmp)/'test';path.write_text(('#define NATIVE 1\n' if a.negative else '')+fixture.decode().replace('/* SOURCE */',body))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr
        for scenario in (['votes','idempotent','restart','firmware','provider','cleanup','parsing','optional','publication','removal'] if a.negative else ['votes','idempotent','restart','firmware','provider','cleanup','parsing','optional','faults','restart_states','providers_rebind','publication','removal']):
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=30)
            if a.negative:
                assert run.returncode,scenario;results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR: AddressSanitizer' in l or 'runtime error:' in l),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr;results.update(json.loads(run.stdout))
    report={'tests':results,'source_sha256':{PATH:hashlib.sha256(source).hexdigest()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full ADSP source with simulated DT/NVMEM/GPR-ready/remoteproc/sysfs behavior and real pthread worker/removal locking; no real kernel kernfs scheduling, Qualcomm DSP recovery, firmware execution, suspend/resume, IRQ or hardware verification.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
