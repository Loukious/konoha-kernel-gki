from pathlib import Path
import argparse,subprocess,tempfile,json,re,hashlib,resource
ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--source-root',type=Path);ap.add_argument('--output',type=Path);ap.add_argument('--negative-controls',action='store_true');a=ap.parse_args();h=Path(__file__).resolve().parent;root=a.source_root or a.workspace/a.package;b=root/'qcom/opensource/eva-kernel/msm/eva'
def fn(text,name):
 m=re.search(r'^(?:static )?(?:inline )?[\w *]+\b'+name+r'\([^;{]*\{',text,re.M);assert m,name
 i=m.end();depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]+'\n'
def funcs(file,names):
 s=(b/file).read_text();return ''.join(fn(s,n) for n in names)
def decl(file,kind,name):
 s=(b/file).read_text();m=re.search(r'^'+kind+' '+name+r' \{.*?^\};',s,re.M|re.S);assert m,name;return m.group()+'\n'
uapi=(root/'qcom/opensource/eva-kernel/include/uapi/eva/media/msm_eva_private.h').read_text();assert re.search(r'#define\s+MAX_HFI_PKT_SIZE\s+490\b',uapi)
fragments={
 'wire.inc':funcs('msm_cvp_dsp.c',['cvp_dsp_rpmsg_callback']),
 'wire_types.inc':''.join(decl('msm_cvp_dsp.h','struct',n) for n in ['eva_power_req','eva_mem_remote','cvp_dsp_rsp_msg','cvp_dsp2cpu_cmd']),
 'memory.inc':funcs('cvp_smem.c',['__cvp_dma_buf_map_attachment','__cvp_dma_buf_unmap_attachment','msm_dma_get_device_address','msm_dma_put_device_address','alloc_dma_mem','free_dma_mem','msm_cvp_smem_cache_operations']),
 'vmap.inc':funcs('msm_cvp_buf.c',['msm_cvp_dma_buf_vmap','msm_cvp_dma_buf_vunmap']),
 'nodes.inc':funcs('msm_cvp_core.c',['cvp_get_core','cvp_put_core'])+funcs('cvp.c',['cvp_release_core','cvp_child_ready','msm_probe_cvp_device','msm_cvp_remove']),
 'irq.inc':funcs('vm/cvp_vm_main.c',['msm_cvp_vm_init_reg_and_irq'])+funcs('cvp_hfi.c',['__add_device','cvp_iris_hfi_delete_device']),
 'dsp.inc':funcs('msm_cvp_dsp.c',['cvp_dsp_device_init','cvp_dsp_device_exit','cvp_dsp_device_deinit']),
 'ioctl.inc':funcs('msm_cvp_ioctl.c',['_get_pkt_hdr_from_user','_get_fence_pkt_hdr_from_user','_copy_pkt_from_user','_copy_checked_pkt_from_user','_copy_pkt_to_user']),
 'registry.inc':funcs('msm_cvp_dsp.c',['cvp_put_fastrpc_node','cvp_get_fastrpc_node_with_handle','dequeue_frpc_node','pop_frpc_node','cvp_destroy_dsp_node','cvp_remove_dsp_sessions','eva_fastrpc_driver_unregister','cvp_fastrpc_callback','cvp_fastrpc_cleanup_worker']),
 'file.inc':funcs('msm_cvp_buf.c',['msm_cvp_fget']),
}
runs={};controls=[]
with tempfile.TemporaryDirectory(prefix='eva-tests-') as td:
 d=Path(td)
 for n,s in fragments.items():(d/n).write_text(s)
 commands={}
 for group in ['wire','memory','nodes','irq','dsp','ioctl','registry']:
  cmd=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie','-pthread','-I',str(d),str(h/'tests'/('eva_'+group+'.c')),'-o',str(d/group)];commands[group]=cmd
  subprocess.run(cmd,check=True);p=subprocess.run([str(d/group)],check=True,text=True,stdout=subprocess.PIPE,timeout=30);print(p.stdout.strip());runs[group]=json.loads(p.stdout)
 if a.negative_controls:
  variants=[('wire_missing_size_check','wire.inc','if (!data || (len != sizeof(packet.rsp) && len != sizeof(packet.cmd)))','if (!data)','wire'),('wire_command_type_overlap','wire.inc','rsp->type >= DSP2CPU_POWERON &&','true &&','wire'),('memory_no_contiguous_check','memory.inc','table->nents != 1','false','memory'),('memory_unchecked_end','memory.inc','dbuf->size - 1 > U32_MAX - sg_dma_address(table->sgl)','false','memory'),('memory_zero_iova_leak','memory.inc','if (mem->mapping_info.attach)','if (mem->device_addr)','memory'),('memory_heap_ref_leak','memory.inc','dma_heap_put(heap);','(void)heap;','memory'),('memory_no_failed_map_unwind','memory.inc','fail_mapping:\n\tmsm_dma_put_device_address(mem->flags, &mem->mapping_info);','fail_mapping:\n\t(void)mem;','memory'),('file_accepts_next_fd','file.inc','found != fd ||','false ||','memory'),('vmap_missing_address','vmap.inc','iosys_map_set_vaddr(&vmap->map, vmap->vaddr);','(void)vmap;','memory')]
  variants += [('node_publish_incomplete','nodes.inc','core->ready = true;','core->ready = false;','nodes'),('node_no_cdev_withdrawal','nodes.inc','cdev_del(core->cdev);','(void)core;','nodes'),('node_release_ignores_users','nodes.inc','wait_for_completion(&core->users_zero);','(void)core;','nodes'),('irq_second_failure_leak','irq.inc','free_irq(res->irq, device);','(void)device;','irq'),('irq_wd_not_freed','irq.inc','free_irq(dev->cvp_hal_data->irq_wd, dev);','(void)dev;','irq'),('dsp_error_pointer_accepted','dsp.inc','if (IS_ERR(me->dsp_thread))','if (!me->dsp_thread)','dsp'),('dsp_missing_worker_join','dsp.inc','kthread_stop(me->dsp_thread);','(void)me;','dsp')]
  variants += [('ioctl_header_race','ioctl.inc','kp->data.hfi_pkt.pkt_data[0] = hdr->size;','(void)hdr;','ioctl'),('ioctl_source_range_overflow','ioctl.inc','if (start > MAX_HFI_PKT_SIZE || size > MAX_HFI_PKT_SIZE - start)','if (false)','ioctl')]
  variants += [('registry_skip_ref_drain','registry.inc','while (atomic_read(&node->refcount))','while (false)','registry'),('registry_skip_session_cleanup','registry.inc','delete_dsp_session(inst, node);','(void)inst;','registry')]
  for name,frag,old,new,group in variants:
   p=d/frag;original=p.read_text();assert old in original,name;p.write_text(original.replace(old,new,1));subprocess.run(commands[group],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   result=subprocess.run([str(d/group)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)));p.write_text(original);assert result.returncode!=0,'Survived '+name;controls.append({'name':name,'rejected':True,'returncode':result.returncode});print('REJECTED',name)
report={'status':'PASS','groups':runs,'negative_controls':controls,'source_sha256':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in json.loads((h/'eva-sources.json').read_text())},'harness_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (h/'tests').glob('eva_*')},'limits':'Actual extracted functions with mocked kernel services; no kernel VFS/IRQ/IOMMU, firmware or hardware execution.'}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
