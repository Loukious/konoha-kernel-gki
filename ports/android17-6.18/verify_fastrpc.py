from pathlib import Path
import argparse,subprocess,tempfile,hashlib,json,re,resource
ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--source-root',type=Path);ap.add_argument('--output',type=Path);ap.add_argument('--negative-controls',action='store_true');a=ap.parse_args()
h=Path(__file__).resolve().parent;root=a.source_root or a.workspace/a.package;s=root/'qcom/opensource/dsp-kernel';c=(s/'dsp/fastrpc.c').read_text();shared=(s/'dsp/fastrpc_shared.h').read_text();public=(s/'include/linux/fastrpc.h').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:int|long|void|bool)\s+'+name+r'\([^;{]*\{',text,re.M);assert m,name
 start=m.start();i=m.end();depth=1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+'\n'
def decl(text,kind,name):
 m=re.search(r'^[ \t]*'+kind+' '+name+r' \{.*?^[ \t]*\};',text,re.M|re.S);assert m,name
 return m.group()+'\n'
def macro(name):
 lines=shared.splitlines();i=next(i for i,line in enumerate(lines) if re.match(r'^#define '+name+r'\b',line));out=lines[i]
 while lines[i].endswith(chr(92)):
  i+=1;out+='\n'+lines[i]
 return out+'\n'
base_macros=''.join(macro(n) for n in ['FASTRPC_DEV_MAX','FASTRPC_MAX_SESSIONS','FASTRPC_MAX_CB_POOL','SMMU_4GB_ADDRESS_SPACE','DEFAULT_UNUSED','ROOT_PD','USER_UNSIGNEDPD_POOL'])
wire_macros=''.join(macro(n) for n in ['FASTRPC_CTXID_IDR_POS','FASTRPC_CTXID_IDR_BITS','FASTRPC_CTX_MAX','FASTRPC_CTXID_IDR_MASK','FASTRPC_GET_IDR_FROM_CTXID','FASTRPC_CTXID_PD_BITS','FASTRPC_CTXID_PD_MASK','FASTRPC_GET_CTXID_FROM_RSP_CTX','FASTRPC_RSP_VERSION2','FASTRPC_NOTIF_CTX_RESERVED'])
wire_types=wire_macros+''.join(decl(shared,'struct',n) for n in ['fastrpc_invoke_rsp','fastrpc_invoke_rspv2','dsp_notif_rsp'])+decl(shared,'enum','fastrpc_response_flags')
client_types=base_macros+''.join(decl(public,'enum',n) for n in ['fastrpc_driver_status'])+''.join(decl(public,'struct',n) for n in ['fastrpc_device','fastrpc_driver'])+decl(shared,'enum','fastrpc_process_state')
client_decl=decl(c,'struct','fastrpc_client')+'static LIST_HEAD(fastrpc_clients);\n'
client_code=''.join(fn(c,n) for n in ['fastrpc_device_put_locked','fastrpc_client_put_locked','fastrpc_client_finish','fastrpc_clients_release','fastrpc_driver_unregister','fastrpc_driver_register'])
fragments={'wire_types.inc':wire_types,'wire.inc':fn(c,'fastrpc_handle_rpc_response'),'client_types.inc':client_types,'client_decl.inc':client_decl,'clients.inc':client_code}
fragments['cb_types.inc']=base_macros+''.join(decl(shared,'struct',n) for n in ['fastrpc_buf','fastrpc_smmu','fastrpc_pool_ctx'])
fragments['cb.inc']=decl(c,'struct','fastrpc_cb_resource')+''.join(fn(c,n) for n in ['fastrpc_genpool_release','fastrpc_genpool_create','fastrpc_cb_probe','fastrpc_cb_remove'])
uapi=(s/'include/uapi/misc/fastrpc.h').read_text()
fragments['memory_types.inc']=decl(shared,'struct','fastrpc_buf')+decl(uapi,'struct','fastrpc_alloc_dma_buf')+decl(shared,'enum','fastrpc_buf_type')
fragments['memory.inc']=''.join(fn(c,n) for n in ['get_buffer_attr','fastrpc_release','fastrpc_mmap','fastrpc_dmabuf_alloc'])
transport=(s/'dsp/fastrpc_rpmsg.c').read_text()
transport_names=['FASTRPC_MAX_VMIDS','FASTRPC_MAX_SPD','ADSP_DOMAIN_ID','MDSP_DOMAIN_ID','SDSP_DOMAIN_ID','CDSP_DOMAIN_ID','FASTRPC_MAX_SESSIONS_PER_PROCESS','FASTRPC_CPUINFO_DEFAULT','FASTRPC_CPUINFO_EARLY_WAKEUP','AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME','AUDIO_PDR_ADSP_SERVICE_NAME','ADSP_AUDIOPD_NAME','SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME','SENSORS_PDR_ADSP_SERVICE_NAME','ADSP_SENSORPD_NAME','OIS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME','OIS_PDR_ADSP_SERVICE_NAME','ADSP_OISPD_NAME','SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME','SENSORS_PDR_SLPI_SERVICE_NAME','SLPI_SENSORPD_NAME','FASTRPC_NON_SECURE_WAKE_SOURCE_CLIENT_NAME','FASTRPC_SECURE_WAKE_SOURCE_CLIENT_NAME']
fragments['transport_types.inc']=''.join(macro(n) for n in transport_names)+decl(shared,'struct','gid_list')+decl(uapi,'enum','fastrpc_status_flags')+'#define QCOM_SCM_VMID_HLOS 3\n#define QCOM_SCM_PERM_RWX 7\nstatic const char *domains[4]={"adsp","mdsp","slpi","cdsp"};\n'
fragments['transport.inc']=''.join(fn(transport,n) for n in ['fastrpc_rpmsg_probe','fastrpc_rpmsg_remove'])
fragments['invoke_types.inc']=''.join(decl(public,'struct',n) for n in ['fastrpc_device','fastrpc_dev_map_dma','fastrpc_dev_unmap_dma','fastrpc_dev_get_hlos_pid'])+decl(shared,'enum','fastrpc_map_state')+macro('ADSP_MMAP_DMA_BUFFER')
fragments['invokes.inc']=decl(c,'union','fastrpc_dev_param')+''.join(fn(c,n) for n in ['fastrpc_channel_update_invoke_cnt','fastrpc_dev_map_dma','fastrpc_dev_unmap_dma'])
fragments['files.inc']=''.join(fn(c,n) for n in ['fastrpc_channel_update_invoke_cnt','fastrpc_device_release','fastrpc_device_open'])
fragments['files_types.inc']=''.join(macro(n) for n in ['FASTRPC_DSPSIGNAL_NUM_SIGNALS','FASTRPC_DSPSIGNAL_GROUP_SIZE','FASTRPC_UNIQUE_ID_CONST','DEFAULT_UNUSED'])+decl(shared,'enum','fastrpc_process_state')
controls=[];runs={}
with tempfile.TemporaryDirectory(prefix='fastrpc-tests-') as td:
 d=Path(td)
 for n,t in fragments.items():(d/n).write_text(t)
 commands={}
 for group in ['wire','clients','cb','memory','transport','invokes','files']:
  cmd=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie','-pthread','-I',str(d),str(h/'tests'/f'fastrpc_{group}.c'),'-o',str(d/group)];commands[group]=cmd
  subprocess.run(cmd,check=True);result=subprocess.run([str(d/group)],check=True,text=True,stdout=subprocess.PIPE,timeout=30);print(result.stdout.strip());runs[group]=json.loads(result.stdout)
 if a.negative_controls:
  variants=[('wire_missing_minimum','wire.inc','len < (int)sizeof(wire_ctx)','len < 0','wire'),('wire_missing_notification_bound','wire.inc','len < (int)sizeof(notif)','false','wire'),('wire_ignored_version','wire.inc','extended.version != FASTRPC_RSP_VERSION2','false','wire'),('wire_stale_job','wire.inc','ctx && ctx->ctxid == ctxid','ctx && (ctx->ctxid == ctxid || true)','wire'),('registry_missing_device_ref','clients.inc','client->device->refs++;','(void)client;','clients'),('registry_missing_unregister_drain','clients.inc','wait = client->active && client->active != current;','wait = false;','clients'),('registry_missing_probe_error_unwind','clients.inc','if (err)\n\t\t\t\t\tfastrpc_driver_unregister(driver);','if (false)\n\t\t\t\t\tfastrpc_driver_unregister(driver);','clients')]
  variants += [
   ('file_missing_id_rollback','files.inc','if (fl->tgid_frpc != -1)\n\t\tida_free(&cctx->tgid_frpc_ida,\n\t\t\t fl->tgid_frpc - cctx->domain_id * FASTRPC_UNIQUE_ID_CONST);','if (false)\n\t\tida_free(&cctx->tgid_frpc_ida, fl->tgid_frpc);','files'),
   ('file_post_ssr_count_underflow','files.inc','if (counted)','if (counted || true)','files'),
   ('cb_missing_rollback','cb.inc','devres_release_group(dev, group);','(void)group;','cb'),
   ('cb_negative_mapping_as_size','cb.inc','if (mapped < 0)','if (mapped < 0 && false)','cb'),
   ('cb_pool_bounds','cb.inc','sess && sess->smmucount >= FASTRPC_MAX_CB_POOL','sess && sess->smmucount >= FASTRPC_MAX_CB_POOL && false','cb'),
   ('cb_missing_mapping_release','cb.inc','if (res->mapped)','if (res->mapped && false)','cb'),
   ('permission_copy_leak','memory.inc','kfree(vmids_list);','(void)vmids_list;','memory'),
   ('export_missing_channel_ref','memory.inc','fastrpc_channel_ctx_get(fl->cctx);','(void)fl;','memory'),
   ('export_mmap_allocation_bound','memory.inc','FASTRPC_PHYS(buf->phys), buf->size);','FASTRPC_PHYS(buf->phys), vma->vm_end - vma->vm_start);','memory'),
   ('transport_partial_child_cleanup','transport.inc','of_platform_depopulate(rdev);','(void)rdev;','transport'),
   ('transport_pdr_failure_cleanup','transport.inc','pdr_handle_release(data->spd[i].pdrhandle);','(void)data;','transport'),
   ('transport_missing_export_drain','transport.inc','wait_event(cctx->export_wait, !atomic_read(&cctx->exported_bufs));','(void)cctx;','transport'),
   ('invoke_unheld_mutex_release','invokes.inc','if (remote_locked)','if (remote_locked || true)','invokes'),
   ('invoke_boolean_pending','invokes.inc','atomic_dec_and_test(&fl->dma_invokes)','atomic_exchange(&fl->dma_invokes, 0)','invokes'),
  ]
  for name,frag,old,new,group in variants:
   f=d/frag;original=f.read_text();assert old in original,name;f.write_text(original.replace(old,new,1));subprocess.run(commands[group],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   result=subprocess.run([commands[group][-1]],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)));f.write_text(original);assert result.returncode!=0,'Negative control survived: '+name
   controls.append({'name':name,'rejected':True,'returncode':result.returncode});print('REJECTED',name)
report={'status':'PASS','tests':runs,'negative_controls':controls,'source_sha256':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in json.loads((h/'fastrpc-sources.json').read_text())},'harness_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((h/'tests').glob('fastrpc_*'))},'limits':'Actual extracted functions with mocked kernel services; no DSP/firmware, kernel IRQ/VFS/devres/IOMMU runtime or hardware validation.'}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
