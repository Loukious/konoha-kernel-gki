from pathlib import Path
import argparse, hashlib, json, re, subprocess, tempfile
H=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,default=H/'private-source');parser.add_argument('--report',type=Path,default=H/'bounds-validation.json');args=parser.parse_args()
B=args.source/'qcom/opensource/audio-kernel/asoc/codecs'
clang=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang')
def function(text,name):
 m=re.search(r'^\w[^\n]*\b'+name+r'\([^;{}]*\)\s*\{',text,re.M);assert m,name
 start=m.start();pos=m.end();depth=1
 while depth:
  if text[pos]=='{':depth+=1
  if text[pos]=='}':depth-=1
  pos+=1
 return text[start:pos]+'\n'
def read(p):return (B/p).read_text()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
ph=read('sia91xx/sipa_parameter.h');fh=ph[ph.index('#define SIPA_FW_VER_MAIN'):ph.index('void sipa_param_load_fw')]
fc=read('sia91xx/sipa_parameter.c');fc='\n'.join(function(fc,n) for n in ['sipa_param_list_valid','sipa_param_validate'])
th=read('sia91xx/sipa_tuning_if.h');th=th[th.index('#define DATA_MAX_LEN'):th.index('enum cal_packet_opt')]
tc=read('sia91xx/sipa_tuning_if.c');tc='\n'.join(function(tc,n) for n in ['myCircularQueueCreate','myCircularQueueEnQueue','myCircularQueueDeQueue','sipa_tuning_write'])
ah=read('aw882xx/aw882xx.h');ah='enum { AWRW_I2C_ST_NONE, AWRW_I2C_ST_READ, AWRW_I2C_ST_WRITE };\n'+ '\n'.join(re.findall(r'^#define AWRW_.*$',ah,re.M))+'\nenum { AWRW_FLAG_WRITE, AWRW_FLAG_READ };\n'+ah[ah.index('enum {\n\tAWRW_HDR_WR_FLAG'):ah.index('/********************************************\n * struct aw882xx')]
ac=read('aw882xx/aw882xx.c');ac='\n'.join(function(ac,n) for n in ['aw882xx_awrw_write','aw882xx_awrw_data_check','aw882xx_awrw_parse_buf'])
suites={
 'firmware':(fh,fc,[('size','cont->size < sizeof(*param)','false'),('list','list->num <= (param->data_size - list->offset) / size','true'),('crc','param->crc != crc32','param->crc == crc32'),('nested','!sipa_param_list_valid(param, &chip->startup,\n\t\t\t\t\t\t   sizeof(SIPA_REG_PROC))','false')]),
 'tuning':(th,tc,[('overflow','msg->payload_size > DATA_MAX_LEN - sizeof(*msg)','false'),('full','next == obj->head','false'),('fault','if (copy_to_user(arg, obj->q[obj->head].data, obj->q[obj->head].len))','if (false)'),('snapshot','mutex_lock(&pdata->lock);','copy_from_user(data, arg, sizeof(*msg));\n\tmutex_lock(&pdata->lock);')]),
 'aw_bounds':(ah,ac,[('range','data[AWRW_HDR_REG_NUM] > reg_num_max - data[AWRW_HDR_REG_ADDR]','false'),('short','ret != data_len + 1','ret < 0'),('leak','\t\t\tkfree(data_buf);','\t\t\t/* omitted */')])}
report={'status':'PASS','source':str(args.source),'compiler':str(clang),'compiler_sha256':sha(clang),'suites':{},'limits':'Host execution of production functions with mocked allocation, locking, firmware and userspace-copy primitives; no kernel concurrency or hardware claim.'}
with tempfile.TemporaryDirectory(prefix='onyx-amplifier-tests-') as tmp:
 tmp=Path(tmp)
 for name,(header,code,mutations) in suites.items():
  template=(H/'tests'/f'{name}.c').read_text();report['suites'][name]={'mutations':{}}
  for label,old,new in [('baseline','','')]+mutations:
   body=code
   if old:assert old in body,(name,label);body=body.replace(old,new,1)
   text=template.replace('/* PRODUCTION_HEADER */',header).replace('/* PRODUCTION_FUNCTIONS */',body)
   src=tmp/f'{name}-{label}.c';exe=src.with_suffix('');src.write_text(text)
   flags=['-std=gnu11','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-pointer-sign','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
   cp=subprocess.run([str(clang),*flags,str(src),'-o',str(exe)],capture_output=True,text=True)
   assert cp.returncode==0,cp.stderr
   run=subprocess.run([str(exe)],capture_output=True,text=True)
   if label=='baseline':
    assert run.returncode==0,run.stdout+run.stderr
    report['suites'][name].update({'result':run.stdout.strip(),'cases':int(re.search(r'PASS (\d+)',run.stdout)[1]),'production_sha256':hashlib.sha256(code.encode()).hexdigest()})
   else:
    assert run.returncode!=0,(name,label,'mutation survived')
    report['suites'][name]['mutations'][label]={'result':'rejected','exit':run.returncode}
report['cases']=sum(x['cases'] for x in report['suites'].values());report['controls']=sum(len(x['mutations']) for x in report['suites'].values());args.report.write_text(json.dumps(report,indent=2)+'\n');print(f"PASS {report['cases']} cases, {report['controls']} rejecting controls")
