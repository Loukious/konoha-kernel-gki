#!/usr/bin/env python3
"""Exercise extracted Onyx FG sampling, smoothing and lifecycle functions."""
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
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('workspace',type=Path);p.add_argument('--package',default='onyx');p.add_argument('--support-package',default='onyx');p.add_argument('--output',type=Path);a=p.parse_args()
 base=a.workspace/a.package/'drivers/power/supply/mca/mca_strategy/strategy_fg'
 fg=(base/'mca_strategy_fg.c').read_text();shutdown=(base/'mca_battery_shutdown.c').read_text();smooth=(base/'mca_soc_smooth.c').read_text()
 headers='\n'.join(re.sub(r'^#include.*\n','', (base/'inc'/f).read_text(),flags=re.M) for f in ['mca_battery_shutdown.h','strategy_fg.h','mca_soc_smooth.h'])
 groups={'sampling':[(fg,['strategy_fg_get_dod_count','strategy_fg_get_batt_info','mca_strategy_update_fg_info','strategy_fg_record_curr_mean','strategy_fg_record_volt_mean','strategy_fg_ops_get_soc','strategy_fg_ops_get_curr','strategy_fg_ops_get_temp','strategy_fg_ops_get_soc_decimal'])], 'smoothing':[(smooth,['get_unit_time_low_soc','fg_update_mapped_soc','fg_ui_soc_process_special_conditions','fg_ui_soc_update_decimal','fg_ui_soc_smooth']),(shutdown,['mca_battery_shutdown_validate_tables','mca_battery_shutdown_update_vcutoff_para','mca_battery_shutdown_heavy_load_check'])], 'lifetime':[(fg,['strategy_fg_stop_work','strategy_fg_probe','strategy_fg_remove'])], 'events':[(fg,['strategy_fg_process_event','strategy_fg_ops_set_charging_done'])]}
 result={}
 testroot=HERE/'tests'
 common=(testroot/'fgstrategy_common.h').read_text().replace('/* FG_HEADER */',headers)
 with tempfile.TemporaryDirectory(prefix='onyx-fgstrategy-tests-') as tmp:
  d=Path(tmp)
  for key,inputs in groups.items():
   code='\n\n'.join(function(src,name) for src,names in inputs for name in names)
   harness=common+'\n'+(testroot/('fgstrategy_'+key+'.c')).read_text().replace('/* STAGED_DEFINITIONS */',code)
   (d/'harness.c').write_text(harness)
   subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-Werror=implicit-function-declaration',str(d/'harness.c'),'-o',str(d/'harness')],check=True)
   run=subprocess.run([str(d/'harness')],capture_output=True,text=True)
   if run.returncode:raise RuntimeError(key+'\n'+run.stdout+run.stderr)
   result[key]=json.loads(run.stdout)
 for name in ['mca_vote_named','mca_vote_named_override','mca_get_named_effective_result','mca_is_named_override_vote_enabled','mca_named_votable_exists']:
  voter=(a.workspace/a.support_package/'drivers/power/supply/mca/mca_common/mca_voter.c').read_text();assert 'guard(srcu)' in function(voter,name)
 assert 'fg_update_status' not in function(fg,'strategy_fg_process_event')
 assert 'fg->cfg.fg_type != MCA_FG_TYPE_SINGLE' in function(fg,'strategy_fg_parse_dt')
 assert function(fg,'strategy_fg_probe').index('WRITE_ONCE(fg->ready, true)')>function(fg,'strategy_fg_probe').index('strategy_fg_auth_create_group')
 assert 'wired_present' in function(fg,'strategy_fg_process_event') and 'wireless_present' in function(fg,'strategy_fg_process_event')
 pins=['mca_strategy_fg.c','mca_soc_smooth.c','mca_battery_shutdown.c','inc/strategy_fg.h','inc/mca_soc_smooth.h','inc/mca_battery_shutdown.h','inc/xm_battery_auth.h']
 result['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];result['source_sha256']={f:hashlib.sha256((base/f).read_bytes()).hexdigest() for f in pins};result['harness_sha256']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in testroot.glob('fgstrategy*')};result['limits']='Extracted functions with mocked kernel/provider services. Does not validate actual battery/gauge calibration, live workqueue/SRCU/IRQ semantics, firmware updates, boot or phone charging.'
 encoded=json.dumps(result,indent=2)+'\n';print(encoded,end='')
 if a.output:a.output.write_text(encoded)
if __name__=='__main__':main()
