#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S);m=re.search(r'(?m)^(?:static )?[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source);assert m,name;e,d=m.end(),1
 while d:d+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[m.start():e]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args();base=a.workspace/a.package/'drivers/power/supply/mca/mca_strategy/strategy_pd'
 source=(base/'mca_pd_auth.c').read_text();header=re.sub(r'^#include[^\n]*\n','',(base/'pd_auth.h').read_text(),flags=re.M)
 pd=(a.workspace/'onyx/drivers/power/supply/mca/include/mca/protocol/protocol_pd_class.h').read_text();protocol=(a.workspace/'onyx/drivers/power/supply/mca/include/mca/protocol/protocol_class.h').read_text()
 # Keep the real wire command numbers and structures instead of invented enums.
 constants='\n'.join(re.findall(r'^#define (?:PROTOCOL_PD_MAX_|USBPD_UVDM_).*',pd,re.M));constants+='\n'+pd[pd.index('enum mca_pd_dr_request'):pd.index('struct protocol_class_pd_ops')]
 constants+='\nenum {TYPEC_PORT_0=0,TYPEC_PORT_1,TYPEC_PORT_MAX};\n#define MAIN_BUCK_CHARGER 0\n#define XM_ADAPTER_SVID 0x2717\n#define CHARGE_DFX_PD_AUTH_FAILED 1\n#define MCA_EVENT_TYPE_CHARGE_TYPE 1\n#define MCA_EVENT_CHARGE_VERIFY_PROCESS_END 2\n#define MCA_EVENT_CHARGE_TYPE_CHANGE 3\nenum {XM_CHARGER_TYPE_UNKNOW=0,XM_CHARGER_TYPE_PD_VERIFY};\n'
 table=source[source.index('static struct mca_sysfs_attr_info strategy_pd_auth_sysfs_field_tbl[]'):source.index('static int strategy_pd_auth_get_vdm_cmd')]
 names=['strategy_pd_auth_get_vdm_cmd','strategy_pd_auth_is_pd_adapter','strategy_pd_auth_sysfs_show','strategy_pd_auth_string2hex','strategy_pd_auth_set_vdm_cmd','strategy_pd_auth_fail_report_dfx','strategy_pd_auth_sysfs_store','strategy_pd_auth_create_group','strategy_pd_auth_remove_group','strategy_pd_auth_probe','strategy_pd_auth_stop','strategy_pd_auth_remove','strategy_pd_auth_shutdown']
 functions='\n'.join(function(source,n) for n in names);declarations='\n'.join(function(source,n).split('{',1)[0].strip()+';' for n in names)
 test=(HERE/'tests/pdauth_controls.c').read_text().replace('/* HEADER */',header).replace('/* CONSTANTS */',constants).replace('/* TABLE */',table).replace('/* DECLARATIONS */',declarations).replace('/* FUNCTIONS */',functions)
 with tempfile.TemporaryDirectory(prefix='onyx-pdauth-tests-') as tmp:
  d=Path(tmp);(d/'test.c').write_text(test);proc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],text=True,capture_output=True)
  if proc.returncode:raise RuntimeError(proc.stderr)
  run=subprocess.run([str(d/'test')],text=True,capture_output=True,timeout=30)
  if run.returncode:raise RuntimeError(run.stdout+run.stderr)
  report=json.loads(run.stdout)
 report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['source_sha256']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_pd_auth.c',base/'pd_auth.h']};report['limits']='Extracted production PD-auth functions with pthread locks and mocked sysfs/provider/VDM/devm APIs. No actual authentication, firmware transport, kernel sysfs lifetime, charging or phone validation.'
 out=json.dumps(report,indent=2)+'\n';print(out,end='')
 if a.output:a.output.write_text(out)
if __name__=='__main__':main()
