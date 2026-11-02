"""Rebuild matched recovery init with the original Android compiler/link settings."""
from pathlib import Path
import hashlib,json,os,shlex,shutil,subprocess
V=Path(__file__).resolve().parent
R=Path('/home/loukious/Android/custom-rom')
commands=json.loads((V/'original-commands.json').read_text())
targets=json.loads((V/'selected-targets.json').read_text())
clang=R/'prebuilts/clang/host/linux-x86/clang-r596125/bin'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
B=V/'build';B.mkdir(exist_ok=False)
def argv(command):
 parts=shlex.split(command);env=dict(os.environ)
 while parts and '=' in parts[0] and not parts[0].startswith('-'):
  name,val=parts.pop(0).split('=',1);env[name]=val
 # Bypass the cache for this independently audited rebuild.
 if parts and parts[0]=='/usr/bin/ccache':parts.pop(0)
 return parts,env
executed=[]
def run(parts,env,label):
 executed.append({'label':label,'argv':parts})
 with (B/(label+'.log')).open('wb') as log:
  result=subprocess.run(parts,cwd=R,env=env,stdout=log,stderr=subprocess.STDOUT)
 if result.returncode:raise RuntimeError(label+' failed: '+(B/(label+'.log')).read_text(errors='replace')[-4000:])
 print('PASS',label,flush=True)
def link_strip(kind,archive):
 directory=B/kind;(directory/'unstripped').mkdir(parents=True,exist_ok=True)
 output=directory/'unstripped/init';rsp=Path(str(output)+'.rsp');original_rsp=Path(str(R/targets['unstripped'])+'.rsp')
 text=original_rsp.read_text();assert text.count(targets['archive'])==1
 rsp.write_text(text.replace(targets['archive'],str(archive)))
 args,env=argv(commands['unstripped']);args=[str(output) if s==targets['unstripped'] else '@'+str(rsp) if s=='@'+targets['unstripped']+'.rsp' else s for s in args]
 run(args,env,kind+'-link')
 args,env=argv(commands['stripped']);args=[str(output) if s==targets['unstripped'] else str(directory/'init') if s==targets['stripped'] else str(directory/'init.d') if s==targets['stripped']+'.d' else s for s in args]
 run(args,env,kind+'-strip');return directory/'init'
baseline=link_strip('baseline',R/targets['archive'])
assert baseline.read_bytes()==(R/targets['stripped']).read_bytes(),'Unmodified link/strip reproduction differs from original recovery init'
print('PASS baseline reproduces shipping recovery init byte-for-byte',flush=True)
obj=B/'init.o';src=V/'candidate/init.cpp';args,env=argv(commands['object']);assert args[-1]=='system/core/init/init.cpp',args[-3:]
args=[str(obj) if s==targets['object'] else str(obj)+'.d' if s==targets['object']+'.d' else str(src) if s=='system/core/init/init.cpp' else s for s in args]
run(args,env,'candidate-compile')
archive=B/'libinit.a';shutil.copyfile(R/targets['archive'],archive)
# Replace the one corresponding archive member; preserve every other object.
names=subprocess.check_output([str(clang/'llvm-ar'),'t',str(archive)],text=True).splitlines();assert names.count('init.o')==1
run([str(clang/'llvm-ar'),'rD',str(archive),str(obj)],dict(os.environ),'candidate-archive')
new=link_strip('candidate',archive)
assert sha(new)!=sha(baseline)
report={'status':'PASS: original recovery binary reproduced; corrected source compiled and linked','shipping_init_sha256':sha(baseline),'candidate_init_sha256':sha(new),'candidate_init':str(new),'source_sha256':sha(src),'compiler_sha256':sha(clang/'clang++'),'commands':executed,'limits':'Build and exact original-binary reproduction only; init selection host tests and recovery image/hardware checks separate.'}
(V/'binary-build.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['status','shipping_init_sha256','candidate_init_sha256']},indent=2))
