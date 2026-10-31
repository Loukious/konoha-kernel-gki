from pathlib import Path
import argparse,subprocess,tempfile,json,re,hashlib,resource
ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path);ap.add_argument('--output',type=Path);ap.add_argument('--negative-controls',action='store_true');a=ap.parse_args();h=Path(__file__).resolve().parent;root=a.source_root or h/'private-source';b=root/'qcom/opensource/datarmnet-ext'
def fn(path,name):
 s=(b/path).read_text();m=re.search(r'\b'+name+r'\s*\([^;{}]*\)\s*\{',s);assert m,name
 start=max(s.rfind(';',0,m.start()),s.rfind('}',0,m.start()))+1;i=m.end();depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i].strip()+'\n'
fragments={'drains.inc':''.join(fn(p,n) for p,n in [('perf/rmnet_perf_tcp.c','DATARMNET38bb6f2b7a'),('perf/rmnet_perf_udp.c','DATARMNET4b5170a1ef'),('wlan/rmnet_wlan_fragment.c','DATARMNET8c0e010dfb'),('wlan/rmnet_wlan_connection.c','DATARMNETf4e1a29dbc')]),'perf_lifecycle.inc':fn('perf/rmnet_perf_main.c','DATARMNET63abbdc3d3')+fn('perf/rmnet_perf_main.c','DATARMNETa343229e33')}
controls=[];runs={}
with tempfile.TemporaryDirectory(prefix='rmnet-ext-tests-') as td:
 d=Path(td)
 for n,s in fragments.items():(d/n).write_text(s)
 commands={}
 for group in ['lifetime']:
  cmd=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie','-I',str(d),str(h/'tests'/('rmnet_'+group+'.c')),'-o',str(d/group)];commands[group]=cmd
  subprocess.run(cmd,check=True);p=subprocess.run([str(d/group)],check=True,text=True,stdout=subprocess.PIPE,timeout=30);print(p.stdout.strip());runs[group]=json.loads(p.stdout)
 if a.negative_controls:
  variants=[('perf_success_cleanup','perf_lifecycle.inc','\treturn 0;','', 'lifetime'),('perf_missing_rcu_barrier','perf_lifecycle.inc','\trcu_barrier();','\tif (barriers) rcu_barrier();','lifetime')]
  for i in range(4):
   old=list(re.finditer(r'flush_delayed_work\([^;]+;',fragments['drains.inc']))[i].group();variants.append(('purge_cancel_'+str(i),'drains.inc',old,old.replace('flush_delayed_work','cancel_delayed_work_sync'),'lifetime'))
  for name,frag,old,new,group in variants:
   p=d/frag;original=p.read_text();assert old in original,name;p.write_text(original.replace(old,new,1));subprocess.run(commands[group],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   result=subprocess.run([str(d/group)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)));p.write_text(original);assert result.returncode!=0,'Survived '+name;controls.append({'name':name,'rejected':True,'returncode':result.returncode});print('REJECTED',name)
report={'status':'PASS','groups':runs,'negative_controls':controls,'source_sha256':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in json.loads((h/'sources.json').read_text())},'limits':'Actual extracted functions with modeled kernel services; no kernel networking, firmware or hardware execution.'}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
