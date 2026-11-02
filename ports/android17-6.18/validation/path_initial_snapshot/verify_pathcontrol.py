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
 base=a.workspace/a.package/'drivers/power/supply/mca';source=(base/'mca_hardware_monitor/mca_path_control.c').read_text();header=(base/'mca_hardware_monitor/inc/mca_path_control.h').read_text();api=(base/'include/mca/hardware/hw_path_control.h').read_text().replace('#include <linux/types.h>','')
 names=['path_string_int','path_string_count','path_parse_schemes','path_parse_actions','path_parse_boosts','path_parse_dt','path_control_gate','path_apply_condition','path_read_initial_status','path_set_source','mca_path_control_enable_gate','mca_path_control_update_status_work','path_control_sysfs_show','path_control_sysfs_store','mca_path_control_sysfs_create_group','mca_path_control_sysfs_remove_group','path_stop','mca_path_control_probe','mca_path_control_remove','mca_path_control_shutdown']
 globals=source[source.index('#define MCA_LOG_TAG'):source.index('static int path_string_int')];table=source[source.index('static struct mca_sysfs_attr_info path_control_sysfs_field_tbl'):source.index('static ssize_t path_control_sysfs_show',source.index('static const struct attribute_group'))]
 dtbase=a.workspace/'baseline/kernel/xiaomi/sm8735-devicetrees/qcom';dtfiles=[dtbase/'mca.dtsi',dtbase/'onyx-charger-common.dtsi'];properties={}
 for f in dtfiles:
  txt=re.sub(r'/\*.*?\*/|//[^\n]*','',f.read_text(),flags=re.S);block=re.search(r'(?:mca_path_control:\s*|&)mca_path_control\s*\{(.*?)\n\s*\};',txt,re.S);assert block
  for match in re.finditer(r'([\w,-]+)\s*=\s*([^;]+);',block.group(1)):
   vals=re.findall(r'"([^"]*)"',match.group(2))
   if vals:properties[match.group(1)]=vals
 assert len(properties['path_condition'])==20 and properties['control_scheme'][:3]==['0','2','ovpgate,cp_chip_role']
 managed=[int(properties['control_scheme'][i]) for i in range(0,len(properties['control_scheme']),3) if properties['control_scheme'][i+2]!='null'];assert managed==[0]
 constants='';entries=[]
 for index,(name,values) in enumerate(properties.items()):
  ident='dt_property_'+str(index);constants+='static const char *'+ident+'[] = {'+','.join(json.dumps(v) for v in values)+'};\n';entries.append('{'+json.dumps(name)+','+ident+',ARRAY_SIZE('+ident+')}')
 constants+='struct dt_property {const char *name;const char **values;int count;};\nstatic const struct dt_property dt_properties[]={'+','.join(entries)+'};\n'
 tests=(HERE/'tests/pathcontrol_lifecycle.c').read_text();constants+='enum { MAIN_BUCK_CHARGER, SYSFS_DEV_5 };\n'
 for file,enum in [('platform/platform_cp_class.h','mca_cp_role'),('strategy/strategy_class.h','mca_wls_boost_src')]:
  real=(a.workspace/a.package/'drivers/power/supply/mca/include/mca'/file).read_text();match=re.search(r'enum '+enum+r'\s*\{.*?\};',real,re.S)
  if not match and enum=='mca_cp_role':match=re.search(r'enum [\w ]+\s*\{\s*CP_ROLE_MASTER.*?\};',real,re.S)
  assert match,file
  constants+=match.group(0)+'\n'
 declarations='\n'.join(function(source,n).split('{',1)[0].strip()+';' for n in names)
 tests=tests.replace('/* CONSTANTS */',constants).replace('/* HEADER */',api+'\n'+header).replace('/* GLOBALS */',globals).replace('/* DECLARATIONS */',declarations).replace('/* TABLE */',table).replace('/* FUNCTIONS */','\n'.join(function(source,n) for n in names))
 with tempfile.TemporaryDirectory(prefix='onyx-pathcontrol-tests-') as tmp:
  d=Path(tmp);(d/'test.c').write_text(tests);proc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
  if proc.returncode:raise RuntimeError(proc.stderr)
  run=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
  if run.returncode:raise RuntimeError(run.stdout+run.stderr)
  report=json.loads(run.stdout)
 report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['stock_device_tree']={'conditions':[int(v) for v in properties['path_condition'][::2]],'managed_roles':managed,'source_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in dtfiles}};report['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_hardware_monitor/mca_path_control.c',base/'mca_hardware_monitor/inc/mca_path_control.h',base/'include/mca/hardware/hw_path_control.h']};report['limits']='Extracted production path-control functions with pthread locks/SRCU and mocked DT/devm/workqueue/GPIO/CP/buck APIs; optional actual wireless coordination is disabled as for Onyx. No live kernel lifetime, firmware, electrical safety, actual gate state, charging or phone validation.'
 out=json.dumps(report,indent=2)+'\n';print(out,end='')
 if a.output:a.output.write_text(out)
if __name__=='__main__':main()
