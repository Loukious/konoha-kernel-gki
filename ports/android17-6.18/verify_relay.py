#!/usr/bin/env python3
"""Test the complete pinned relay implementation with host kernel services."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
SCENARIOS=['module_init','allocation_failures','cpu_prepare','global','file_mmap','ring','publish_open','hotplug_order','read_poll','partial_open_failure']
MUTATIONS={
 'wrong_cpu_failure_slot':('buf->cpu = cpu;','buf->cpu = 0;','cpu_prepare'),
 'legacy_failure_slot_clear':('if (*per_cpu_ptr(chan->buf, buf->cpu) == buf)\n\t\t*per_cpu_ptr(chan->buf, buf->cpu) = NULL;', '*per_cpu_ptr(chan->buf, 0) = NULL;', 'cpu_prepare'),
 'missing_hotplug_lock':('cpus_read_lock();','(void)0;','hotplug_order'),
 'masked_cpuhp_init_error':('return relay_hp_state < 0 ? relay_hp_state : 0;','return 0;','module_init'),
 'missing_cpuhp_remove':('cpuhp_remove_state(relay_hp_state);','(void)0;','module_init'),
 'missing_file_module_owner':('.owner\t\t= THIS_MODULE,','.owner\t\t= NULL,','file_mmap'),
 'missing_channel_module_ref':('if (!try_module_get(THIS_MODULE))','if (false)','file_mmap'),
 'missing_irq_work_drain':('irq_work_sync(&buf->wakeup_work);','(void)0;','ring'),
 'lost_early_reader_ref':('__relay_reset(buf);\n\n \tif(chan->is_global)', 'kref_init(&buf->kref);\n\t__relay_reset(buf);\n\n \tif(chan->is_global)', 'publish_open'),
 'unchecked_fault_offset':('if (pgoff >= buf->chan->alloc_size >> PAGE_SHIFT)', 'if (false)', 'file_mmap'),
 'unchecked_mmap_offset':('if (vma->vm_pgoff || length != (unsigned long)buf->chan->alloc_size)','if (length != (unsigned long)buf->chan->alloc_size)','file_mmap'),
}
def clean(body):
 return '\n'.join(l for l in body.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)',l))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();src=args.workspace/args.source_dir/'kernel/relay.c';header=args.workspace/'common/include/linux/relay.h';body=src.read_text();results={}
 with tempfile.TemporaryDirectory(prefix='onyx-relay-test-') as temporary:
  for key in (MUTATIONS if args.negative else ['positive']):
   current=body;scenarios=SCENARIOS
   if args.negative:
    before,after,scenario=MUTATIONS[key];assert before in current,key;current=current.replace(before,after);scenarios=[scenario]
   code=(HERE/'tests/relay.c').read_text().replace('/* HEADER */',clean(header.read_text())).replace('/* SOURCE */',clean(current));path=Path(temporary)/'relay.c';binary=path.with_suffix('');path.write_text(code)
   run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if run.returncode:
    Path('/tmp/relay-test-failed.c').write_text(code);raise AssertionError(run.stdout+run.stderr)
   for scenario in scenarios:
    run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=15)
    if args.negative:
     assert run.returncode,key
     results[key]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR:' in l or 'runtime error:' in l),run.stderr[:200])}
    else:
     assert not run.returncode,scenario+': '+run.stdout+run.stderr
     results.update(json.loads(run.stdout))
 report={'tests':results,'source_sha256':{'kernel/relay.c':hashlib.sha256(src.read_bytes()).hexdigest()},'kernel_header_sha256':{'include/linux/relay.h':hashlib.sha256(header.read_bytes()).hexdigest()},'harness_sha256':{'relay.c':hashlib.sha256((HERE/'tests/relay.c').read_bytes()).hexdigest()},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full production relay source and current kernel relay.h; allocation/page mapping/VFS module-file-VMA references/IRQ work/CPU-hotplug services mocked. Pthread synchronization tests CPU preparation serialization. No actual kernel loading, hotplug, mmap or WLAN transport run.'};args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'tests':results},indent=2))
if __name__=='__main__':main()
