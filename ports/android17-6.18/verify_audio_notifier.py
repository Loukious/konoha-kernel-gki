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
    text='\n'.join(line for line in text.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)',line))
    # Kernel 6.6 platform remove returned int; isolate that API-only difference.
    text=re.sub(r'static int audio_notify_remove\(struct platform_device \*pdev\)\n\{\n\treturn 0;\n\}',
                'static void audio_notify_remove(struct platform_device *pdev)\n{\n\treturn;\n}',text)
    return text
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('workspace',type=Path)
    parser.add_argument('--source-dir',default='onyx')
    parser.add_argument('--negative',action='store_true')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    sources={p:(args.workspace/args.source_dir/PREFIX/p).read_bytes() for p in ['dsp/audio_notifier.c','dsp/audio_ssr.c']}
    fixture=(HERE/'tests/audio_notifier.c').read_bytes()
    results={}
    with tempfile.TemporaryDirectory(prefix='onyx-audio-notifier-tests-') as tmp:
        path=Path(tmp)/'test.c';binary=Path(tmp)/'test'
        path.write_text(fixture.decode().replace('/* SOURCE */','\n'.join(clean(data.decode()) for data in sources.values())))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
        assert not run.returncode,run.stdout+run.stderr
        for scenario in ['bounds','probe','failures','domains','remove','drain','concurrent']:
            if args.negative and scenario=='concurrent':continue
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=30)
            if args.negative:
                assert run.returncode,scenario
                results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR: AddressSanitizer' in l),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr
                results.update(json.loads(run.stdout))
    report={'tests':results,'source_sha256':{PREFIX+p:hashlib.sha256(data).hexdigest() for p,data in sources.items()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full source bodies against mock platform, remoteproc, PDR/SSR and pthread SRCU services; two event threads race two registry threads. No real firmware, kernel callback concurrency, suspend/resume or hardware validation.'}
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
