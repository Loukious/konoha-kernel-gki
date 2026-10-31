#!/usr/bin/env python3
"""Validate extracted Type-C transport/switch helpers with mocked providers."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
PREFIX='drivers/soc/qcom/'
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{',source)
 if not m:raise ValueError(name)
 end,depth=m.end(),1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]+'\n'
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args();w=a.workspace.resolve();root=w/a.source_dir
 am=(root/PREFIX/'altmode-glink.c').read_text();fsa=(root/PREFIX/'fsa4480-i2c.c').read_text()
 groups={
 'altmode_transport':(am,['altmode_client_release','altmode_write','__altmode_send_data','altmode_send_data','client_cb_work','altmode_register_client','altmode_deregister_client','altmode_send_ack','altmode_state_cb','altmode_callback']),
 'altmode_switch':(fsa,['fsa4480_usbc_update_settings','fsa4480_usbc_event_changed','fsa4480_usbc_analog_setup_switches','fsa4480_validate_display_port_settings','fsa4480_switch_event','fsa4480_usbc_analog_work_fn','fsa4480_update_reg_defaults','fsa4480_probe','fsa4480_remove']),
 }
 results={}
 with tempfile.TemporaryDirectory(prefix='onyx-altmode-') as tmp:
  for name,(src,names) in groups.items():
   code='\n'.join(function(src,n) for n in names);template=(HERE/'tests'/(name+'.c')).read_text()
   if name=='altmode_switch':
    tables='\n'.join(re.findall(r'(?m)^static const struct fsa4480_reg_val \w+\[\] = \{.*?^\};',fsa,re.S))
    defines='\n'.join(x for x in fsa.splitlines() if x.startswith('#define ') and not x.startswith('#define FSA4480_I2C_NAME'))
    template=template.replace('/* TABLES */',defines+'\n'+tables)
   p=Path(tmp)/(name+'.c');p.write_text(template.replace('/* FUNCTIONS */',code));exe=Path(tmp)/name
   subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Werror=implicit-function-declaration','-pthread',str(p),'-o',str(exe)],check=True)
   run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45)
   if run.returncode:raise RuntimeError(name+': '+run.stderr)
   results[name]=json.loads(run.stdout)
 imports=json.loads((HERE/'altmode-imports.json').read_text())
 originals={i['path']:subprocess.check_output(['git','-C',str(w/i['project']),'cat-file','blob',i['blob']]) for i in imports}
 protected=[]
 for item in imports:
  if item['path'].endswith('.h'):
   data=subprocess.check_output(['git','-C',str(w/item['project']),'cat-file','blob',item['blob']]);assert (root/item['path']).read_bytes()==data;protected.append(item['path'])
 original=originals[PREFIX+'fsa4480-i2c.c'].decode()
 for name in ['fsa_reg_i2c_defaults','dio_reg_i2c_defaults']:
  regex=r'(?m)^static const struct fsa4480_reg_val '+name+r'\[\] = \{.*?^\};';assert re.search(regex,fsa,re.S)[0]==re.search(regex,original,re.S)[0]
 assert '.max_register = DIO4485_POWER_UP_51' in fsa
 assert 'cancel_work_sync' not in function(am,'altmode_callback')
 dereg=function(am,'altmode_deregister_client');assert dereg.index('idr_remove')<dereg.index('cancel_work_sync')<dereg.index('kref_put')
 remove=function(am,'altmode_remove');assert remove.index('removing, true')<remove.index('pmic_glink_unregister_client')<remove.index('cancel_delayed_work_sync')<remove.index('ipc_log_context_destroy')
 assert 'kref_get' in remove and 'kref_put' in remove
 remove=function(fsa,'fsa4480_remove');assert remove.index('list_del_init')<remove.index('unregister_ucsi_glink_notifier')<remove.index('cancel_work_sync')<remove.index('synchronize_srcu')<remove.index('device_init_wakeup')
 assert 'of_find_i2c_device_by_node' not in fsa and 'of_find_device_by_node' not in am
 for text in [am,fsa]:assert 'debugfs_create_file_unsafe' not in text
 # Firmware structures retain their pinned size and field representation.
 orig=originals[PREFIX+'altmode-glink.c'].decode()
 for name in ['usbc_notify_ind_msg','usbc_write_buffer_req_msg']:
  regex=r'(?m)^struct '+name+r' \{.*?^\};';assert re.search(regex,am,re.S)[0]==re.search(regex,orig,re.S)[0]
 if (root/'platform_defconfig').exists():
  config=(root/'platform_defconfig').read_text();assert 'CONFIG_QTI_ALTMODE_GLINK=m' in config and 'CONFIG_QCOM_FSA4480_I2C=m' in config
 results['protected_pinned_headers']=protected;results['firmware_structs_and_switch_tables_unchanged']=True
 results['source_sha256']={i['path']:hashlib.sha256((root/i['path']).read_bytes()).hexdigest() for i in imports}
 results['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'tests').glob('altmode_*.c')}
 results['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
 results['limits']='Extracted C functions with mocked GLINK, device/reference, notifier/SRCU, regmap and workqueue services. No live kernel locking, actual firmware message capture, Type-C/DP electrical validation, hardware switch or phone test.'
 text=json.dumps(results,indent=2)+'\n'
 if a.output:a.output.write_text(text)
 print(text,end='')
if __name__=='__main__':main()
