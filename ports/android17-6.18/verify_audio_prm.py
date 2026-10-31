#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent;PREFIX='qcom/opensource/audio-kernel/';PATH=PREFIX+'dsp/audio_prm.c';HEADER=PREFIX+'include/dsp/audio_prm.h'
def clean(body):return '\n'.join(line for line in body.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)',line))
def main():
    p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--wire',action='store_true');p.add_argument('--token-mutation',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args();source=(a.workspace/a.source_dir/PATH).read_bytes();api=(a.workspace/a.source_dir/HEADER).read_bytes();fixture=(HERE/'tests/audio_prm.c').read_bytes();body=source.decode();results={}
    headers={path:(a.workspace/'onyx'/PREFIX/path).read_bytes() for path in ['include/bindings/qcom,gpr.h','include/soc/audio_mod_devicetable.h','include/ipc/gpr-lite.h']};headers['include/dsp/audio_notifier.h']=(a.workspace/'onyx'/PREFIX/'include/dsp/audio_notifier.h').read_bytes();headers['include/dsp/audio_prm.h']=api
    if a.token_mutation:body=body.replace('\tg_prm.adev = adev;', '\tatomic_set(&prm_command_token, 0);\n\tg_prm.adev = adev;')
    scenarios=['responses','truncated','readiness','ready_epoch','identities','payloads','cores','codec','failures','sync_notifier','removal','service_down','probe_faults','wrap'];negative=['responses','truncated','readiness','ready_epoch','identities','payloads','cores','sync_notifier','removal','service_down']
    with tempfile.TemporaryDirectory(prefix='onyx-audio-prm-tests-') as tmp:
        path=Path(tmp)/'test.c';binary=Path(tmp)/'test';path.write_text('#define NATIVE '+str(int(a.negative or a.source_dir=='onyx-imports'))+'\n'+fixture.decode().replace('/* HEADERS */','\n'.join(clean(b.decode()) for b in headers.values())).replace('/* SOURCE */',clean(body)))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr
        for scenario in (['identities'] if a.token_mutation else ['wire_dump'] if a.wire else negative if a.negative else scenarios):
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=15)
            if a.negative or a.token_mutation:
                assert run.returncode,scenario;results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((line for line in run.stderr.splitlines() if 'Assertion' in line or 'ERROR: AddressSanitizer' in line or 'runtime error:' in line),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr;results.update(json.loads(run.stdout))
    report={'tests':results,'wire_mode':a.wire,'mutation':'reset tokens on binding' if a.token_mutation else None,'source_sha256':{PATH:hashlib.sha256(source).hexdigest(),HEADER:hashlib.sha256(api).hexdigest()},'provider_header_sha256':{PREFIX+path:hashlib.sha256(b).hexdigest() for path,b in headers.items()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full PRM source, exact native public API and frozen GPR/notifier headers with simulated GPR replies, readiness checks, notifier registration/drain and device references. Real pthread waiter/notifier/removal contention; no kernel scheduling, DSP firmware, clocks or hardware verification.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
