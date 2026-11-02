#!/usr/bin/env python3
"""Check production probe/remove ownership with two independent controllers."""
import argparse, hashlib, json, re, subprocess, tempfile
from pathlib import Path
H=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--cc',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();s=a.source.read_text()
def function(name):
    m=re.search(r'(?m)^static [^\n]*\b'+name+r'\([^;]*?\n\{',s);assert m
    pos=s.index('{',m.start())+1;depth=1
    while depth:
        depth+=(s[pos]=='{')-(s[pos]=='}');pos+=1
    return s[m.start():pos]
body='\n'.join(function(n) for n in ['qcom_tlmm_vm_irqchip_probe','qcom_tlmm_vm_irqchip_remove'])
controls={'shared_remove':('platform_get_drvdata(pdev)','last_data'), 'missing_remove':('irq_domain_remove(data->domain);','(void)data;')}
results={}
with tempfile.TemporaryDirectory(prefix='tlmm-vm-') as tmp:
    for name in ['production',*controls]:
        code=body
        if name in controls:
            old,new=controls[name];assert old in code;code=code.replace(old,new)
        src=Path(tmp)/(name+'.c');exe=src.with_suffix('')
        src.write_text((H/'test.c.in').read_text().replace('/* BODY */',code))
        subprocess.run([a.cc,'-std=gnu11','-Wall','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True)
        assert (r.returncode==0)==(name=='production'),r.stderr
        results[name]={'compiled':True,'status':'PASS' if name=='production' else 'REJECTED'}
a.output.write_text(json.dumps({'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'results':results,'limits':'Selected production probe/remove functions with mocked allocation and IRQ domains; no IRQ delivery or consumer concurrency.'},indent=2)+'\n')
print('PASS two-device domain ownership; two compiled regressions rejected')
