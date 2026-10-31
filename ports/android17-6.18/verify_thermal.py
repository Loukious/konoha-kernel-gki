#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^(?:static )?[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source);assert m,name
 e,d=m.end(),1
 while d:d+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[m.start():e]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args()
 base=a.workspace/a.package/'drivers/power/supply/mca/mca_strategy/strategy_charger';source=(base/'mca_charger_thermal.c').read_text();header=(base/'inc/mca_charger_thermal.h').read_text()
 names=['thermal_validate_level','thermal_vote','thermal_boot_override','thermal_apply_flip','thermal_apply_wired','thermal_apply_wireless','mca_set_wls_charger_thermal_remove','mca_get_wls_charger_thermal_remove','thermal_update_current','thermal_initial_current','thermal_clear_votes','thermal_init_voters','mca_charger_thermal_init_voter_work','thermal_read_table','thermal_parse_dt','thermal_read_control','thermal_write_control','mca_charger_thermal_sysfs_show','mca_charger_thermal_sysfs_store','mca_charger_thermal_create_group','mca_charger_thermal_remove_group','mca_charger_thermal_get_max_level','mca_charger_thermal_get_cur_level','mca_charger_thermal_set_cur_level','mca_charger_thermal_wls_super_sts_callback','mca_charger_thermal_process_event','mca_charge_thermal_dump_log_head','mca_charge_thermal_dump_log_context','thermal_stop','mca_charger_thermal_probe','mca_charger_thermal_remove','mca_charger_thermal_shutdown']
 globals=source[source.index('#define MCA_LOG_TAG'):source.index('static int thermal_validate_level')]
 table=source[source.index('static struct mca_sysfs_attr_info mca_charger_thermal_sysfs_field_tbl'):source.index('static int thermal_read_control')]
 tables=table+source[source.index('static const struct thermal_cooling_device_ops g_mca_charger_tcd_ops'):source.index('static int mca_charger_thermal_wls_super_sts_callback')]+source[source.index('static struct mca_log_charge_log_ops g_mca_thermal_log_ops'):source.index('static void thermal_stop')]
 dtbase=a.workspace/'baseline/kernel/xiaomi/sm8735-devicetrees/qcom';dtfiles=[dtbase/'mca.dtsi',dtbase/'onyx-charger-common.dtsi'];rows=None
 for f in dtfiles:
  text=re.sub(r'/\*.*?\*/|//[^\n]*','',f.read_text(),flags=re.S);block=re.search(r'(?:mca_charger_thermal:\s*|&)mca_charger_thermal\s*\{(.*?)\n\s*\};',text,re.S);assert block
  match=re.search(r'wired_thermal\s*=\s*<([^>]*)>',block.group(1));assert match
  rows=[int(v,0) for v in match.group(1).split()]
 assert len(rows)==150 and rows[0]==1900
 constants='static const u32 stock_wired_table[] = {'+','.join(map(str,rows))+'};\n'
 protocol=(a.workspace/'onyx-dev/drivers/power/supply/mca/include/mca/protocol/protocol_class.h').read_text();constants+=re.search(r'enum xm_charger_type\s*\{.*?\};',protocol,re.S).group(0)+'\n'
 tests=(HERE/'tests/thermal_lifecycle.c').read_text();values=sorted(set(re.findall(r'\b(?:MCA_EVENT_\w+|STRATEGY_(?:FUNC|STATUS)_\w+|MCA_CHARGE_LOG_\w+|MCA_SMARTCHG_IF_CHG_\w+|ADAPTER_PROTOCOL_\w+|FG_IC_MASTER)\b',source+tests)));constants+='enum {'+','.join(values)+'};\n'
 declarations='\n'.join(function(source,n).split('{',1)[0].strip()+';' for n in names)
 tests=tests.replace('/* CONSTANTS */',constants).replace('/* HEADER */',header).replace('/* GLOBALS */',globals).replace('/* DECLARATIONS */',declarations).replace('/* TABLES */',tables).replace('/* FUNCTIONS */','\n'.join(function(source,n) for n in names))
 with tempfile.TemporaryDirectory(prefix='onyx-thermal-tests-') as tmp:
  d=Path(tmp);(d/'test.c').write_text(tests);proc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
  if proc.returncode:raise RuntimeError(proc.stderr)
  run=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
  if run.returncode:raise RuntimeError(run.stdout+run.stderr)
  report=json.loads(run.stdout)
 report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['stock_device_tree']={'wired_levels':15,'support_wireless':0,'source_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in dtfiles}};report['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_charger_thermal.c',base/'inc/mca_charger_thermal.h']};report['limits']='Extracted production thermal functions with pthread locks/SRCU and mocked DT, cooling, registration, workqueue, voter and protocol APIs. Provider voter pointers still use the existing unreferenced mca_find_votable contract. No live kernel lifetime, policy timing, electrical safety, hardware charging or phone validation.'
 out=json.dumps(report,indent=2)+'\n';print(out,end='')
 if a.output:a.output.write_text(out)
if __name__=='__main__':main()
