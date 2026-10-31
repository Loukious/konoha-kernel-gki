#!/usr/bin/env python3
"""Verify native CNSS2 lifetime, error and transport functions with sanitizer mocks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
PREFIX='platform/cnss2/'

def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 match=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{',source)
 if not match:raise ValueError(name)
 end,depth=match.end(),1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[match.start():end]+'\n'

def main():
 parser=argparse.ArgumentParser()
 parser.add_argument('workspace',type=Path)
 parser.add_argument('--source-dir',default='onyx')
 parser.add_argument('--negative',action='store_true')
 parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args()
 root=args.workspace/args.source_dir
 sources={name:(root/PREFIX/(name+'.c')).read_text() for name in ['main','qmi','genl','pci']}
 header=(root/PREFIX/'main.h').read_text()
 groups={
  'module': [('main',name) for name in ['cnss_initialize','cnss_exit']],
  'qmi': [('qmi',name) for name in ['cnss_qmi_init','cnss_qmi_deinit','cnss_dms_init','cnss_dms_deinit','cnss_dms_server_exit_work','dms_del_server','cnss_cancel_dms_work','cnss_register_coex_service','cnss_unregister_coex_service','cnss_register_ims_service','cnss_unregister_ims_service']],
  'smem': [('main','cnss_wlan_hw_disable_check')],
  'misc': [('main',name) for name in ['cnss_misc_init','cnss_misc_stop','cnss_misc_deinit']],
  'thermal': [('main',name) for name in ['cnss_tcdev_get_max_state','cnss_tcdev_get_cur_state','cnss_tcdev_set_cur_state','cnss_thermal_cdev_register','cnss_thermal_cdev_unregister','cnss_get_curr_therm_cdev_state','cnss_thermal_cdev_unregister_all']],
  'event': [('main',name) for name in ['cnss_event_free_data','cnss_driver_event_post','cnss_driver_event_work']],
  'genl': [('genl','cnss_genl_send_msg')],
  'pci': [('pci',name) for name in ['cnss_pci_unregister_mhi','cnss_pci_remove']],
 }
 report={'tests':{}};generated={}
 with tempfile.TemporaryDirectory(prefix='onyx-cnss2-test-') as directory:
  temporary=Path(directory)
  def execute(name,source,negative=False):
   c=temporary/(name+'.c');c.write_text(source);exe=c.with_suffix('')
   subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-I',str(HERE/'tests'),str(c),'-o',str(exe)],check=True)
   result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45)
   if negative:
    assert result.returncode,(name,'mutation was not rejected')
    diagnostic=next((line for line in result.stderr.splitlines() if 'Assertion' in line or 'runtime error:' in line or 'ERROR: AddressSanitizer' in line),'')
    assert diagnostic,(name,result.stderr)
    return {'rejected':True,'returncode':result.returncode,'diagnostic':diagnostic}
   assert not result.returncode,(name,result.stderr)
   return json.loads(result.stdout)
  for group,entries in groups.items():
   source=(HERE/'tests'/('cnss2_'+group+'.c')).read_text()
   source=source.replace('/* FUNCTIONS */','\n'.join(function(sources[file],name) for file,name in entries))
   if group=='thermal':
    definition=re.search(r'struct cnss_thermal_cdev \{.*?\n\};',header,re.S).group()
    source=source.replace('/* THERMAL TYPE */',definition)
   if group=='event':
    definition=re.search(r'enum cnss_driver_event_type \{.*?\n\};',header,re.S).group()+'\n'+re.search(r'struct cnss_driver_event \{.*?\n\};',sources['main'],re.S).group()
    source=source.replace('/* EVENT TYPES */',definition)
   if group=='genl':
    definitions='\n'.join(re.search(r'(?m)^#define '+name+r' .+$',sources['genl']).group() for name in ['CNSS_GENL_DATA_LEN_MAX','CNSS_GENL_SEND_RETRY_COUNT','CNSS_GENL_SEND_RETRY_DELAY'])
    source=source.replace('/* GENL CONSTANTS */',definitions)
   generated[group]=source
   report['tests'][group]=execute(group,source)
  if args.negative:
   native_unregister=function(sources['main'],'cnss_thermal_cdev_unregister')
   mutated_unregister=native_unregister.replace('if (entry->tcdev_id == tcdev_id)', 'if (true)')
   mutations={
    'thermal_wrong_id':('thermal',native_unregister,mutated_unregister),
    'thermal_unregister_lock':('thermal','thermal_cooling_device_unregister(removed->tcdev);','mutex_lock(&priv->tcdev_lock);\n\t\tthermal_cooling_device_unregister(removed->tcdev);\n\t\tmutex_unlock(&priv->tcdev_lock);'),
    'smem_short_descriptor':('smem','if (size < sizeof(*peripheral_state_info))','if (false)'),
    'qmi_lookup_leak':('qmi','plat_priv->qmi_initialized = false;\n\t\tqmi_handle_release(&plat_priv->qmi_wlfw);','plat_priv->qmi_initialized = false;'),
    'dms_lookup_leak':('qmi','if (ret) {\n\t\tplat_priv->dms_initialized = false;\n\t\tqmi_handle_release(&plat_priv->qmi_dms);','if (ret) {\n\t\tplat_priv->dms_initialized = false;'),
    'ims_failed_init_release':('qmi','if (plat_priv->ims_initialized) {','if (true) {'),
    'misc_callback_not_withdrawn':('misc','cnss_utils_unregister_status_notifier(CNSS_UTILS_FMD_STATUS,\n\t\t\t\t\t      cnss_fmd_status_update_cb, plat_priv);','(void)0;'),
    'module_failure_masked':('module','ret = platform_driver_register(&cnss_platform_driver);','ret = platform_driver_register(&cnss_platform_driver);\n\tif (ret) return 0;'),
    'event_stop_ignored':('event','if (plat_priv->stopping) {','if (false) {'),
    'queued_event_stop_ignored':('event','if (READ_ONCE(plat_priv->stopping)) {','if (false) {'),
    'pci_early_dma_release':('pci','cnss_pci_unregister_mhi(pci_priv);\n\tcnss_pci_free_aux_mem(pci_priv);','cnss_pci_free_aux_mem(pci_priv);\n\tcnss_pci_unregister_mhi(pci_priv);'),
    'genl_null_payload':('genl','if (total_size && !buff)','if (false)'),
   }
   report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    source=generated[group];assert source.count(before)==1,(name,source.count(before))
    report['negative_controls'][name]=execute(name,source.replace(before,after),True)
 fixture=HERE/('cnss2-imports.json' if (HERE/'cnss2-imports.json').exists() else 'imports.json')
 pins=json.loads(fixture.read_text())
 report['source_sha256']={pin['path']:hashlib.sha256((root/pin['path']).read_bytes()).hexdigest() for pin in pins}
 report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((HERE/'tests').glob('cnss2_*')) if p.is_file()}
 report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
 report['limits']='Extracted production functions with mocked Linux services. Event handlers, firmware/PCIe/MHI, thermal core, callback providers and IRQ/workqueue scheduling are simulated; no device or hardware validation.'
 args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
