#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
PREFIX='qcom/opensource/audio-kernel/'
def clean(text):
    return '\n'.join(line for line in text.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit|subsys_initcall)',line))
def main():
    p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--output',type=Path,required=True);p.add_argument('--negative',action='store_true');a=p.parse_args()
    paths=['include/bindings/qcom,gpr.h','include/soc/audio_mod_devicetable.h','include/ipc/gpr-lite.h','ipc/gpr-lite.c']
    data={path:(a.workspace/a.source_dir/PREFIX/path).read_bytes() for path in paths}
    source=data[paths[-1]].decode()
    if a.negative:source=source.replace('struct device *dev, struct device_driver *drv)', 'struct device *dev, const struct device_driver *drv)')
    fixture=(HERE/'tests/audio_gpr.c').read_bytes();results={}
    with tempfile.TemporaryDirectory(prefix='onyx-audio-gpr-tests-') as tmp:
        src=Path(tmp)/'test.c';binary=Path(tmp)/'test'
        src.write_text(fixture.decode().replace('/* HEADERS */','\n'.join(clean(data[path].decode()) for path in paths[:-1])).replace('/* SOURCE */',clean(source)))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(src),'-o',str(binary)],capture_output=True,text=True)
        assert not run.returncode,run.stdout+run.stderr
        for scenario in ['bounds','routing','transmit','domains','probe','lifetime','init']:
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=30)
            if a.negative:
                assert run.returncode,scenario
                results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR: AddressSanitizer' in l),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr
                results.update(json.loads(run.stdout))
    report={'tests':results,'source_sha256':{PREFIX+path:hashlib.sha256(b).hexdigest() for path,b in data.items()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full GPR source/header bodies with simulated RPMsg, OF, IDR, service devices and notifier services. Threaded callback/removal model uses real pthread locks and references. No actual kernel callback concurrency, DSP firmware, IRQs, suspend/resume or hardware validation.'}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
