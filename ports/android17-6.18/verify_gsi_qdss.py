#!/usr/bin/env python3
import argparse, hashlib, json, re, subprocess, tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{',source)
 if not m: raise ValueError(name)
 end=m.end();depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]+'\n'

def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args();root=a.workspace/a.source_dir
 pins=json.loads((H/'gsi-qdss-imports.json').read_text()); originals={p['path']:subprocess.check_output(['git','-C',str(a.workspace/p['project']),'cat-file','blob',p['blob']]).decode() for p in pins}; files={Path(p['path']).name:(root/p['path']).read_text() for p in pins}
 groups={'gsi_queue': [('f_gsi.c',n) for n in ['post_event','read_event','peek_event','reset_event_queue','gsi_rndis_message_valid']], 'ipa_registry':[('f_gsi.c',n) for n in ['gsi_ipa_ops_valid','gsi_ipa_ops_in_module','gsi_ipa_module_notify','gsi_ipa_registry_clear','gsi_ipa_get','gsi_ipa_put','ipa_ready_callback']]}
 groups['gsi_lifetime']=[('f_gsi.c',n) for n in ['gsi_ctrl_pkt_alloc','gsi_ctrl_pkt_free','gsi_ctrl_clear_cpkt_queues','gsi_release','gsi_put','gsi_device_release','gsi_function_ctrl_port_init','gsi_function_init','gsi_ctrl_dev_open','gsi_ctrl_dev_release','gsi_ctrl_dev_read','gsi_inst_clean','gsi_free_inst']]
 groups['qdss_lifetime']=[('f_qdss.c',n) for n in ['qdss_release','qdss_notify','qdss_pm_put','qdss_write_complete','qdss_free_reqs','usb_qdss_alloc_req','alloc_usb_qdss','usb_qdss_write','usb_qdss_open','usb_qdss_close','usb_qdss_free_inst','qdss_eps_disable','qdss_set_alt']]
 groups['gsi_channels']=[('f_gsi.c',n) for n in ['ipa_connect_channels','ipa_data_path_enable','ipa_data_path_disable','ipa_disconnect_channel']]
 groups['ipa_tokens']=[('f_gsi.c',n) for n in ['gsi_ipa_token_activate','gsi_ipa_token_retire','ipa_usb_notify_cb']]
 groups['qdss_header_enabled']=[]
 groups['qdss_header_disabled']=[]
 result={}
 with tempfile.TemporaryDirectory(prefix='onyx-gsi-qdss-tests-') as tmp:
  for group,funcs in groups.items():
   template='qdss_header' if group.startswith('qdss_header_') else group
   src=(H/'tests'/(template+'.c')).read_text();src=src.replace('/* QDSS_PUBLIC */',re.sub(r'^#include.*$','',files['usb_qdss.h'],flags=re.M));src=src.replace('/* FUNCTIONS */','\n'.join(function(files[f],n) for f,n in funcs));types='\n'.join(re.search(r'(?m)^typedef struct '+n+r' \{.*?^\} '+n+';',files['rndis.h'],re.S).group() for n in ['rndis_init_msg_type','rndis_query_msg_type','rndis_set_msg_type','rndis_halt_msg_type','rndis_reset_msg_type','rndis_keepalive_msg_type']);src=src.replace('/* RNDIS_TYPES */',types)
   ipa=files['ipa_usb.h'];ipa=re.sub(r'^#include.*$','',ipa,flags=re.M);ipa=ipa[:ipa.index('#if IS_ENABLED')];src=src.replace('/* IPA_TYPES */',ipa+'\n#endif\n')
   qtypes='\n'.join(re.search(r'(?m)^struct '+n+r' \{.*?^\};',files['f_qdss.h'],re.S).group() for n in ['usb_qdss_ch','gqdss','f_qdss','usb_qdss_opts','qdss_req']);qtypes=qtypes.replace('enum qti_port_type qport','int qport');src=src.replace('/* QDSS_TYPES */',qtypes)
   dwc=(a.workspace/'onyx/include/linux/usb/dwc3-msm.h').read_text();dtypes='\n'.join(re.search(r'(?m)^struct '+n+r' \{.*?^\};',dwc,re.S).group() for n in ['usb_gsi_request','gsi_channel_info']);src=src.replace('/* DWC_TYPES */',dtypes)
   p=Path(tmp)/(group+'.c');p.write_text(src);exe=p.with_suffix('');subprocess.run(['cc','-std=gnu11','-DQDSS_ENABLED='+str(int(group.endswith('_enabled'))),'-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I',str(H/'tests'),str(p),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45)
   if run.returncode:raise RuntimeError(group+': '+run.stderr)
   result[group]=json.loads(run.stdout)
 # Public binary structures and USB descriptor literals remain native.
 unchanged=[]
 for entry in pins:
  rel=entry['path'];before=originals[rel];after=(root/rel).read_text()
  if rel.endswith('.h') and rel not in ['drivers/usb/gadget/function/f_gsi.h','drivers/usb/gadget/function/f_qdss.h','include/linux/usb/usb_qdss.h']:
   assert before==after,rel;unchanged.append(rel)
 for name in ['ipa_usb.h','usb_qdss.h','usb_ctrl_qti.h']:
  rel=next(e['path'] for e in pins if Path(e['path']).name==name)
  pat=r'(?m)^(?:struct|enum) \w+ \{.*?^\};'
  assert re.findall(pat,originals[rel],re.S)==re.findall(pat,(root/rel).read_text(),re.S),name
 descriptors=[]
 for name in ['f_gsi.h','f_qdss.c']:
  rel=next(e['path'] for e in pins if Path(e['path']).name==name)
  pat=r'(?m)^static struct usb_\w+ (\w+) = \{.*?^\};'
  before={m[1]:re.sub(r'\s+','',m[0]) for m in re.finditer(pat,originals[rel],re.S)}
  after={m[1]:re.sub(r'\s+','',m[0]) for m in re.finditer(pat,(root/rel).read_text(),re.S)}
  assert before==after,name;descriptors.extend(before)
 gsi=files['f_gsi.c'];bind=function(gsi,'gsi_bind');unbind=function(gsi,'gsi_unbind')
 assert 'gsi->ipa_token);' in bind and 'gsi_ipa_token_activate(gsi)' in bind
 assert unbind.index('gsi_ipa_token_retire')<unbind.index('deinit_teth_prot')<unbind.index('gsi_ipa_put')
 assert 'USB_COMP_EP0_BUFSIZ' in function(gsi,'gsi_setup')
 qdss=files['f_qdss.c'];unbind_q=function(qdss,'qdss_unbind')
 assert unbind_q.index('qdss_eps_disable')<unbind_q.index('qdss_free_reqs')<unbind_q.index('clear_eps')
 result['unchanged_usb_descriptors']=descriptors
 result['public_data_layouts_unchanged']=True
 result['unchanged_pinned_headers']=unchanged
 result['source_contracts']={'bind_uses_distinct_ipa_token':True,'token_retired_before_provider_deinit':True,'qdss_requests_freed_after_endpoint_drain':True,'ep0_transfer_bounded':True}
 result['source_sha256']={p['path']:hashlib.sha256((root/p['path']).read_bytes()).hexdigest() for p in pins};result['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').iterdir()) if p.name in ['qdss_header.c', 'ipa_tokens.c', 'gsi_channels.c', 'qdss_lifetime.c', 'gsi_lifetime.c', 'ipa_registry.c', 'gsi_queue.c', 'gsi_mock.h']}
 result['limits']='Extracted actual driver functions with mocked Linux services; does not prove live module-loader, USB/IRQ/PM ordering, IPA firmware, phone boot or hardware operation.'
 text=json.dumps(result,indent=2)+'\n';print(text,end='')
 if a.output:a.output.write_text(text)
if __name__=='__main__':main()
