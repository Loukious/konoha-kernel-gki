#!/usr/bin/env python3
"""Exercise production fixed-regulator probe failure paths with framework mocks."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();s=a.source.read_text()
def function(name):
    m=re.search(r'(?m)^static [^\n]*\b'+name+r'\([^;]*?\n\{',s);assert m,name
    pos=s.index('{',m.start())+1;depth=1
    while depth:
        depth+=(s[pos]=='{')-(s[pos]=='}');pos+=1
    return s[m.start():pos]
body='\n'.join(function(n) for n in ['qti_reg_fixed_voltage_init','reg_fixed_voltage_probe'])
controls={'double_gpio_put':('return ret;\n\t}\n\n\tplatform_set_drvdata','gpiod_put(cfg.ena_gpiod);\n\t\treturn ret;\n\t}\n\n\tplatform_set_drvdata'),'ignored_proxy':('return dev_err_probe(dev, ret, "failed to register proxy consumer\\n");','return 0;'),'ignored_debug':('return dev_err_probe(dev, ret, "failed to register debug regulator\\n");','return 0;')}
results={}
with tempfile.TemporaryDirectory(prefix='qti-fixed-probe-') as tmp:
    for name in ['production',*controls]:
        code=body
        if name in controls:
            old,new=controls[name];assert old in code,name;code=code.replace(old,new,1)
        src=Path(tmp)/(name+'.c');exe=src.with_suffix('');src.write_text((H/'probe.c.in').read_text().replace('/* PRODUCTION */',code))
        subprocess.run([a.cc,'-std=gnu11','-Wall','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True);assert (r.returncode==0)==(name=='production'),r.stderr
        results[name]={'compiled':True,'status':'PASS' if name=='production' else 'REJECTED'}
a.output.write_text(json.dumps({'status':'PASS','source_sha256':hashlib.sha256(s.encode()).hexdigest(),'results':results,'limits':'Production probe/helper with mocked regulator/GPIO/devres services; no physical rail, regulator concurrency or phone validation.'},indent=2)+'\n');print('PASS five probe scenarios; three compiled regressions rejected')
