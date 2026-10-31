#!/usr/bin/env python3
"""Prove selected main display failure-path tests reject their pinned originals."""
import argparse,hashlib,json,resource,subprocess,tempfile
from pathlib import Path
from verify_display_support import function
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--output',type=Path);args=p.parse_args()
imports={i['path']:i for i in json.loads((HERE/'display-main-imports.json').read_text())}
pre='qcom/opensource/display-drivers/msm/'
groups={
'display_main_gem':[(pre+'msm_gem.c',['put_pages','get_pages','msm_gem_dumb_create','msm_gem_cpu_prep']),(pre+'msm_fb.c',['msm_framebuffer_check_size'])],
'display_main_text':[(pre+'mi_disp/mi_disp_procfs.c',['mi_disp_procfs_tx_cmd_set_write','mi_disp_procfs_mipi_rw_write','mi_disp_procfs_dsi_phy_rw_write','mi_disp_procfs_dsi_porch_rw_write','mi_disp_procfs_pps_rw_write']),(pre+'mi_disp/mi_disp_log.c',['mi_disp_log_printk_func','mi_disp_log_read_stats','mi_disp_log_write']),(pre+'mi_disp/mi_dsi_panel.c',['mi_dsi_panel_show_dsi_cmd_set_type'])],
'display_main_async':[(pre+'mi_disp/mi_disp_file.c',['mi_disp_set_feature_work_handler','mi_disp_set_feature_queue_work','mi_disp_ioctl_set_count_info_work_handler','mi_disp_ioctl_set_count_info_queue_work']),(pre+'mi_disp/mi_disp_feature.c',['mi_disp_feature_event_notify_by_type'])],
'display_main_workers':[(pre+'msm_drv.c',['msm_drm_display_thread_create']),(pre+'sde/sde_fence.c',['sde_sync_get']),(pre+'sde_dbg.c',['sde_register_md_panic_notifer','sde_unregister_md_panic_notifier','sde_mini_dump_add_va_region'])]}
replace={'display_main_gem':'msm_gem_cpu_prep','display_main_text':'mi_disp_procfs_dsi_phy_rw_write','display_main_async':'mi_disp_set_feature_queue_work','display_main_workers':'msm_drm_display_thread_create'};results={}
with tempfile.TemporaryDirectory(prefix='onyx-display-main-negative-') as tmp:
 root=Path(tmp)
 for group,parts in groups.items():
  defs=[]
  for path,names in parts:
   current=(args.workspace/args.source_dir/path).read_text();it=imports[path];old=subprocess.check_output(['git','-C',str(args.workspace/it['project']),'cat-file','blob',it['blob']],text=True)
   for name in names:defs.append(function(old if name==replace[group] else current,name))
  harness=HERE/'tests'/(group+'.c');code=harness.read_text().replace('/* FUNCTIONS */','\n'.join(defs));source=root/(group+'.c');source.write_text(code);binary=root/group
  subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-include','limits.h','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(source),'-o',str(binary)],check=True)
  def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30,preexec_fn=no_core);assert run.returncode and any(s in run.stderr for s in ['Assertion','AddressSanitizer','runtime error:']),run.stderr
  results[group]={'original_function':replace[group],'expected_failure':True,'exit':run.returncode,'harness_sha256':hashlib.sha256(harness.read_bytes()).hexdigest()}
text=json.dumps(results,indent=2)+'\n'
if args.output:args.output.write_text(text)
print(text,end='')
