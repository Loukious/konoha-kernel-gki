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
PATH=PREFIX+'ipc/audio-pkt.c'
def clean(text):return '\n'.join(line for line in text.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_platform_driver|module_init|module_exit)',line))
def main():
    p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--cleanup-mutation',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    source=(a.workspace/a.source_dir/PATH).read_bytes();fixture=(HERE/'tests/audio_pkt.c').read_bytes();results={}
    headers={path:(a.workspace/'onyx'/PREFIX/path).read_bytes() for path in ['include/bindings/qcom,gpr.h','include/soc/audio_mod_devicetable.h','include/ipc/gpr-lite.h']};body=source.decode()
    if a.negative:body=re.sub(r'static int audio_pkt_platform_driver_remove\(struct platform_device \*adev\)(.*?)(?=\nstatic const struct of_device_id)',lambda m:m.group(0).replace('static int','static void',1).replace('return 0;','return;'),body,flags=re.S)
    if a.cleanup_mutation:
        body=body.replace('\tmutex_lock(&priv->lock);\n\tif (priv->ever_bound)', '\tif (priv->ever_bound)').replace('\tmutex_unlock(&priv->lock);\n\tcdev_device_del', '\tcdev_device_del')
    scenarios=['tx_lengths','tx_headers','tx_faults','rx_headers','rx_basic','regions_multiple','regions_bounds','regions_invalid','reads','queues','backups','sessions','withdrawal','receive_wait','old_file','open_remove','failure_paths','logger','sync_callback','send_unbind']
    negatives=['tx_lengths','tx_headers','rx_headers','rx_basic','regions_multiple','regions_bounds','regions_invalid','reads','sessions','withdrawal','old_file','open_remove','failure_paths','logger','sync_callback','send_unbind']
    with tempfile.TemporaryDirectory(prefix='onyx-audio-pkt-tests-') as tmp:
        path=Path(tmp)/'test.c';binary=Path(tmp)/'test';path.write_text('#define NATIVE '+str(int(a.negative))+'\n'+fixture.decode().replace('/* HEADERS */','\n'.join(clean(b.decode()) for b in headers.values())).replace('/* SOURCE */',clean(body)))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr
        for scenario in (['cleanup_remove'] if a.cleanup_mutation else negatives if a.negative else scenarios+['publication','register_publication','singleton','initial_faults','cleanup_remove']):
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=15)
            if a.negative or a.cleanup_mutation:
                assert run.returncode,scenario;results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((line for line in run.stderr.splitlines() if 'Assertion' in line or 'ERROR: AddressSanitizer' in line or 'runtime error:' in line),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr;results.update(json.loads(run.stdout))
    report={'tests':results,'mutation':'remove platform cleanup mutex' if a.cleanup_mutation else None,'source_sha256':{PATH:hashlib.sha256(source).hexdigest()},'provider_header_sha256':{PREFIX+path:hashlib.sha256(b).hexdigest() for path,b in headers.items()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full packet source and frozen GPR wire headers with simulated cdev/device references, real allocated SKB storage/queues, private logger, synchronous GPR probe/callback, ION FD translation, copy/allocation failures and pthread read/send/withdraw contention. Work execution is explicit; no kernel VFS/workqueue scheduling, DMA, DSP or hardware verification.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
