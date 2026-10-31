#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def function(s,n):
 s=s.replace('static void icnss_remove(struct platform_device *pdev)\n#endif\n{','static void icnss_remove(struct platform_device *pdev)\n{')
 s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
 m=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(n)+r'\([^;#]*?\)\n\{',s)
 if not m:raise ValueError(n)
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+'\n'

def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.workspace/a.source_dir
 sources={n:(root/'platform/icnss2'/f'{n}.c').read_text() for n in ['main','power','qmi']};header=(root/'platform/icnss2/main.h').read_text()
 groups={'module':[('main',n) for n in ['icnss_initialize','icnss_exit']], 'qmi':[('qmi',n) for n in ['icnss_register_fw_service','icnss_unregister_fw_service','icnss_dms_init','icnss_dms_deinit','icnss_register_ims_service','icnss_unregister_ims_service','icnss_clear_server']], 'protocol':[('power',n) for n in ['icnss_aop_set_vreg_param','icnss_aop_pdc_reconfig']]+[('main',n) for n in ['icnss_record_smmu_fault_timestamp','icnss_pci_get_suspended','icnss_ring_doorbell']]+[('qmi',n) for n in ['icnss_set_feature_list','icnss_get_feature_list']], 'event':[('main',n) for n in ['icnss_free_event_data','icnss_driver_event_post','icnss_driver_event_work']], 'diag':[('qmi',n) for n in ['wlfw_athdiag_read_send_sync_msg','wlfw_athdiag_write_send_sync_msg']]}
 groups['icc']=[('main',n) for n in ['icnss_register_bus_scale','icnss_unregister_bus_scale']]
 groups['publication']=[('main','icnss_publish_fault_handler')]
 groups['teardown']=[('main',n) for n in ['icnss_unregister_power_supply_notifier','icnss_remove']]
 assert 'iommu_set_fault_handler(' not in function(sources['main'],'icnss_smmu_dt_parse')
 probe=function(sources['main'],'icnss_probe');assert probe.index('icnss_register_fw_service(priv)') < probe.index('icnss_publish_fault_handler(priv)')
 report={'tests':{}};generated={}
 with tempfile.TemporaryDirectory(prefix='icnss2-tests-') as directory:
  tmp=Path(directory)
  def execute(name,source,negative=False):
   c=tmp/(name+'.c');c.write_text(source);binary=c.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-I',str(H/'tests'),str(c),'-o',str(binary)],check=True)
   r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
   if negative:
    assert r.returncode,(name,'mutation not rejected');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for group,entries in groups.items():
   source=(H/'tests'/f'icnss2_{group}.c').read_text().replace('/* FUNCTIONS */','\n'.join(function(sources[f],n) for f,n in entries))
   if group=='event':source=source.replace('/* EVENT TYPES */','\n'.join(re.search(kind+r' '+n+r' \{.*?\n\};',header,re.S).group() for kind,n in [('enum','icnss_driver_event_type'),('enum','icnss_soc_wake_event_type'),('struct','icnss_driver_event')]))
   generated[group]=source;report['tests'][group]=execute(group,source)
  if a.negative:
   mutations={
    'module_main_error_masked':('module','ret = platform_driver_register(&icnss_driver);','ret = platform_driver_register(&icnss_driver);\n if (ret) return 0;'),
    'qmi_lookup_leak':('qmi','priv->qmi_registered = false;\n\t\tqmi_handle_release(&priv->qmi);','priv->qmi_registered = false;'),
    'dms_lookup_leak':('qmi','priv->dms_registered = false;\n\t\tqmi_handle_release(&priv->qmi_dms);','priv->dms_registered = false;'),
    'ims_failed_init_release':('qmi','if (priv->ims_registered) {','if (true) {'),
    'event_negative_type':('event','if ((unsigned int)type >= ICNSS_DRIVER_EVENT_MAX)', 'if (false)'),
    'event_queued_after_stop':('event','if (READ_ONCE(priv->stopping)) {','if (false) {'),
    'event_atomic_wait':('event','if (gfp == GFP_ATOMIC && (flags & ICNSS_EVENT_SYNC))','if (false)'),
    'diag_oversized_response_copy':('diag','memcpy(data, resp->data, data_len);','memcpy(data, resp->data, resp->data_len);'),
    'doorbell_null_mapping':('protocol','if (!priv->mem_base_va)','if (false)'),
    'aop_negative_parameter':('protocol','(unsigned int)param >= ARRAY_SIZE(vreg_param_str)','false'),
    'teardown_early_handles':('teardown','flush_workqueue(priv->event_wq);','icnss_unregister_fw_service(priv);\n flush_workqueue(priv->event_wq);'),
    'fault_callback_module_not_retained':('publication','__module_get(THIS_MODULE);','(void)0;'),
    'icc_error_path_reference_leak':('icc','icc_put(bus_bw_info->icc_path);','(void)0;'),
    'qmi_shutdown_reconnect_assertion':('qmi','if (ret == -ESHUTDOWN)','if (false)'),
    'feature_narrow_shift':('protocol','BIT_ULL(feature)','(1U << feature)'),
   }
   report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    source=generated[group]
    # registration and cleanup both contain balanced-release sequences; mutate registration only.
    assert before in source,name
    report['negative_controls'][name]=execute(name,source.replace(before,after,1),True)
 report['source_sha256']={p['path']:hashlib.sha256((root/p['path']).read_bytes()).hexdigest() for p in json.loads((H/('icnss2-imports.json' if (H/'icnss2-imports.json').exists() else 'imports.json')).read_text())}
 report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('icnss2_*')) if p.is_file()}
 report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['limits']='Extracted production functions with mocked Linux services. IRQ/workqueue concurrency, hardware DMA, firmware boot and physical removal are not established.'
 a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
