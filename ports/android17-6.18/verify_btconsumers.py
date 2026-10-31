#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def function(s,n):
 s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
 m=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(n)+r'\s*\([^;#]*?\)\n\{',s)
 if not m:raise ValueError(n)
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+'\n'
def stripped(s):return re.sub(r'^#include.*\n','',s,flags=re.M)
def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=a.workspace/a.source_dir;bt=root/'qcom/opensource/bt-kernel';sources={n:(bt/path).read_text() for n,path in {'codec':'btfmcodec/btfm_codec.c','interface':'btfmcodec/btfm_codec_interface.c','endpoint':'btfmcodec/btfm_codec_hw_interface.c','bearer':'btfmcodec/btfm_codec_btadv_interface.c','swr':'soundwire/btfm_swr.c','swrif':'soundwire/btfm_swr_hw_interface.c'}.items()}
 groups={'packets':[('codec',n) for n in ['STREAM_TO_UINT32','btfmcodec_dev_rxwork_impl','btfmcodec_dev_write','btfmcodec_dev_enqueue_pkt','btfmcodec_enqueue_transport','btfmcodec_dequeue_transport','btfmcodec_dev_read']]}
 groups['commands']=[('codec',n) for n in ['btfmcodec_commands_stopped','btfmcodec_wait_response']]+[('interface',n) for n in ['btfmcodec_get_dai_drvdata','btfmcodec_configure_master','btfmcodec_configure_dma','btfmcodec_hwep_shutdown','btfmcodec_notify_usecase_start']]
 groups['component']=[('interface',n) for n in ['btfm_codec_get_mixer_control_impl','btfm_codec_get_mixer_control','btfmcodec_put_mixer_control_impl','btfmcodec_put_mixer_control','btfmcodec_codec_probe','btfmcodec_codec_remove','btfmcodec_is_valid_cache_avb','btfmcodec_check_and_cache_configs']]
 groups['ports']=[('swr',n) for n in ['btfm_get_bt_soc_index','btfm_swr_hw_init_locked','btfm_swr_hw_init','btfm_swr_enable_port_locked','btfm_swr_enable_port','btfm_swr_disable_port_locked','btfm_swr_disable_port']]
 groups['lifetime']=[('codec',n) for n in ['btfmcodec_dev_open','btfmcodec_dev_release','btfm_get_btfmcodec','btfmcodec_stop_commands','btfmcodec_drain_commands','btfmcodec_device_release','btfmcodec_init','btfmcodec_deinit']]
 groups['endpoint']=[('codec',n) for n in ['btfm_get_btfmcodec','btfmcodec_stop_commands','btfmcodec_endpoint_enter','btfmcodec_endpoint_exit']]+[('interface',n) for n in ['btfm_register_codec','btfm_unregister_codec']]+[('endpoint',n) for n in ['btfmcodec_register_hw_ep','btfmcodec_unregister_hw_ep']]
 groups['swr_lifetime']=[('swr',n) for n in ['btfm_swr_free','btfm_swr_open','btfm_swr_release','btfm_swr_ioctl','btfm_swr_probe','btfm_swr_remove']]
 groups['params']=[('swrif',n) for n in ['btfm_put_codec_type','btfm_put_feedback_ch_setting','btfm_swr_dai_hw_params','btfm_get_sampling_rate','btfm_swr_dai_get_channel_map','btfm_swr_dai_get_configs']]
 report={'tests':{}};generated={}
 with tempfile.TemporaryDirectory(prefix='btconsumer-test-') as d:
  tmp=Path(d)
  def execute(name,s,negative=False):
   f=tmp/(name+'.c');f.write_text(s);exe=f.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-I',str(H/'tests'),str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
   if negative:
    assert r.returncode,(name,'mutation not rejected');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for group,entries in groups.items():
   s=(H/'tests'/f'btconsumer_{group}.c').read_text().replace('/* FUNCTIONS */','\n'.join(function(sources[file],n) for file,n in entries))
   s=s.replace('/* HW HEADER */',stripped((bt/'btfmcodec/include/btfm_codec_hw_interface.h').read_text()))
   s=s.replace('/* CODEC HEADER */',stripped((bt/'btfmcodec/include/btfm_codec.h').read_text())).replace('/* PACKET HEADER */',stripped((bt/'btfmcodec/include/btfm_codec_pkt.h').read_text()))
   s=s.replace('/* SWR HEADER */',stripped((bt/'soundwire/btfm_swr.h').read_text())).replace('/* SLAVE HEADER */',stripped((bt/'soundwire/btfm_swr_slave.h').read_text())).replace('/* SLAVE TABLE */',stripped((bt/'soundwire/btfm_swr_slave.c').read_text()))
   s=s.replace('/* SWRIF HEADER */',stripped((bt/'soundwire/btfm_swr_hw_interface.h').read_text()))
   generated[group]=s;report['tests'][group]=execute(group,s)
  if a.negative:
   mutations={'packet_short_header':('packets','if (skb->len < BTM_HEADER_LEN)','if (false)'), 'packet_envelope_length':('packets','if (len != skb->len)','if (false)'), 'partial_read_discards_tail':('packets','if (skb->len)\n\t\tskb_queue_head','if (false)\n\t\tskb_queue_head')}
   mutations.update({
    'enqueue_error_ignored':('commands','if (ret < 0)\n\t\treturn ret;\n\treturn btfmcodec_wait_response','if (false)\n\t\treturn ret;\n\treturn btfmcodec_wait_response'),
    'positive_get_configs_rejected':('commands','if (ret < 0)\n\t\t\treturn ret;','if (ret)\n\t\t\treturn ret;'),
    'timeout_success':('commands','return -ETIMEDOUT;','return 0;'),
    'stop_predicate_removed':('commands','btfmcodec_commands_stopped(chardev), msecs_to_jiffies','false, msecs_to_jiffies'),
    'control_error_masked':('component','return mixer_ctrl[i].put(kcontrol, ucontrol);','mixer_ctrl[i].put(kcontrol, ucontrol); return 1;'),
    'component_probe_error_masked':('component','if (ret)\n\t\t\treturn ret;','if (false)\n\t\t\treturn ret;'),
    'cache_replacement_leak':('component','kfree(tmp);','(void)tmp;'),
    'port_bound_removed':('ports','port_num >= ARRAY_SIZE(pbtfmswr->ports)','false'),
    'rollback_count_lost':('ports','btfm_num_ports_open--;','(void)0;'),
    'retry_rate_ignored':('ports','pbtfmswr->ports[port_num].sample_rate != sample_rate','false'),
    'reset_write_error_ignored':('ports','if (ret)\n\t\t\treturn ret;','if (false)\n\t\t\treturn ret;'),
    'premature_cdev_open':('lifetime','if (!btfmcodec_dev->ready)','if (false)'),
    'file_device_reference_lost':('lifetime','get_device(&btfmcodec->dev);','(void)0;'),
    'endpoint_device_reference_lost':('endpoint','get_device(copy->dev);','(void)0;'),
    'endpoint_drain_bypassed':('endpoint','synchronize_srcu(&btfmcodec_endpoint_srcu);','(void)0;'),
    'stale_file_reaches_reprobe':('swr_lifetime','if (priv != pbtfmswr || priv->stopping)','if (!pbtfmswr)'),
    'swr_open_reference_lost':('swr_lifetime','kref_get(&pbtfmswr->ref);','(void)0;'),
    'qlea_rate_regression':('params','*sampling_rate = 192000;','*sampling_rate = 96000;'),
    'dma_second_interface_regression':('params','hwep_config->inf_index = 2;','hwep_config->inf_index = 1;'),
   })
   report['negative_controls']={}

   for name,(group,before,after) in mutations.items():
    s=generated[group];assert before in s;report['negative_controls'][name]=execute(name,s.replace(before,after,1),True)
 pins=json.loads((H/('btconsumers-imports.json' if (H/'btconsumers-imports.json').exists() else 'imports.json')).read_text());report['source_sha256']={pin['path']:hashlib.sha256((root/pin['path']).read_bytes()).hexdigest() for pin in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('btconsumer_*'))};report['limits']='Extracted production functions; kernel services mocked. Firmware, audio hardware, IRQ/workqueue scheduling and actual ALSA/SoundWire concurrency require device validation.';a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
