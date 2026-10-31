#!/usr/bin/env python3
"""Run extracted MCA protocol/storage functions under ASan and UBSan."""
import argparse, hashlib, json, re, subprocess, tempfile
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
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('workspace',type=Path);parser.add_argument('--package',default='onyx');parser.add_argument('--output',type=Path);args=parser.parse_args()
 base=args.workspace/args.package/'drivers/power/supply/mca'
 paths={'registry':'mca_protocol/mca_protocol_class.c','pd':'mca_protocol/protocol_pd/mca_protocol_pd_class.c','wire':'mca_protocol/protocol_pd/qcom_tcpc/qcom_adsp_pd_protocol.c','partition':'mca_shared_memory/charger_partition.c','subpmic':'mca_qcom_subpmic_proxy/mca_qcom_subpmic_proxy.c'}
 sources={k:(base/v).read_text() for k,v in paths.items()}
 groups={'notify': [('subpmic',['qcom_subpmic_notify_cb','qcom_subpmic_enable_charging','qcom_subpmic_set_term_volt','qcom_subpmic_set_input_current_limit'])], 'registry': [('registry',['protocol_class_get_protocol_data','protocol_class_register_ops','protocol_class_unregister_ops','protocol_class_get_adapter_max_power'])],
 'partition':[('partition',['charger_partition_request_valid','charger_partition_io','charger_partition_alloc','charger_partition_transaction_matches','charger_partition_dealloc','charger_partition_read','charger_partition_write','charger_partition_field'])],
 'wire':[('wire',['adsp_pd_protocol_get_pps_max_power','adsp_pd_protocol_select_pps_pdo','adsp_pd_protocol_get_pdos','adsp_pd_protocol_get_current_state','adsp_pd_vdm_bswap4','adsp_pd_protocol_request_vdm_cmd']),('pd',['protocol_class_pd_collect_caps','protocol_class_pps_adapter_get_adv_current'])]}
 headers={k:(base/'include/mca'/v).read_text() for k,v in [('partition','shared_memory/charger_partition_class.h'),('class','protocol/protocol_class.h'),('pd','protocol/protocol_pd_class.h')]}
 out={}
 with tempfile.TemporaryDirectory(prefix='onyx-chargepolicy-test-') as temp:
  p=Path(temp)
  for group,entries in groups.items():
   definitions='\n\n'.join(function(sources[key],name) for key,names in entries for name in names)
   template=(HERE/'tests'/('chargepolicy_'+group+'.c')).read_text()
   header=''
   if group=='partition':header=headers['partition'][headers['partition'].index('#define CHARGER_PARTITION_MAGIC'):headers['partition'].index('/*\n * A successful alloc')]
   if group=='wire':
    header='\n'.join(re.findall(r'(?:enum \w+|struct (?:pd_pdo|adapter_power_cap|adapter_power_cap_info))\s*\{.*?\};',headers['pd']+headers['class'],flags=re.S))
   enum=re.search(r'enum adsp_pd_prop\s*\{.*?\};',sources['wire'],flags=re.S)[0]
   if group=='notify':
    enum='\n'.join(re.findall(r'enum mca_subpmic_\w+\s*\{.*?\};',sources['subpmic'],flags=re.S))
    template=template.replace('/* EVENT_DEFINITIONS */','\n'.join('#define '+n+' 1' for n in sorted(set(re.findall(r'\b(?:MCA_EVENT_\w+|STRATEGY_FUNC_\w+)\b',definitions)))))
   text=template.replace('/* STAGED_DEFINITIONS */',definitions).replace('/* HEADER_DEFINITIONS */',header).replace('/* ENUM_DEFINITIONS */',enum)
   (p/'harness.c').write_text(text)
   subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-Werror=implicit-function-declaration',str(p/'harness.c'),'-o',str(p/'harness')],check=True)
   run=subprocess.run([str(p/'harness')],capture_output=True,text=True)
   if run.returncode:raise RuntimeError(group+'\n'+run.stdout+run.stderr)
   out[group]=json.loads(run.stdout)
 specs={'mca_protocol/mca_protocol_class.c':'protocol_class_get_protocol_data','mca_protocol/protocol_pd/mca_protocol_pd_class.c':'protocol_class_pd_get_ops_data','mca_protocol/protocol_qc/mca_protocol_qc_class.c':'protocol_class_qc_get_ops_data','mca_platform/mca_platform_bc12_class.c':'platform_class_bc12_get_ops_data'}
 out['registry_guards']={}
 for path,getter in specs.items():
  s=(base/path).read_text();refs=len(re.findall(r'\b'+getter+r'\(',s))-1;guards=s.count('guard(srcu)(&mca_ops_srcu)');assert refs==guards,(path,refs,guards);out['registry_guards'][path]=guards
 cleanup=function(sources['subpmic'],'qcom_subpmic_cleanup');assert cleanup.index('mca_adsp_glink_unregister_ops')<cleanup.index('platform_class_buckchg_ops_unregister')<cleanup.index('disable_delayed_work_sync')<cleanup.index('kfree(entry)')
 out['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];out['source_sha256']={str(p.relative_to(base)):hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*.c')}
 out['limitations']='Mock block/firmware services and pthread SRCU model; no live kernel, storage, PD negotiation or hardware validation.'
 serialized=json.dumps(out,indent=2)+'\n';print(serialized,end='')
 if args.output:args.output.write_text(serialized)
if __name__=='__main__':main()
