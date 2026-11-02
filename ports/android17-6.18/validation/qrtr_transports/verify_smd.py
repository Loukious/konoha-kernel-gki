#!/usr/bin/env python3
"""Check native SMD callback preview gating using production source."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();s=a.source.read_text()
m=re.search(r'(?m)^static int qcom_smd_qrtr_callback\([^;]*?\n\{',s);assert m
pos=s.index('{',m.start())+1;depth=1
while depth:
    depth+=(s[pos]=='{')-(s[pos]=='}');pos+=1
body=s[m.start():pos]
controls={'malformed_preview':('return 0;','rc = 0;'),'failed_preview':('!rc && qcom_glink_is_wakeup(true)','qcom_glink_is_wakeup(true)')}
results={}
with tempfile.TemporaryDirectory(prefix='qrtr-smd-') as tmp:
    for label in ['production',*controls]:
        code=body
        if label in controls:
            old,new=controls[label];assert old in code;code=code.replace(old,new,1)
        src=Path(tmp)/(label+'.c');exe=src.with_suffix('');src.write_text((H/'smd.c.in').read_text().replace('/* PRODUCTION */',code))
        subprocess.run([a.cc,'-std=gnu11','-Wall','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True);assert (r.returncode==0)==(label=='production'),r.stderr
        results[label]={'compiled':True,'status':'PASS' if label=='production' else 'REJECTED'}
a.output.write_text(json.dumps({'status':'PASS','source_sha256':hashlib.sha256(s.encode()).hexdigest(),'results':results,'limits':'Production callback with mocked endpoint parser and wakeup state; no actual RPMSG transport or concurrent teardown.'},indent=2)+'\n');print('PASS five SMD callback cases; two compiled regressions rejected')
