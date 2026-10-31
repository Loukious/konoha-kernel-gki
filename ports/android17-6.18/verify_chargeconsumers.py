#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
from dt_fixtures import stock_tables
HERE=Path(__file__).resolve().parent
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source)
 if not m:raise ValueError(name)
 e=m.end();depth=1
 while depth:depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[m.start():e]
def structure(source,name):
 start=source.index('struct '+name+' {');e=source.index('{',start)+1;depth=1
 while depth:depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[start:e+1]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('workspace',type=Path);parser.add_argument('--package',default='onyx');parser.add_argument('--basp-package',default='onyx');parser.add_argument('--support-package',default='onyx');parser.add_argument('--output',type=Path);a=parser.parse_args()
 base=a.workspace/a.package/'drivers/power/supply/mca';wired=(base/'mca_strategy/strategy_charger/mca_strategy_quickchg.c').read_text();jeita=(base/'mca_strategy/strategy_charger/mca_buckchg_jeita.c').read_text()
 smartbase=a.workspace/a.basp_package/'drivers/power/supply/mca';smart=(smartbase/'mca_smart_charge/mca_smart_charge.c').read_text();header=(smartbase/'include/mca/smartchg/smart_chg_class.h').read_text();header=re.sub(r'^#include.*\n','',header,flags=re.M)
 wh=(base/'mca_strategy/strategy_charger/inc/mca_quick_charger.h').read_text();jh=(base/'mca_strategy/strategy_charger/inc/mca_buckchg_jeita.h').read_text()
 structs='\n'.join(structure(wh,n) for n in ['mca_quick_charge_volt_para','mca_quick_charge_temp_para','mca_quick_charge_volt_para_info','mca_quick_charge_temp_para_info','mca_quick_charge_batt_para_info'])+'\n'+'\n'.join(structure(jh,n) for n in ['mca_buckchg_jeita_volt_data','mca_buckchg_jeita_volt_para','mca_buckchg_jeita_data','mca_buckchg_jeita_para'])+'\n'+structure(wired,'mca_qc_basp_pending')
 funcs='\n'.join(function(jeita,n) for n in ['mca_buckchg_jeita_get_curr','mca_jeita_validate_table','mca_jeita_basp_prepare','mca_jeita_basp_abort','mca_jeita_basp_commit'])+'\n'+'\n'.join(function(wired,n) for n in ['mca_qc_free_batt_para','mca_qc_free_tables','mca_qc_validate_batt_para','mca_quick_charge_set_cp_path_enable','mca_qc_basp_abort','mca_qc_basp_prepare','mca_qc_basp_commit','mca_quick_charge_jump_stage','mca_quick_charge_chg_disable_voter_cb','mca_quick_charge_remove'])+'\n'+function(smart,'smart_basp_dispatch')
 common=(HERE/'tests/consumers_common.h').read_text().replace('/* HEADER */',header).replace('/* TABLE_STRUCTS */',structs).replace('/* FUNCTIONS */',funcs)
 dt_path=a.workspace/'baseline/kernel/xiaomi/sm8735-devicetrees/qcom/onyx-charger-common.dtsi'
 stock_code,stock_report=stock_tables(dt_path)
 result={'stock_tables':stock_report,'device_tree_sha256':hashlib.sha256(dt_path.read_bytes()).hexdigest()}
 supportbase=a.workspace/a.support_package/'drivers/power/supply/mca';interface=(supportbase/'mca_common/mca_charge_interface.c').read_text();ih=(supportbase/'include/mca/common/mca_charge_interface.h').read_text();ih=re.sub(r'^#include.*\n','',ih,flags=re.M)
 interface_common=(HERE/'tests/consumers_common.h').read_text().split('/* HEADER */')[0]+ih+'\n'
 interface_common+='''
#define __rcu
#define DEFINE_MUTEX(name) struct mutex name = {PTHREAD_MUTEX_INITIALIZER}
#define DEFINE_STATIC_SRCU(name) pthread_rwlock_t name=PTHREAD_RWLOCK_INITIALIZER
#define srcu_dereference(p,s) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_access_pointer(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
#define rcu_dereference_protected(p,c) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define lockdep_is_held(p) true
static void synchronize_srcu(pthread_rwlock_t *s) {assert(!pthread_rwlock_wrlock(s));assert(!pthread_rwlock_unlock(s));}
static int kstrtouint(const char *s,int base,unsigned int *v) {char *end;errno=0;unsigned long x=strtoul(s,&end,base);if(errno || end==s || *end || x>UINT32_MAX || *s=='-')return -EINVAL;*v=x;return 0;}
'''
 interface_common+=interface[interface.index('enum mca_charge_if_sysfs_type {'):interface.index('static int mca_charge_if_get_op_type(')]
 interface_common+='\n'.join(function(interface,n) for n in ['mca_charge_if_get_op_type','mca_charge_if_get_ops','mca_charge_if_op_set','mca_charge_if_ops_register','mca_charge_if_ops_unregister'])
 with tempfile.TemporaryDirectory(prefix='onyx-chargeconsumers-') as tmp:
  d=Path(tmp)
  for group in ['basp','tables','stock','interface']:
   body=(HERE/('tests/consumers_'+group+'.c')).read_text() if group!='stock' else stock_code+'\nint main(void) {test_stock_tables();puts("{\\\"selected_stock_tables\\\":true}");}\n'
   test=d/'test.c';test.write_text((interface_common if group=='interface' else common)+'\n'+body);out=d/'test'
   subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(test),'-o',str(out)],check=True)
   run=subprocess.run([str(out)],capture_output=True,text=True,timeout=20)
   if run.returncode:raise RuntimeError(run.stdout+run.stderr)
   result[group]=json.loads(run.stdout)
 for source,prefix in [(wired,'mca_quick_charge'),(jeita,'mca_buckchg_jeita')]:
  assert 'mca_find_votable(' not in source
  remove=function(source,prefix+'_remove' if prefix=='mca_quick_charge' else prefix+'_stop')
  assert remove.index('mca_smartchg_if_ops_unregister')<remove.index('mutex_lock')
  assert remove.index('disable_delayed_work_sync')<remove.index('destroy_vote' if prefix=='mca_quick_charge' else 'mca_destroy_votable')
 assert 'mutex_trylock' in function(wired,'mca_qc_basp_prepare')
 assert 'guard(srcu)' in function(interface,'mca_charge_if_op_set') and 'guard(srcu)' in function(interface,'mca_charge_if_op_get')
 for name in ['mca_quick_charge_chg_disable_voter_cb','mca_quick_charge_chg_en_voter_cb']:
  body=function(wired,name);assert 'schedule_work' in body and 'cancel_delayed_work_sync' not in body and 'force_stop_charging' not in body
 for name in ['mca_buckchg_base_jeita_update','mca_buckchg_flip_jeita_update','mca_buckchg_jeita_update']:
  assert re.search(r'vbat_index > 0\s*&&',function(jeita,name))
 result['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];result['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_strategy/strategy_charger'/n for n in ['mca_strategy_quickchg.c','mca_buckchg_jeita.c','inc/mca_quick_charger.h','inc/mca_buckchg_jeita.h']]};result['limits']='Extracted production callbacks with real pthread mutex/SRCU-reader models and mocked kernel work/voters. No live kernel workqueue, charging firmware, device boot or hardware charging validation.'
 result['support_sha256']={str(f.relative_to(supportbase)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [supportbase/n for n in ['mca_common/mca_charge_interface.c','include/mca/common/mca_charge_interface.h','mca_common/mca_voter.c','include/mca/common/mca_voter.h']]}
 text=json.dumps(result,indent=2)+'\n';print(text,end='')
 if a.output:a.output.write_text(text)
if __name__=='__main__':main()
