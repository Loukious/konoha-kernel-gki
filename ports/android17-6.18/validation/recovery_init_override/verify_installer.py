"""Run the actual one-partition installer against guarded fastboot mocks."""
from pathlib import Path
import contextlib,hashlib,io,json,os,runpy,shutil,subprocess,sys,tempfile
V=Path(__file__).resolve().parent
attempt=Path(Path('/tmp/onyx-recovery-init-attempt.txt').read_text().strip())
real_plan=json.loads((attempt/'capture-plan.json').read_text());source=(attempt/'install-recovery-init.py').read_text()
original_run=subprocess.run;original_check=subprocess.check_output;original_argv=sys.argv
cases=['success','wrong-product','wrong-slot','locked','already-userspace','wrong-size','fastbootd-failed','readback-mismatch']
reports=[]
for case in cases:
 with tempfile.TemporaryDirectory(prefix='onyx-recovery-installer-') as tmp:
  root=Path(tmp);stage=root/'stage';stage.mkdir();plan=dict(real_plan);plan['windows_stage']=str(stage)
  shutil.copyfile(Path(json.loads(Path(plan['recovery_report']).read_text())['image']),stage/'recovery.img');(root/'capture-plan.json').write_text(json.dumps(plan));(root/'install.py').write_text(source)
  current=json.loads(Path(plan['current_five_images_report']).read_text());candidate=json.loads(Path(plan['recovery_report']).read_text());expected={'recovery':candidate,**current['images']};calls=[];userspace=False
  def mock_run(args,**kwargs):
   global userspace
   assert args[:3]==['/mnt/d/platform-tools/fastboot.exe','-s','5493f3c6'],args
   op=args[3:];calls.append(op)
   if op[0]=='getvar':
    name=op[1];values={'product':'wrong' if case=='wrong-product' else 'onyx','current-slot':'b' if case=='wrong-slot' else 'a','unlocked':'no' if case=='locked' else 'yes','is-userspace':'yes' if userspace or case=='already-userspace' else 'no','partition-size:recovery_a':hex(candidate['size']-4096 if case=='wrong-size' else candidate['size'])}
    return subprocess.CompletedProcess(args,0,b'',(name+': '+values[name]+'\n').encode())
   if op[0]=='flash':assert op[1]=='recovery_a' and Path(op[2]).read_bytes()==Path(candidate['image']).read_bytes();return subprocess.CompletedProcess(args,0,b'OKAY',b'')
   if op[:2]==['reboot','fastboot']:
    if case=='fastbootd-failed':raise subprocess.TimeoutExpired(args,kwargs['timeout'])
    userspace=True;return subprocess.CompletedProcess(args,0,b'OKAY',b'')
   if op[0]=='fetch':
    name=op[1].removesuffix('_a');dest=Path(op[2]);src=Path(expected[name].get('path',expected[name].get('image')))
    if case=='readback-mismatch' and name=='recovery':dest.write_bytes(b'wrong image')
    else:shutil.copyfile(src,dest)
    return subprocess.CompletedProcess(args,0,b'OKAY',b'')
   assert op==['reboot'];return subprocess.CompletedProcess(args,0,b'OKAY',b'')
  def mock_check(args,**kwargs):assert args[:2]==['wslpath','-w'];return str(args[2])+'\n'
  subprocess.run=mock_run;subprocess.check_output=mock_check;sys.argv=[str(root/'install.py')]
  error=None
  try:
   with contextlib.redirect_stdout(io.StringIO()):runpy.run_path(str(root/'install.py'),run_name='__main__')
  except (AssertionError,subprocess.TimeoutExpired) as e:error=type(e).__name__
  finally:subprocess.run=original_run;subprocess.check_output=original_check;sys.argv=original_argv
  writes=[x for x in calls if x[0]=='flash'];boots=[x for x in calls if x==['reboot']]
  if case=='success':assert error is None and len(writes)==1 and len(boots)==1 and len([x for x in calls if x[0]=='fetch'])==6
  elif case in ['fastbootd-failed','readback-mismatch']:assert error and len(writes)==1 and not boots
  else:assert error and not writes and not boots
  reports.append({'case':case,'status':'PASS','exception':error,'writes':[x[1] for x in writes],'normal_reboots':len(boots)})
report={'status':'PASS','cases':reports,'limits':'Actual installer and real image hashes with fake fastboot responses; no real flash, protocol, device boot or electrical behavior tested.'}
(V/'installer-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
