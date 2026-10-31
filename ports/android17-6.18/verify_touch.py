#!/usr/bin/env python3
"""Run extracted touch boundary and lifetime functions under ASan/UBSan."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(src,name):
 src=re.sub(r'/\*.*?\*/','',src,flags=re.S)
 m=re.search(r'(?m)^(?:static )?(?:inline )?[\w *]+\b'+name+r'\([^;]*?\)\n\{',src)
 if not m:raise ValueError(name)
 end=m.end();depth=1
 while depth:depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[m.start():end]+'\n'

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--output',type=Path);ap.add_argument('--firmware-dir',type=Path);a=ap.parse_args();w=a.workspace.resolve();b=w/a.source_dir/'qcom/opensource/touch-drivers'
 files={p.name:p.read_text() for p in b.rglob('*.c')}
 groups={'touch_proc_read':[('nt36xxx_ext_proc.c','xm_htc_op_mode_proc_read')], 'touch_csv':[('nt36xxx_mp_ctrlram.c',n) for n in ['goto_next_line','copy_this_line','parse_mp_setting_criteria_item','parse_mp_setting_ain_array','parse_mp_criteria_item_array']], 'touch_removal':[('nt36xxx.c','nvt_ts_remove')], 'touch_registration':[('xiaomi_touch_core.c','register_touch_panel')], 'touch_mmap':[('xiaomi_touch_operations.c','xiaomi_touch_dev_mmap_locked')], 'touch_ring':[('xiaomi_touch_core.c','add_common_data_to_buf')], 'touch_firmware':[('nt36xxx_fw_update.c',n) for n in ['CheckSum','byte_to_word','nvt_bin_header_parser']], 'touch_flash':[('nt36xxx.c','nvt_flash_read')], 'touch_points':[('xiaomi_touch_device.c',n) for n in ['report_finger_event','report_stylus_event','report_touch_event']]}
 results={}
 with tempfile.TemporaryDirectory(prefix='onyx-touch-tests-') as tmp:
  d=Path(tmp)
  for name,funcs in groups.items():
   src=(HERE/'tests'/f'{name}.c').read_text().replace('/* FUNCTIONS */','\n'.join(function(files[f],n) for f,n in funcs))
   if name=='touch_firmware':
    fixture=HERE/'touch-imports.json'
    pins=json.loads(fixture.read_text());pin=next(e for e in pins if e['path'].endswith('nt36xxx_fw_update.c'))
    original=subprocess.check_output(['git','-C',str(w/pin['project']),'cat-file','blob',pin['blob']],text=True)
    legacy=function(original,'nvt_bin_header_parser')
    for var in ['nvt_bin_header_parser','bin_map','partition','cascade_2nd_header_info','spi_dma_div_cnt_val']:
     legacy=re.sub(r'\b'+var+r'\b','original_'+var,legacy)
    src=src.replace('/* ORIGINAL */',legacy)
   c=d/f'{name}.c';c.write_text(src);exe=d/name
   subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-sign-compare','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(exe)],check=True)
   firmware_paths=sorted(a.firmware_dir.glob('*.bin')) if name=='touch_firmware' and a.firmware_dir else []
   run=subprocess.run([str(exe)]+[str(p) for p in firmware_paths],capture_output=True,text=True,timeout=45)
   if run.returncode:raise RuntimeError(name+': '+run.stderr)
   results[name]=json.loads(run.stdout)
 if a.firmware_dir:results['rom_firmware_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(a.firmware_dir.glob('*.bin'))}
 results['source_sha256']={str(p.relative_to(w/a.source_dir)):hashlib.sha256(p.read_bytes()).hexdigest() for p in b.rglob('*') if p.is_file()}
 results['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'tests').glob('*.c')}
 # Validate import identities and caller-visible interfaces separately from mocks.
 fixture=HERE/'touch-imports.json'
 pins=json.loads(fixture.read_text())
 for entry in pins:
  expected=subprocess.check_output(['git','-C',str(w/entry['project']),'rev-parse',entry['revision']+':'+entry['path']],text=True).strip()
  assert expected==entry['blob']
  if entry['path'].endswith('include/uapi/touch/xiaomi_touch.h'):
   original=subprocess.check_output(['git','-C',str(w/entry['project']),'cat-file','blob',entry['blob']])
   assert (w/a.source_dir/entry['path']).read_bytes()==original
 ext=files['nt36xxx_ext_proc.c']
 assert 'copy_to_user(buf, tmp_buf, sizeof(tmp_buf))' not in ext
 assert ext.count('simple_read_from_buffer(buf, count, f_pos, tmp_buf, cnt)')==23
 ops=files['xiaomi_touch_operations.c'];core=files['xiaomi_touch_core.c'];nvt=files['nt36xxx.c']
 assert 'PAGE_SHARED' not in function(ops,'xiaomi_touch_dev_mmap_locked')
 assert 'VM_MAYEXEC' in function(ops,'xiaomi_touch_dev_mmap_locked')
 for text in [core,nvt]:assert '.suppress_bind_attrs = true' in text and '__module_get(THIS_MODULE)' in text
 unreg=function(core,'unregister_touch_panel')
 assert unreg.index('touch_ready, false')<unreg.index('xiaomi_touch_remove_proc')<unreg.index('xiaomi_touch_mode_remove')<unreg.index('hardware_operation,')
 assert 'free_pages_exact' not in unreg and 'kzalloc_free' not in unreg
 results['source_contracts']={'exact_imports_checked':len(pins),'touch_uapi_unchanged':True,'proc_readers_using_bounded_copies':23,'resident_driver_and_mapping_lifetime':True}
 results['limits']='Extracted driver functions with mocked Linux services; verifies memory, framing and resource contracts, not live VFS/IRQ/SPI semantics, firmware execution, timing or phone operation.'
 out=json.dumps(results,indent=2)+'\n';print(out,end='')
 if a.output:a.output.write_text(out)
if __name__=='__main__':main()
