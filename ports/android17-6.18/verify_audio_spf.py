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
def clean(text):return '\n'.join(l for l in text.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_platform_driver)',l))
def main():
    p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--token-negative',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    source=(a.workspace/a.source_dir/PREFIX/'dsp/spf-core.c').read_bytes();fixture=(HERE/'tests/audio_spf.c').read_bytes();results={}
    headers={path:(a.workspace/'onyx'/PREFIX/path).read_bytes() for path in ['include/bindings/qcom,gpr.h','include/soc/audio_mod_devicetable.h','include/ipc/gpr-lite.h']}
    body=source.decode()
    if a.negative:fixture=fixture.replace(b'&&p->hdr.src_port==3&&p->hdr.dst_port==1&&p->hdr.token&&!p->hdr.reserved&&!p->hdr.client_data',b'').replace(b'static unsigned identities(void){',b'#if 0\nstatic unsigned identities(void){').replace(b'static unsigned truncated(void)',b'#endif\nstatic unsigned truncated(void)').replace(b'else if(!strcmp(argv[1],\"identities\"))n=identities();',b'')
    if a.negative:body=re.sub(r'static int spf_core_platform_driver_remove\(struct platform_device \*pdev\)(.*?)(?=\nstatic const struct of_device_id)',lambda m:m.group(0).replace('static int','static void',1).replace('return 0;','return;'),body,flags=re.S)
    if a.token_negative:body=body.replace('core->token = (u32)atomic_inc_return(&spf_command_token);\n\tif (!core->token)\n\t\tcore->token = (u32)atomic_inc_return(&spf_command_token);','if (!++core->token)\n\t\tcore->token++;')
    with tempfile.TemporaryDirectory(prefix='onyx-audio-spf-tests-') as tmp:
        path=Path(tmp)/'test.c';binary=Path(tmp)/'test';path.write_text(fixture.decode().replace('/* HEADERS */','\n'.join(clean(b.decode()) for b in headers.values())).replace('/* SOURCE */',clean(body)))
        run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True);assert not run.returncode,run.stdout+run.stderr
        for scenario in (['identities'] if a.token_negative else (['responses','truncated','children','failures','remove'] if a.negative else ['responses','identities','truncated','children','failures','remove'])):
            run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=30)
            if a.negative or a.token_negative:
                assert run.returncode,scenario;results[scenario]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR: AddressSanitizer' in l),run.stderr[:200])}
            else:
                assert not run.returncode,scenario+': '+run.stdout+run.stderr;results.update(json.loads(run.stdout))
    report={'tests':results,'source_sha256':{PREFIX+'dsp/spf-core.c':hashlib.sha256(source).hexdigest()},'provider_header_sha256':{PREFIX+path:hashlib.sha256(b).hexdigest() for path,b in headers.items()},'harness_sha256':hashlib.sha256(fixture).hexdigest(),'mutation':'restore per-service token reset' if a.token_negative else None,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full SPF source with simulated GPR replies, clock-manager lifecycle, platform devices and work queue; a threaded command waiter is removed through real pthread locking. No actual kernel scheduling, DSP firmware, IRQ, suspend/resume or hardware validation.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
