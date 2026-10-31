#!/usr/bin/env python3
"""Check extracted battery properties and FG provider callback lifetimes."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S);m=re.search(r'(?m)^[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source)
 if not m:raise ValueError(name)
 e=m.end();depth=1
 while depth:depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[m.start():e]

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('workspace',type=Path);parser.add_argument('--package',default='onyx');parser.add_argument('--support-package',default='onyx');parser.add_argument('--output',type=Path);args=parser.parse_args();base=args.workspace/args.package/'drivers/power/supply/mca'
 sources={k:(base/v).read_text() for k,v in {'battery':'mca_business/business_battery/mca_battery_psy.c','registry':'mca_strategy/strategy_fg/mca_strategy_fg_class.c','panel':'mca_common/mca_qcom_panel.c','business':'mca_business/business_battery/mca_business_battery.c'}.items()}
 groups={'lifetime':['business_battery_psy_init','business_battery_psy_deinit'], 'panel':['mca_panel_get_screen_state','mca_panel_get_hbm_state','mca_panel_event_notifier_callback','mca_panel_register_panel_notifier'], 'battery':['battery_supply_property','battery_scale_value','battery_capacity_level','battery_status','power_supply_read_temp'],'registry':['strategy_class_fg_ops_register','strategy_class_fg_ops_unregister','strategy_class_fg_ops_get_soc','strategy_class_fg_ops_get_charging_done','strategy_class_fg_copy_model_name']};out={}
 with tempfile.TemporaryDirectory(prefix='onyx-chargebusiness-test-') as temp:
  p=Path(temp)
  for key,names in groups.items():
   definitions='\n\n'.join(function(sources['battery' if key=='lifetime' else key],name) for name in names);definitions=definitions.replace('int mca_panel_get_', 'static int mca_panel_get_') if key=='panel' else definitions;template=(HERE/'tests'/('chargebusiness_'+key+'.c')).read_text();(p/'harness.c').write_text(template.replace('/* STAGED_DEFINITIONS */',definitions))
   subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-Werror=implicit-function-declaration',str(p/'harness.c'),'-o',str(p/'harness')],check=True)
   run=subprocess.run([str(p/'harness')],capture_output=True,text=True)
   if run.returncode:raise RuntimeError(key+'\n'+run.stdout+run.stderr)
   out[key]=json.loads(run.stdout)
 voter=(args.workspace/args.support_package/'drivers/power/supply/mca/mca_common/mca_voter.c').read_text();destroy=function(voter,'mca_destroy_votable');assert destroy.index('list_del')<destroy.index('synchronize_srcu')<destroy.index('kfree(votable)');assert 'guard(srcu)' in function(voter,'mca_get_named_client_vote')
 remove=function(sources['business'],'business_battery_remove');assert remove.index('mca_event_block_notify_unregister')<remove.index('business_battery_psy_deinit')
 cleanup=function(sources['battery'],'business_battery_psy_deinit');assert cleanup.index('mca_log_charge_log_unregister')<cleanup.index('thermal_zone_device_unregister')<cleanup.index('power_supply_unregister')
 panel=function(sources['panel'],'mca_panel_remove');assert panel.index('disable_delayed_work_sync')<panel.index('panel_event_notifier_unregister')<panel.index('g_panel = NULL')
 out['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];out['source_sha256']={str(p.relative_to(base)):hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*.c')};out['limitations']='Mock kernel services and pthread SRCU; no actual fuel-gauge policy, power_supply runtime, panel, thermal zone or hardware tests.'
 serialized=json.dumps(out,indent=2)+'\n';print(serialized,end='')
 if args.output:args.output.write_text(serialized)
if __name__=='__main__':main()
