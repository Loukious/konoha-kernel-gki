from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile,os
H=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--source',type=Path,default=H/'private-source');ap.add_argument('--output',type=Path,required=True);ap.add_argument('--suite',action='append');ap.add_argument('--negative',action='store_true');args=ap.parse_args()
clang=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/prebuilts/clang/host/linux-x86/clang-r584948c/bin/clang')
def function(text,name):
 m=re.search(r'(?m)^[^\n;]*\b'+re.escape(name)+r'\([^;]*?\n\{',text);assert m,name
 brace=text.index('{',m.start());depth=0
 for item in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',text[brace:],re.S):
  if item.group()=='{':depth+=1
  if item.group()=='}':
   depth-=1
   if not depth:return text[m.start():brace+item.end()]
 raise AssertionError(name)
specs={
'mhi':('net/qrtr/mhi.c',['qcom_mhi_qrtr_ul_callback','qcom_mhi_qrtr_send','qcom_mhi_qrtr_stop','qrtr_mhi_of_parse','qcom_mhi_qrtr_probe','qcom_mhi_qrtr_remove'],{
 'missing_stop_wakeup':('complete_all(&qdev->prepared);\n\tcomplete_all(&qdev->ringfull);','(void)qdev;'),
 'retry_socket_leak':('reinit_completion(&qdev->ringfull);','reinit_completion(&qdev->ringfull); if (skb->sk) sock_hold(skb->sk);'),
 'uninitialized_latency':('*rt = false;','(void)rt;'),
 'missing_failure_cleanup':('qcom_mhi_qrtr_stop(qdev);\n\t\tqrtr_endpoint_unregister(&qdev->ep);','qcom_mhi_qrtr_stop(qdev);'),
})}
reports=[]
for name,(path,names,mutations) in specs.items():
 if args.suite and name not in args.suite:continue
 source=(args.source/path).read_text();code='\n\n'.join(function(source,n) for n in names);cases={"production":None,**(mutations if args.negative else {})};results=[]
 with tempfile.TemporaryDirectory(prefix='onyx-transport-'+name+'-') as td:
  td=Path(td);(td/'test.c').write_bytes((H/'tests'/f'{name}.c').read_bytes())
  for case,mutation in cases.items():
   selected=code
   if mutation:
    old,new=mutation;assert selected.count(old)==1,(case,selected.count(old));selected=selected.replace(old,new)
   (td/'production.inc').write_text(selected)
   result=subprocess.run([str(clang),'-std=gnu11','-O1','-g','-Wall','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(td/'test.c'),'-o',str(td/case)],capture_output=True,text=True)
   assert not result.returncode,(name,case,result.stderr)
   run=subprocess.run([str(td/case)],capture_output=True,text=True,timeout=10)
   assert (run.returncode==0)==(case=='production'),(name,case,run.stdout,run.stderr)
   results.append({'case':case,'status':'PASS' if not run.returncode else 'REJECTED','log':(run.stdout+run.stderr)[-4000:]})
 reports.append({'suite':name,'source':path,'sha256':hashlib.sha256((args.source/path).read_bytes()).hexdigest(),'results':results})
args.output.write_text(json.dumps({'status':'PASS','suites':reports},indent=2)+'\n');print('PASS',', '.join(r['suite'] for r in reports))
