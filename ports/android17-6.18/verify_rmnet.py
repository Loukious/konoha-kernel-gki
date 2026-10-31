#!/usr/bin/env python3
"""Run native RMNET ownership and protocol functions with sanitizer mocks."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
PREFIX='qcom/opensource/datarmnet/core/'
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 match=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{',source)
 if not match:raise ValueError(name)
 end,depth=match.end(),1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[match.start():end]+'\n'
def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=a.workspace/a.source_dir
 source={name:(root/PREFIX/(name+'.c')).read_text() for name in ['rmnet_config','rmnet_ctl_ipa','rmnet_module','rmnet_ctl_client','rmnet_genl','rmnet_map_data','rmnet_ll_ipa']}
 source['rmnet_map']=(root/PREFIX/'rmnet_map.h').read_text()
 groups={
 'module':[('rmnet_genl','rmnet_core_genl_init'),('rmnet_config','rmnet_init'),('rmnet_ctl_ipa','rmnet_ctl_ipa_register'),('rmnet_ctl_ipa','rmnet_ctl_ipa_ready'),('rmnet_ctl_ipa','rmnet_ctl_init')],
 'hooks':[('rmnet_module',n) for n in ['rmnet_module_hook_register','rmnet_module_hook_is_set','rmnet_module_hook_unregister_no_sync']],
 'ctl':[('rmnet_ctl_client',n) for n in ['rmnet_ctl_set_dbgfs','rmnet_ctl_endpoint_setdev','rmnet_ctl_endpoint_post','rmnet_ctl_register_client','rmnet_ctl_unregister_client','rmnet_ctl_send_client','rmnet_ctl_log','rmnet_ctl_get_stats']],
 'genl':[('rmnet_genl','rmnet_core_genl_send_resp')],
 'deaggregate':[('rmnet_map','rmnet_map_data_ptr'),('rmnet_map_data','rmnet_map_deaggregate')],
 'll':[('rmnet_ll_ipa',n) for n in ['rmnet_ll_ipa_rx','rmnet_ll_ipa_probe','rmnet_ll_ipa_remove','rmnet_ll_ipa_tx']],
 }
 report={'tests':{}};generated={}
 with tempfile.TemporaryDirectory(prefix='onyx-rmnet-test-') as d:
  tmp=Path(d)
  def run(name,text,negative=False):
   c=tmp/(name+'.c');c.write_text(text);exe=c.with_suffix('')
   subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-I',str(HERE/'tests'),str(c),'-o',str(exe)],check=True)
   r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45)
   if negative:
    assert r.returncode,(name,'Mutation accepted')
    diagnostic=next((x for x in r.stderr.splitlines() if 'Assertion' in x or 'runtime error:' in x or 'ERROR: AddressSanitizer' in x),'')
    assert diagnostic,(name,r.stderr)
    return {'rejected':True,'returncode':r.returncode,'diagnostic':diagnostic}
   assert not r.returncode,(name,r.stderr)
   return json.loads(r.stdout)
  for group,entries in groups.items():
   code=(HERE/'tests'/('rmnet_'+group+'.c')).read_text().replace('/* FUNCTIONS */','\n'.join(function(source[f],n) for f,n in entries))
   if group=='ctl':
    texts='\n'.join((root/PREFIX/(n+'.h')).read_text() for n in ['rmnet_ctl','rmnet_ctl_client'])+'\n'+source['rmnet_ctl_client']
    types=['enum rmnet_ctl_log_lvl','struct rmnet_ctl_client_hooks','struct rmnet_ctl_stats','struct rmnet_ctl_dev','struct rmnet_ctl_client','struct rmnet_ctl_endpoint']
    code=code.replace('/* CTL TYPES */','\n'.join(re.search(re.escape(t)+r' \{.*?\n\};',texts,re.S).group() for t in types))
   if group=='deaggregate':
    types=['rmnet_map_header','rmnet_map_v5_csum_header','rmnet_map_v5_nl_pair','rmnet_map_v5_coal_header','rmnet_map_dl_csum_trailer']
    code=code.replace('/* MAP TYPES */','\n'.join(re.search('struct '+t+r' \{.*?\n\}[^;\n]*;',source['rmnet_map'],re.S).group() for t in types))
   if group=='ll':
    header=(root/PREFIX/'rmnet_ll.h').read_text();code=code.replace('/* LL STATS */',re.search(r'struct rmnet_ll_stats \{.*?\n\};',header,re.S).group())
   generated[group]=code;report['tests'][group]=run(group,code)
  if a.negative:
   hook=function(source['rmnet_module'],'rmnet_module_hook_register')
   mutations={
    'hook_negative_index':('hooks',hook,hook.replace('hook >= 0 && ','')),
    'hook_stale_unregister':('hooks','if (rcu_access_pointer(hook_info->func) == info[i].func)','if (true)'),
    'module_init_error_masked':('module','if (rc)\n\t\treturn rc;','if (rc)\n\t\treturn 0;'),
    'ctl_raw_skb_free':('ctl','} else {\n\t\t\tkfree_skb(skb);','} else {\n\t\t\tkfree(skb);'),
    'qmap_short_header':('deaggregate','if (skb->len < sizeof(*maph))','if (skb->len == 0)'),
    'll_missing_rcu_drain':('ll','synchronize_rcu();','(void)0;'),
    'genl_put_leak':('genl','if (!msg_head) {\n\t\trc = -ENOMEM;\n\t\tgoto free_skb;','if (!msg_head) {\n\t\trc = -ENOMEM;\n\t\tgoto out;'),
   }
   report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    code=generated[group];assert code.count(before)==1,(name,code.count(before));report['negative_controls'][name]=run(name,code.replace(before,after),True)
 pins=json.loads((HERE/'rmnet-imports.json').read_text());report['source_sha256']={pin['path']:hashlib.sha256((root/pin['path']).read_bytes()).hexdigest() for pin in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((HERE/'tests').glob('rmnet_*'))};report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['limits']='Extracted production functions with mocked kernel services; no hardware, firmware, actual RCU scheduling, DMA or end-to-end network validation.'
 a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
