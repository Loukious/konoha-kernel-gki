#!/usr/bin/env python3
"""Check display migration contracts and extracted request/lifetime functions."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
from verify_display_support import function
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('workspace',type=Path);parser.add_argument('--source-dir',default='onyx');parser.add_argument('--output',type=Path);args=parser.parse_args()
base=args.workspace/args.source_dir;used={}
def read(path):
 b=(base/path).read_bytes();used[path]=hashlib.sha256(b).hexdigest();return b.decode()
pre='qcom/opensource/display-drivers/msm/'
driver=read(pre+'msm_drv.c');header=read(pre+'msm_drv.h');feature=read(pre+'mi_disp/mi_disp_feature.c');file=read(pre+'mi_disp/mi_disp_file.c');fence=read(pre+'sde/sde_fence.c');debug=read(pre+'sde_dbg.c');panel=read(pre+'mi_disp/mi_dsi_panel.c');gem=read(pre+'msm_gem.c');fb=read(pre+'msm_fb.c');log=read(pre+'mi_disp/mi_disp_log.c');proc=read(pre+'mi_disp/mi_disp_procfs.c');contracts=[]
def check(name,condition):
 assert condition,name;contracts.append(name)
init=function(driver,'msm_drm_device_init');check('GEM lock exists before display resources initialize',init.index('mutex_init(&priv->gem_lock)')<init.index('sde_power_resource_init'))
check('All migrated GEM paths share the same private mutex','struct mutex gem_lock' in header and 'msm_gem_mutex' in header and '->struct_mutex' not in driver)
close=function(driver,'msm_release');check('Last close retains device around core release',close.index('drm_dev_get(dev)')<close.index('drm_release(inode, filp)')<close.index('msm_lastclose(dev)')<close.index('drm_dev_put(dev)'))
check('DRM file operations retain unsigned offset requirement','FOP_UNSIGNED_OFFSET' in driver)
check('Device and callback ownership keeps successful display module resident','__module_get(THIS_MODULE)' in function(driver,'msm_drm_register'))
init=function(feature,'mi_disp_feature_init');check('Client list and locks precede cdev exposure',init.index('spin_lock_init')<init.index('mi_disp_cdev_register'))
check('Feature singleton publishes after complete cdev and device setup',init.index('device_create')<init.index('smp_store_release'))
check('Feature lookup cannot reenter initialization','mi_disp_feature_init()' not in function(feature,'mi_get_disp_feature'))
open_body=function(file,'mi_disp_open');check('Client event state exists before notification publication',open_body.index('INIT_LIST_HEAD(&client->event_list)')<open_body.index('list_add_tail'))
check('Old cdev identities cannot open the current singleton','inode->i_cdev != df->cdev' in open_body)
check('Raw events cannot overflow the 4 KiB event budget','event->length > 4096 - sizeof(struct disp_event_resp)' in feature)
check('DSI receive alignment rejects unsigned overflow','req->rx_len > U32_MAX - 3' in file)
check('Debug notification and work drain before buffers free',function(debug,'sde_dbg_destroy').index('sde_unregister_md_panic_notifier')<function(debug,'sde_dbg_destroy').index('cancel_work_sync')<function(debug,'sde_dbg_destroy').index('vfree'))
check('Panel withdraws the draining pwrkey callback before teardown',function(panel,'mi_dsi_panel_deinit').index('mi_display_pwrkey_callback_set(NULL)')<function(panel,'mi_dsi_panel_deinit').index('wakeup_source_unregister'))
check('CPU read/write waits use the Linux reservation usage enum','dma_resv_usage_rw(write)' in function(gem,'msm_gem_cpu_prep'))
check('Base and private GEM objects share reservation state','(*obj)->resv = msm_obj->resv' in gem)
check('Failed GEM setup can remove an initialized inactive-list entry','INIT_LIST_HEAD(&msm_obj->mm_list)' in gem)
check('VRAM pages release before their VMA node is freed',function(gem,'msm_gem_free_object').index('put_pages(obj)')<function(gem,'msm_gem_free_object').index('put_iova(obj)'))
check('Embedded DRM client owns its unregister callback','.unregister = msm_drm_client_unregister' in driver and 'drm_client_release(client)' in function(driver,'msm_drm_client_unregister'))
check('Log formatter reports only bytes present in its local buffer','vscnprintf' in function(log,'mi_disp_log_printk_func') and 'static char textbuf' not in log)
check('Log reads serialize with ring writes','spin_lock_irqsave' in function(log,'mi_disp_log_read_stats'))
check('Procfs stack writes reserve space for the terminator','count >= sizeof(cmd_buffer)' in function(proc,'mi_disp_procfs_dsi_phy_rw_write'))
groups={
 'display_main_text':[(proc,['mi_disp_procfs_tx_cmd_set_write','mi_disp_procfs_mipi_rw_write','mi_disp_procfs_dsi_phy_rw_write','mi_disp_procfs_dsi_porch_rw_write','mi_disp_procfs_pps_rw_write']),(log,['mi_disp_log_printk_func','mi_disp_log_read_stats','mi_disp_log_write']),(panel,['mi_dsi_panel_show_dsi_cmd_set_type'])],
 'display_main_gem':[(gem,['put_pages','get_pages','msm_gem_dumb_create','msm_gem_cpu_prep']),(fb,['msm_framebuffer_check_size'])],
 'display_main_async':[(file,['mi_disp_set_feature_work_handler','mi_disp_set_feature_queue_work','mi_disp_ioctl_set_count_info_work_handler','mi_disp_ioctl_set_count_info_queue_work']),(feature,['mi_disp_feature_event_notify_by_type'])],
 'display_main_workers':[(driver,['msm_drm_display_thread_create']),(fence,['sde_sync_get']),(debug,['sde_register_md_panic_notifer','sde_unregister_md_panic_notifier','sde_mini_dump_add_va_region'])],
 'display_main_close':[(driver,['msm_fops_open','msm_release'])],
}
results={};harnesses={}
with tempfile.TemporaryDirectory(prefix='onyx-display-main-tests-') as tmp:
 root=Path(tmp)
 for group,parts in groups.items():
  path=HERE/'tests'/(group+'.c');data=path.read_bytes();harnesses[path.name]=hashlib.sha256(data).hexdigest();code=data.decode().replace('/* FUNCTIONS */','\n'.join(function(s,n) for s,names in parts for n in names));source=root/(group+'.c');source.write_text(code);binary=root/group
  subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-include','limits.h','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(source),'-o',str(binary)],check=True)
  run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=60);assert not run.returncode,run.stdout+run.stderr;results.update(json.loads(run.stdout))
report={'tests':results,'source_contracts':contracts,'source_sha256':used,'harness_sha256':harnesses,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Extracted functions with simulated allocator, workers, DRM reference counting, notifier and OF services. Tests do not establish kernel scheduling, DRM atomic commits, hardware fences, display firmware, suspend/resume or phone compatibility.'};text=json.dumps(report,indent=2)+'\n'
if args.output:args.output.write_text(text)
print(text,end='')
