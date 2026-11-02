from pathlib import Path
import argparse, hashlib, json, re, subprocess, tempfile
H=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,default=H/'private-source');parser.add_argument('--report',type=Path,default=H/'lifecycle-validation.json');args=parser.parse_args()
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
sources={'aw882xx/aw882xx.c':['aw_componet_codec_register','aw882xx_stop_component'],'sia91xx/sipa.c':['sipa_remove_files','sipa_create_files','sipa_component_remove'],'sia91xx/sipa_91xx.c':['sia91xx_register_interrupt']}
body='\n'.join(function((B/p).read_text(),n) for p,names in sources.items() for n in names)
mutations=[('registry_unwind','aw882xx->codec_ops->unregister_codec(aw882xx->dev);','/* omitted */'),('fw_drain','cancel_delayed_work_sync(&aw882xx->fw_work);','/* omitted */'),('irq_drain','cancel_delayed_work_sync(&aw882xx->interrupt_work);','/* omitted */'),('profile_early','\taw882xx_dev_deinit(aw882xx->aw_pa);','\t/* omitted */'),('sysfs_unwind','\t\tsipa_remove_files(si_pa);','\t\t/* omitted */'),('irq_release','\t\tfree_irq(si_pa->irq, si_pa);','\t\t/* omitted */'),('irq_error','if (irq < 0)','if (false)')]
report={'status':'PASS','source':str(args.source),'source_sha256':{str(B/p):hashlib.sha256((B/p).read_bytes()).hexdigest() for p in sources},'controls':{},'limits':'Host tests execute production registration and teardown functions with instrumented kernel primitives; no concurrent hot-unbind or hardware coverage.'}
with tempfile.TemporaryDirectory(prefix='onyx-amplifier-lifetime-') as tmp:
 for name,old,new in [('baseline','','')]+mutations:
  code=body
  if old:assert old in code,name;code=code.replace(old,new,1)
  src=Path(tmp)/(name+'.c');exe=src.with_suffix('');src.write_text((H/'tests/lifecycle.c').read_text().replace('/* PRODUCTION_FUNCTIONS */',code))
  cp=subprocess.run([str(clang),'-std=gnu11','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(src),'-o',str(exe)],capture_output=True,text=True)
  assert cp.returncode==0,cp.stderr
  run=subprocess.run([str(exe)],capture_output=True,text=True)
  if name=='baseline':assert run.returncode==0,run.stderr;report['cases']=int(re.search(r'PASS (\d+)',run.stdout)[1]);report['result']=run.stdout.strip()
  else:assert run.returncode!=0,(name,'survived');report['controls'][name]={'result':'rejected','exit':run.returncode}
args.report.write_text(json.dumps(report,indent=2)+'\n');print(f"PASS {report['cases']} cases, {len(mutations)} rejecting controls")
