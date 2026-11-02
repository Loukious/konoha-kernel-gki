#!/usr/bin/env python3
"""Run production ADC protocol/lifetime functions against transport mocks."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();s=a.source.read_text()
def function(name):
    m=re.search(r'(?m)^static [^\n]*\b'+name+r'\([^;]*?\n\{',s);assert m,name
    start=s.index('{',m.start());depth=0
    for t in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',s[start:],re.S):
        if t.group()=='{':depth+=1
        elif t.group()=='}':
            depth-=1
            if not depth:return s[m.start():start+t.end()]
    raise AssertionError(name)
names=['glink_adc_handle_read_resp','glink_adc_callback','glink_adc_state_callback','glink_adc_read_channel','glink_adc_fwnode_xlate','glink_adc_stop','glink_adc_unregister_client']
body='\n'.join(function(n) for n in names)
types=s[s.index('#define MSG_OWNER_SMB_ADC'):s.index('static void glink_adc_handle_read_resp')]
controls={'short_header':('len < sizeof(*hdr)','false'),'response_identity':('read_resp->chan == adc->request.chan','true'),'timeout_reuse':('adc->desynced = true;','adc->desynced = false;'),'stop_error':('adc->result = -ENODEV;','adc->result = 0;'),'down_error':('adc->result = -ENOTCONN;','adc->result = 0;'),'client_write_drain':('mutex_lock(&adc->lock);\n\tpmic_glink_unregister_client','/* no drain */\n\tpmic_glink_unregister_client')}
results={}
with tempfile.TemporaryDirectory(prefix='glink-adc-') as tmp:
    for label in ['production',*controls]:
        code=body
        if label in controls:
            old,new=controls[label];assert old in code,label;code=code.replace(old,new,1)
        src=Path(tmp)/(label+'.c');exe=src.with_suffix('');src.write_text((H/'protocol.c.in').read_text().replace('/* PRODUCTION_TYPES */',types).replace('/* PRODUCTION_FUNCTIONS */',code))
        subprocess.run([a.cc,'-std=gnu11','-Wall','-Werror','-fsanitize=address,undefined','-g',str(src),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True);assert (r.returncode==0)==(label=='production'),r.stderr
        results[label]={'compiled':True,'status':'PASS' if label=='production' else 'REJECTED'}
a.output.write_text(json.dumps({'status':'PASS','source_sha256':hashlib.sha256(s.encode()).hexdigest(),'results':results,'limits':'Production functions with sequential transport/framework mocks; no actual firmware, kernel concurrency, physical ADC or phone validation.'},indent=2)+'\n');print('PASS GLINK ADC protocol/lifetime paths; six compiled regressions rejected')
