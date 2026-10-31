#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def function(s,n):
 s=re.sub(r'/\*.*?\*/','',s,flags=re.S);m=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(n)+r'\s*\([^;#]*?\)\n\{',s)
 if not m:raise ValueError(n)
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+'\n'
def stripped(s):return re.sub(r'^#include.*\n','',s,flags=re.M)
def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=a.workspace/a.source_dir;B=root/'drivers/slimbus';sources={n:(B/(n+'.c')).read_text() for n in ['messaging','core','stream','qcom-ngd-ctrl']};BT=root/'qcom/opensource/bt-kernel/slimbus';sources['bt']=(BT/'btfm_slim.c').read_text();sources['hwep']=(BT/'btfm_slim_hw_interface.c').read_text();groups={'transactions':[('messaging',n) for n in ['slim_put_async_txn','slim_msg_response','slim_alloc_txn_tid','slim_free_txn_tid','slim_do_transfer','slim_val_inf_sanity','slim_slicesize','slim_xfer_msg','slim_fill_msg','slim_read','slim_readb','slim_write','slim_writeb']]};groups['addresses']=[('core',n) for n in ['slim_report_absent','slim_device_alloc_laddr','slim_device_report_present','slim_get_logical_addr']];groups['streams']=[('stream',n) for n in ['slim_stream_allocate','slim_connect_port_channel','slim_disconnect_port','slim_deactivate_remove_channel','slim_get_prate_code','slim_stream_prepare_locked','slim_stream_prepare','slim_define_channel_content','slim_get_segdist_code','slim_define_channel','slim_activate_channel','slim_stream_enable_locked','slim_stream_enable','slim_stream_disable_locked','slim_stream_disable','slim_stream_unprepare_locked','slim_stream_unprepare','slim_stream_unprepare_disconnect_port_locked','slim_stream_unprepare_disconnect_port','slim_stream_free_locked','slim_stream_free']];groups['ngd_transfer']=[('qcom-ngd-ctrl',n) for n in ['qcom_slim_ngd_tx_msg_get','qcom_slim_ngd_tx_msg_dma_cb','qcom_slim_ngd_tx_msg_post','qcom_slim_ngd_tx_msg_cancel','qcom_slim_ngd_rx','qcom_slim_ngd_xfer_msg','qcom_slim_calc_coef']];groups['bluetooth']=[('bt',n) for n in ['btfm_slim_cleanup_ch','btfm_slim_check_channels','btfm_slim_enable_ch','btfm_slim_disable_ch']]+[('hwep',n) for n in ['btfm_slim_find_channel','btfm_slim_dai_hw_params','btfm_get_sampling_rate','btfm_slim_dai_prepare','btfm_slim_dai_set_channel_map','btfm_slim_dai_get_channel_map','btfm_slim_dai_get_configs']];groups['ngd_lifecycle']=[('qcom-ngd-ctrl',n) for n in ['qcom_slim_ngd_probe','qcom_slim_ngd_remove']];groups['ngd_dma']=[('qcom-ngd-ctrl',n) for n in ['qcom_slim_ngd_post_rx_msgq','qcom_slim_ngd_init_rx_msgq','qcom_slim_ngd_init_tx_msgq','qcom_slim_ngd_init_dma','qcom_slim_ngd_exit_dma']];groups['core_lifetime']=[('core',n) for n in ['slim_dev_release','slim_device_update_status','slim_device_remove','slim_get_device','slim_unregister_controller']];groups['bt_lifetime']=[('bt',n) for n in ['btfm_slim_free','btfm_slim_open','btfm_slim_release','btfm_slim_ioctl','btfm_slim_probe','btfm_slim_remove']];groups['transaction_race']=[('messaging',n) for n in ['slim_put_async_txn','slim_msg_response','slim_alloc_txn_tid','slim_free_txn_tid']];report={'tests':{}};generated={}
 with tempfile.TemporaryDirectory(prefix='slim-test-') as d:
  tmp=Path(d)
  def execute(name,s,negative=False):
   f=tmp/(name+'.c');f.write_text(s);exe=f.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-missing-field-initializers','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie','-I',str(H/'tests'),str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
   if negative:
    assert r.returncode,(name,'mutation not rejected');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for group,entries in groups.items():
   s=(H/'tests'/f'slim_{group}.c').read_text().replace('/* FUNCTIONS */','\n'.join(function(sources[file],n) for file,n in entries));s=s.replace('/* PUBLIC HEADER */',stripped((root/'include/linux/slimbus.h').read_text())).replace('/* PRIVATE HEADER */',stripped((B/'slimbus.h').read_text()));s=s.replace('/* SEG TABLE */',sources['stream'][sources['stream'].index('static const struct segdist_code'):sources['stream'].index('};')+2]);s=s.replace('/* NGD CONSTANTS */',sources['qcom-ngd-ctrl'][sources['qcom-ngd-ctrl'].index('/* NGD ('):sources['qcom-ngd-ctrl'].index('#define CREATE_TRACE_POINTS')]);s=s.replace('/* BT HEADER */',re.sub(r'^#define BTFMSLIM_(?:DBG|INFO|ERR).*\n','',stripped((BT/'btfm_slim.h').read_text()),flags=re.M)).replace('/* BT INTERFACE HEADER */',stripped((BT/'btfm_slim_hw_interface.h').read_text()));s=s.replace('/* BT SOC CONSTANTS */','\n'.join('#define '+name+' '+str(i) for i,name in enumerate(sorted(set(re.findall(r'QCA_\w+_SOC_ID_\w+',sources['bt']))),1)));generated[group]=s;report['tests'][group]=execute(group,s)
  if a.negative:
   mutations={
    'async_registry_reference_lost_in_race':('transaction_race','if (txn->async)\n\t\trefcount_inc(&txn->refs);','if (false)\n\t\trefcount_inc(&txn->refs);'),
    'bt_stale_inode_guard_lost':('bt_lifetime','inode->i_cdev == btfm_slim_drv_data->cdev','true'),
    'bt_open_reference_lost':('bt_lifetime','kref_get(&btfm_slim_drv_data->ref);','(void)0;'),
    'core_status_callback_drain_lost':('core_lifetime','synchronize_srcu(&slim_status_srcu);','(void)0;'),
    'core_creation_drain_lost':('core_lifetime','synchronize_srcu(&slim_creation_srcu);','(void)0;'),
    'ngd_partial_rx_termination_lost':('ngd_dma','dmaengine_terminate_sync(ctrl->dma_rx_channel);','(void)0;'),
    'ngd_probe_pm_vote_leak':('ngd_lifecycle','pm_runtime_put_noidle(dev);','(void)0;'),
    'ngd_remove_dma_termination_missing':('ngd_lifecycle','qcom_slim_ngd_exit_dma(ctrl);','(void)0;'),
    'ngd_stack_completion_unrevoked':('ngd_transfer','ctrl->txdesc[index].comp = NULL;','(void)0;'),
    'ngd_post_rollback_missing':('ngd_transfer','ctrl->tx_tail = index;','(void)0;'),
    'ngd_rx_short_frame_accepted':('ngd_transfer','len < 4 ||','false ||'),
    'bt_failed_cleanup_state_discarded':('bluetooth','if (ret)\n\t\t\treturn ret;\n\t\tdai->needs_disable = false;','if (false)\n\t\t\treturn ret;\n\t\tdai->needs_disable = false;'),
    'bt_channel_map_truncation':('bluetooth','rx_slot[i] > U8_MAX','false'),
    'reply_overflow':('transactions','len <= txn->reply_len','true'),
    'async_response_ref_lost':('transactions','if (txn->async)\n\t\trefcount_inc(&txn->refs);','if (false)\n\t\trefcount_inc(&txn->refs);'),
    'transaction_error_cleanup_lost':('transactions','slim_free_txn_tid(ctrl, txn);','(void)0;'),
    'wide_read_count_truncated':('transactions','count > 16','false'),
    'new_address_output_missing':('addresses','*laddr = sbdev->laddr;','(void)0;'),
    'address_rollback_leak':('addresses','ida_free(&ctrl->laddr_ida, laddr);','(void)0;'),
    'stream_mask_bound_missing':('streams','cfg->port_mask & ~GENMASK(31, 0)','false'),
    'stream_device_reference_missing':('streams','get_device(&dev->dev);','(void)0;'),
    'stream_connect_error_ignored':('streams','if (ret)\n\t\t\tgoto disconnect;','if (false)\n\t\t\tgoto disconnect;'),
   };report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    src=generated[group];assert before in src;report['negative_controls'][name]=execute(name,src.replace(before,after,1),True)
 pins=json.loads((H/'bt-slim-imports.json').read_text());report['source_sha256']={pin['path']:hashlib.sha256((root/pin['path']).read_bytes()).hexdigest() for pin in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('slim_*'))};report['limits']='Extracted production functions, mocked kernel services. No hardware/firmware or actual IRQ/SRCU/workqueue proof.';a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
