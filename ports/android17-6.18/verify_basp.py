#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 m=re.search(r'(?m)^[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source)
 if not m:raise ValueError(name)
 e=m.end();depth=1
 while depth:depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[m.start():e]
def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--package',default='onyx');p.add_argument('--output',type=Path);a=p.parse_args()
 base=a.workspace/a.package/'drivers/power/supply/mca';source=(base/'mca_smart_charge/mca_smart_charge.c').read_text();header=(base/'include/mca/smartchg/smart_chg_class.h').read_text();header=re.sub(r'^#include.*\n','',header,flags=re.M)
 structs=source[source.index('struct smart_basp_record {'):source.index('static void smart_basp_free_decoded')]
 names=['smart_basp_free_decoded','smart_basp_decode','smart_basp_dispatch','smart_basp_transport_free','smart_basp_detach','smart_basp_file_free','smart_basp_vma_open','smart_basp_vma_close']
 common=(HERE/'tests/basp_common.h').read_text().replace('/* STAGED_HEADER */',header).replace('/* STAGED_STRUCTS */',structs).replace('/* STAGED_FUNCTIONS */','\n'.join(function(source,n) for n in names))
 results={}
 with tempfile.TemporaryDirectory(prefix='onyx-basp-tests-') as tmp:
  d=Path(tmp)
  for group in ['decode','transaction']:
   f=d/'test.c';f.write_text(common+'\n'+(HERE/('tests/basp_'+group+'.c')).read_text());out=d/'test'
   subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(f),'-o',str(out)],check=True)
   r=subprocess.run([str(out)],capture_output=True,text=True)
   if r.returncode:raise RuntimeError(group+'\n'+r.stdout+r.stderr)
   results[group]=json.loads(r.stdout)
 assert 'virt_to_phys' not in source and 'remap_pfn_range' not in source
 assert 'vm_insert_page' in function(source,'smart_charge_mmap')
 assert 'memdup_user(user, total)' in function(source,'smart_charge_ioctl')
 assert function(source,'smart_charge_remove').index('misc_deregister')<function(source,'smart_charge_remove').index('smart_basp_detach')
 assert function(source,'smart_charge_remove').index('smart_basp_detach')<function(source,'smart_charge_remove').index('smart_charge_destroy_voters')
 results['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];results['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_smart_charge/mca_smart_charge.c',base/'mca_smart_charge/inc/mca_smart_charge.h',base/'include/mca/smartchg/smart_chg_class.h']};results['limits']='Extracted production functions with mocked kernel allocation, locks, SRCU and VMA services; static producer ABI evidence only. No live producer execution, phone, boot, charging or real VMA concurrency validation.'
 text=json.dumps(results,indent=2)+'\n';print(text,end='')
 if a.output:a.output.write_text(text)
if __name__=='__main__':main()
