#!/usr/bin/env python3
"""Check actual staged GPU helpers with ASan/UBSan and pinned source contracts."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
G='qcom/opensource/graphics-kernel/'
S='qcom/opensource/synx-kernel/msm/synx/'
F='qcom/opensource/mm-drivers/hw_fence/'
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b'+name+r'\([^;]*?\)\n\{',source)
 if not m:raise ValueError(name)
 end,depth=m.end(),1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]+'\n'
def structure(source,name):
 return re.search(r'(?m)^struct '+name+r' \{.*?^\}[^;]*;',source,re.S)[0]+'\n'
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args()
 W=a.workspace.resolve();D=W/a.source_dir
 used={}
 def read(p):
  used[p]=(D/p).read_bytes();return used[p].decode()
 ipc=read(S+'ipclite.c');synx=read(S+'synx.c');iommu=read(G+'kgsl_iommu.c');gmu=read(G+'adreno_gen8_gmu.c');hfi=read(G+'adreno_gen8_hfi.c');hw=read(F+'src/hw_fence_drv_utils.c');reclaim=read(G+'kgsl_reclaim.c');core=read(G+'kgsl.c');adreno=read(G+'adreno.c');shmem=read(G+'kgsl_sharedmem.c');priv=read(F+'src/hw_fence_drv_priv.c');hwm=read(F+'src/msm_hw_fence.c')
 contracts=[]
 def check(name,condition):
  assert condition,name;contracts.append(name)
 init=function(adreno,'kgsl_3d_init');exit=function(core,'kgsl_core_exit')
 check('restricted hook is final fallible KGSL init step',init.index('platform_driver_register')<init.index('kgsl_register_shmem_callback')<init.index('smp_store_release'))
 check('successful restricted hook retains module text','__module_get(THIS_MODULE)' in function(shmem,'kgsl_register_shmem_callback'))
 check('compound shmem requests defer to core','IS_ERR_OR_NULL(memdesc) || order' in function(shmem,'kgsl_shmem_fill_page'))
 check('KGSL opens gated before device lookup',function(core,'kgsl_open').index('smp_load_acquire')<function(core,'kgsl_open').index('kgsl_get_minor'))
 check('KGSL sysstats removed before teardown',exit.index('sysstats_unregister')<exit.index('kgsl_reclaim_close')<exit.index('destroy_workqueue')<exit.index('kgsl_events_exit'))
 check('KGSL cdev lifetime independent of static storage','cdev_alloc()' in function(core,'kgsl_core_init') and 'cdev_del(kgsl_driver.cdev)' in exit)
 check('KGSL virtual device release synchronized','wait_for_completion(&kgsl_virtdev_released)' in exit)
 check('IOMMU references are balanced','of_get_child_by_name' in function(iommu,'kgsl_iommu_get_context_device') and 'put_device' in function(iommu,'kgsl_iommu_detach_context'))
 check('reclaim request bounded by atomic signed width',function(reclaim,'kgsl_reclaim_shrink_scan_objects').count('INT_MAX')==2)
 check('shrinker drained before workqueue destroyed',exit.index('kgsl_reclaim_close')<exit.index('destroy_workqueue'))
 check('work period timer cannot rearm on removal','timer_shutdown_sync' in function(core,'kgsl_device_platform_remove'))
 init=function(synx,'synx_init')
 check('native SYNX state exists before SSR or IPC callbacks',init.index('synx_local_mem_init')<init.index('qcom_register_ssr_notifier')<init.index('ipclite_register_client'))
 check('SYNX callback publication precedes permanent module reference',init.index('ipclite_register_client')<init.index('__module_get')<init.index('smp_store_release'))
 check('SYNX native allocation failure reports ENOMEM','rc = -ENOMEM' in init)
 check('SYNX workqueues destroyed on failed initialization',init.count('destroy_workqueue')==2)
 check('SYNX IPC consumes only published state','smp_load_acquire(&synx_ready)' in function(synx,'synx_ipc_callback'))
 check('IPCLite IRQ never sees a torn registration','smp_load_acquire(&synx_client.reg_complete)' in function(ipc,'ipclite_rx_data') and 'smp_store_release' in function(ipc,'ipclite_register_client_v0'))
 check('IPCLite transmit does not mutate receive IRQ identity','signal_id =' not in function(ipc,'ipclite_tx'))
 close=function(ipc,'ipclite_channel_close')
 check('IPCLite IRQ and mailbox drain before child release',close.index('devm_free_irq')<close.index('mbox_free_channel')<close.index('ipclite_child_unregister'))
 check('IPCLite child reference release synchronized','wait_for_completion' in function(ipc,'ipclite_child_unregister'))
 init=function(ipc,'ipclite_init_v0')
 check('IPCLite debug state initialized before IRQs',init.index('ipclite_debug_setup')<init.index('ipclite_channel_init'))
 check('IPCLite firmware init_done published last',init.index('insert_magic_number')<init.index('hdr.init_done =')<init.index('mbox_send_message'))
 check('IPCLite API rejects unprobed provider','smp_load_acquire(&ipclite_ready)' in function(ipc,'get_global_partition_info'))
 check('IPCLite singleton cannot sysfs unbind','.suppress_bind_attrs = true' in ipc and '__module_get(THIS_MODULE)' in init)
 alloc=function(hw,'hw_fence_utils_alloc_mem');share=function(hw,'hw_fence_gunyah_share_mem')
 check('HW fence allocation does not publish notifiers','_register_vm_mem_with_hyp' not in alloc)
 check('SCM descriptor allocations precede ownership assignment',share.index('acl = kzalloc')<share.index('sgl = kzalloc')<share.index('qcom_scm_assign_mem'))
 close=function(hw,'hw_fence_utils_release')
 check('HW fence IRQ thread callbacks quiesce before mappings free',close.index('devm_free_irq')<close.index('kthread_stop')<close.index('iommu_unmap')<close.index('dma_free_attrs'))
 check('HW fence listener observes stop during idle and active waits',function(hw,'hw_fence_soccp_listener').count('kthread_should_stop()')==3)
 init=function(priv,'hw_fence_init')
 check('HW fence tables initialized before interrupt delivery',init.index('hw_fence_dma_fence_init_hash_table')<init.index('hw_fence_utils_init_soccp_irq'))
 check('HW fence shares only after resources are initialized',init.index('hw_fence_utils_init_soccp_irq')<init.index('hw_fence_utils_share_mem')<init.index('__module_get'))
 check('HW fence singleton cannot sysfs unbind','.suppress_bind_attrs = true' in hwm)
 timeline=read(G+'kgsl_timeline.c')
 check('software timeline preserves 64-bit seqno','dma_fence_init64' in timeline and '.use_64bit_seqno' not in timeline)
 # Preserve hardware/register data and userspace ABI by comparing exact pinned Git blobs.
 imports=json.loads((HERE/'gpu-imports.json').read_text());protected=[]
 for item in imports:
  p=item['path']
  if (p.endswith('_reg.h') or p.endswith('_snapshot.h') or p.endswith('adreno-gpulist.h') or '/include/uapi/' in p or p==G+'adreno_hfi.h'):
   original=subprocess.check_output(['git','-C',str(W/item['project']),'cat-file','blob',item['blob']])
   assert (D/p).read_bytes()==original,p;protected.append(p)
 ipc_h=read(S+'ipclite.h');orig=(W/'onyx-imports'/S/'ipclite.h').read_text()
 for name in ['ipcmem_toc_header','ipcmem_offsets','ipcmem_partition_header']:
  check('IPCLite firmware ABI '+name+' unchanged',structure(ipc_h,name)==structure(orig,name))
 config=dict(re.findall(r'^(CONFIG_\w+)=(.*)$',(D/'platform_defconfig').read_text(),re.M))
 required={'QCOM_KGSL':'m','QCOM_KGSL_IDLE_TIMEOUT':'80','QCOM_KGSL_SORT_POOL':'y','QCOM_KGSL_CONTEXT_DEBUG':'y','QCOM_KGSL_IOCOHERENCY_DEFAULT':'y','QCOM_ADRENO_DEFAULT_GOVERNOR':'"msm-adreno-tz"','QCOM_KGSL_SYNX':'y','QCOM_KGSL_USE_SHMEM':'y','QCOM_KGSL_PROCESS_RECLAIM':'y','DEVFREQ_GOV_QCOM_ADRENO_TZ':'m','DEVFREQ_GOV_QCOM_GPUBW_MON':'m','QCOM_SPEC_SYNC':'y'}
 for k,v in required.items():assert config['CONFIG_'+k]==v,k
 types=structure(read(G+'kgsl_gmu_core.h'),'gmu_block_header')
 hfi_h=read(G+'adreno_hfi.h')
 for name in ['hfi_queue_table_header','hfi_queue_header','hfi_queue_table']:types+=structure(hfi_h,name)
 types+=structure(ipc_h,'ipcmem_toc_header')
 groups={
  'gpu_bounds':[((W/'common/include/linux/dma-fence.h').read_text(),['__dma_fence_is_later']),(timeline,['to_timeline_fence','timeline_fence_signaled']),(hw,['hw_fence_checked_region_size']),(reclaim,['kgsl_reclaim_remaining']),(ipc,['ipcmem_rx_avail','ipcmem_rx_peak','ipcmem_tx_avail','ipcmem_tx_write_one','insert_magic_number']),(gmu,['gen8_gmu_fw_valid','find_gmu_memdesc','find_vma_block']),(hfi,['gen8_hfi_ring_valid','gen8_hfi_queue_read','gen8_hfi_queue_write'])],
  'gpu_iommu':[(iommu,['kgsl_iommu_detach_context','kgsl_iommu_get_context_device','kgsl_iommu_setup_context'])],
  'gpu_synx_init':[(synx,['synx_init'])],
  'gpu_shmem':[(shmem,['kgsl_shmem_fill_page'])],
  'gpu_hwfence_listener':[(hw,['hw_fence_soccp_listener'])],
 }
 results={};harness_hashes={}
 with tempfile.TemporaryDirectory(prefix='onyx-gpu-tests-') as temp:
  tmp=Path(temp)
  for name,parts in groups.items():
   template=HERE/'tests'/f'{name}.c';harness_hashes[template.name]=hashlib.sha256(template.read_bytes()).hexdigest()
   definitions='\n'.join(function(source,f) for source,names in parts for f in names)
   source=template.read_text().replace('/* STAGED_TYPES */',types).replace('/* STAGED_DEFINITIONS */',definitions)
   (tmp/f'{name}.c').write_text(source)
   subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(tmp/f'{name}.c'),'-o',str(tmp/name)],check=True)
   run=subprocess.run([str(tmp/name)],capture_output=True,text=True)
   if run.returncode:raise RuntimeError(name+': '+run.stderr)
   results.update(json.loads(run.stdout))
 report={'tests':results,'cases':sum(v for k,v in results.items() if k.endswith('_cases')),'source_contracts':contracts,'preserved_hardware_uapi_files':protected,'rom_config':required,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'sha256':{p:hashlib.sha256(b).hexdigest() for p,b in used.items()},'harness_sha256':harness_hashes,'limits':'Actual staged helper bodies with simulated kernel services; source publication/cleanup order checks. No live IRQ concurrency, firmware authentication, GMU rendering, secure-world assignment, real DMA/IOMMU faults, reclaim pressure, suspend/resume or hardware boot validation.'}
 output=json.dumps(report,indent=2)+'\n'
 if a.output:a.output.write_text(output)
 print(output,end='')
if __name__=='__main__':main()
