#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
PATH='qcom/opensource/audio-kernel/dsp/digital-cdc-rsc-mgr.c'
def main():
    p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=(a.workspace/a.source_dir/PATH).read_bytes();fixture=(HERE/'tests/audio_votes.c').read_bytes();results={}
    body='\n'.join(l for l in data.decode().splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL)',l))
    with tempfile.TemporaryDirectory(prefix='onyx-audio-votes-tests-') as tmp:
        path=Path(tmp)/'test.c';binary=Path(tmp)/'test';path.write_text(fixture.decode().replace('/* SOURCE */',body))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr
        for scenario in ['votes','restoration','faults','shutdown','concurrent']:
            if a.negative and scenario=='concurrent':continue
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=30)
            if a.negative:
                assert run.returncode,scenario;results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR: AddressSanitizer' in l),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr;results.update(json.loads(run.stdout))
    report={'tests':results,'source_sha256':{PATH:hashlib.sha256(data).hexdigest()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full source clock-vote bodies with simulated CCF and real pthread lock contention; no actual clock hardware, kernel callback scheduling, firmware or suspend/resume verification.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
