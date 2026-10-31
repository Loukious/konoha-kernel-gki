#!/usr/bin/env python3
"""Exercise extracted charging telemetry, policy, lifecycle and provider functions."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source)
 if not m:raise ValueError(name)
 e=m.end();depth=1
 while depth:depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[m.start():e]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('workspace',type=Path);p.add_argument('--package',default='onyx');p.add_argument('--support-package',default='onyx');p.add_argument('--header-package',default='onyx');p.add_argument('--output',type=Path);args=p.parse_args();base=args.workspace/args.package/'drivers/power/supply/mca';support=args.workspace/args.support_package/'drivers/power/supply/mca'
 smart=(base/'mca_smart_charge/mca_smart_charge.c').read_text();telemetry=(base/'mca_common/mca_charge_mievent.c').read_text();voter=(support/'mca_common/mca_voter.c').read_text();head=(base/'mca_smart_charge/inc/mca_smart_charge.h').read_text();th=(args.workspace/args.header_package/'drivers/power/supply/mca/include/mca/common/mca_charge_mievent.h').read_text();th=re.sub(r'^#include.*\n','',th,flags=re.M)
 groups={'telemetry':(telemetry,['mievent_valid','mca_charge_mievent_report','mca_charge_mievent_set_state']),'policy':(smart,['smartcharging_receive_control_message','handle_smart_batt_message','smartcharging_handle_algorithm_conflict','smart_charge_soc_limit_workfunc','mca_smart_charge_process_event','smart_charge_file_open']),'registry':(smart,['mca_smartchg_if_get_ops','mca_smartchg_if_ops_register','mca_smartchg_if_ops_unregister','smart_charge_fv_control']),'lifetime':(smart,['smart_charge_stop_work','smart_charge_probe','smart_charge_remove'])}
 out={}
 with tempfile.TemporaryDirectory(prefix='onyx-smartcharge-tests-') as tmp:
  d=Path(tmp)
  for key,(source,names) in groups.items():
   template=(HERE/'tests'/('smartcharge_'+key+'.c')).read_text();code='\n\n'.join(function(source,n) for n in names)
   if key=='telemetry':
    tables=telemetry[telemetry.index('static const struct charge_mievent_info'):telemetry.index('static DEFINE_MUTEX(mievent_lock)')];code=tables+'\n'+code
   template=template.replace('/* TELEMETRY_HEADER */',th).replace('/* SMART_HEADER */',head.replace('struct mutex', 'pthread_mutex_t')).replace('/* STAGED_DEFINITIONS */',code);(d/'harness.c').write_text(template)
   subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-Werror=implicit-function-declaration',str(d/'harness.c'),'-o',str(d/'harness')],check=True)
   run=subprocess.run([str(d/'harness')],capture_output=True,text=True)
   if run.returncode:raise RuntimeError(key+'\n'+run.stdout+run.stderr)
   out[key]=json.loads(run.stdout)
 assert 'mmap_addr' not in smart and 'remap_pfn_range' not in smart
 assert 'guard(srcu)' in function(smart,'smart_charge_set_soc_limit')
 create=function(voter,'mca_create_votable');assert create.index('votable->default_value')<create.index('list_add') and 'ERR_PTR(-EEXIST)' in create
 assert 'votable->voted_on = !rc' in function(voter,'mca_vote')
 for name in ['mca_smartchg_get_scene','mca_smartchg_get_board_temp','mca_smartchg_is_extreme_cold_enabled','mca_smartchg_get_limit_soc']:
  assert 'guard(srcu)' in function(smart,name)
 out['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];out['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in base.rglob('*.c')};out['limitations']='Extracted functions with mocked kernel services and pthread lifetime model; no live kernel, hardware charging, firmware, boot or real MIEV delivery.'
 encoded=json.dumps(out,indent=2)+'\n';print(encoded,end='')
 if args.output:args.output.write_text(encoded)
if __name__=='__main__':main()
