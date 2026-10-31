#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def function(s,n):
 m=re.search(r'(?m)^static (?:int|void) (?:__init |__exit )?'+n+r'\([^;]*?\)\n\{',s)
 assert m,n
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+'\n'

def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=a.workspace/a.source_dir
 sources={'qcom':(root/'drivers/crypto/qcom-rng.c').read_text(),'msm':(root/'qcom/opensource/securemsm-kernel/qrng/msm_rng.c').read_text()}
 generated={}
 for name,s in sources.items():
  s=s[:s.index('static const struct acpi_device_id' if name=='qcom' else 'static const struct of_device_id')]
  s=re.sub(r'^#include.*\n','',s,flags=re.M)
  cases=(H/'tests'/(name+'_cases.c')).read_text()
  if name=='msm':
   module=(H/'tests/msm_module.c').read_text().replace('/* PRODUCTION MODULE FUNCTIONS */',function(sources[name],'msm_rng_init')+'\n'+function(sources[name],'msm_rng_exit'))
   cases=cases.replace('int main(void)',module+'\nint main(void)').replace('buffers();lifecycle();races();','buffers();lifecycle();races();module_cases();')
  generated[name]=(H/'tests/rng_fake.h').read_text()+'\n'+s+'\n'+cases
 report={'tests':{}}
 with tempfile.TemporaryDirectory(prefix='rng-test-') as d:
  tmp=Path(d)
  def execute(name,src,negative=False):
   f=tmp/(name+'.c');f.write_text(src);exe=f.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-label','-Wno-missing-field-initializers','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie',str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40)
   if negative:
    assert r.returncode,(name,'mutation survived');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for name,src in generated.items():report['tests'][name]=execute(name,src)
  if a.negative:
   mutations={
    'qcom_stopped_context_reads_hardware':('qcom','if (rng->stopping) {','if (false) {'),
    'qcom_zero_length_reads_register':('qcom','while (currsize < max) {','do {'),
    'qcom_wait_ignored':('qcom','if (!wait) {','if (false) {'),
    'qcom_tail_overwrites':('qcom','memcpy(data, &val, max - currsize);','memcpy(data, &val, WORD_SZ);'),
    'qcom_native_zero_rejection_lost':('qcom','if (!val)\n\t\t\treturn -EINVAL;','if (false)\n\t\t\treturn -EINVAL;'),
    'qcom_read_clock_leak':('qcom','clk_disable_unprepare(rng->clk);','(void)0;'),
    'qcom_clock_failure_reads':('qcom','ret = clk_prepare_enable(rng->clk);\n\tif (ret)\n\t\tgoto out;','ret = clk_prepare_enable(rng->clk);\n\tif (false)\n\t\tgoto out;'),
    'qcom_alg_retention_lost':('qcom','kref_get(&rng->ref);','(void)0;'),
    'qcom_double_destroy':('qcom','if (atomic_xchg(&rng->alg_ref, 0))','if (true)'),
    'qcom_init_removal_gate_lost':('qcom','if (rng->stopping)\n\t\tret = -ENODEV;','if (false)\n\t\tret = -ENODEV;'),
    'qcom_hwrng_unregister_lost':('qcom','if (rng->of_data->hwrng_support)\n\t\thwrng_unregister(&rng->hwrng);','if (false)\n\t\thwrng_unregister(&rng->hwrng);'),
    'qcom_stop_drain_lost':('qcom','mutex_lock(&rng->lock);\n\trng->stopping = true;\n\tmutex_unlock(&rng->lock);','rng->stopping = true;'),
    'qcom_large_crypto_length':('qcom','if (dlen > INT_MAX)','if (false)'),
    'msm_suspend_gate_lost':('msm','if (rng->suspended) {','if (false) {'),
    'msm_unaligned_word_write':('msm','memcpy(out + currsize, &val, sizeof(val));','*(u32 *)(out + currsize) = val;'),
    'msm_partial_word_overrun':('msm','while (max - currsize >= sizeof(val)) {','while (max > currsize) {'),
    'msm_wait_ignored':('msm','if (!wait || failed++ == RETRY_MAX_CNT)','if (failed++ == RETRY_MAX_CNT)'),
    'msm_retry_budget_reset':('msm','currsize += sizeof(val);','currsize += sizeof(val); failed = 0;'),
    'msm_clock_failure_vote_leak':('msm','if (ret)\n\t\tgoto drop_vote;','if (ret)\n\t\tgoto unlock;'),
    'msm_vote_failure_hidden':('msm','return ret ? ret : currsize;','return currsize;'),
    'msm_stale_ioctl_gate_lost':('msm','if (rng->stopping)\n\t\tret = -ENODEV;','if (false)\n\t\tret = -ENODEV;'),
    'msm_stale_open_rebind':('msm','if (msm_rng_ready && msm_rng_dev_cached == rng) {','if (msm_rng_ready && msm_rng_dev_cached) {'),
    'msm_open_reference_lost':('msm','get_device(&rng->chardev);','(void)0;'),
    'msm_open_before_module_commit':('msm','if (msm_rng_ready && msm_rng_dev_cached == rng) {','if (msm_rng_dev_cached == rng) {'),
    'msm_module_owner_lost':('msm','\t.owner = THIS_MODULE,','\t.owner = NULL,'),
    'msm_probe_icc_handle_leak':('msm','put_icc:\n\ticc_put(rng->icc_path);','put_icc:\n\t(void)0;'),
    'msm_remove_registry_lost':('msm','msm_rng_dev_cached = NULL;','(void)0;'),
    'msm_duplicate_controller_allowed':('msm','if (msm_rng_dev_cached) {','if (false) {'),
    'msm_short_crypto_result_accepted':('msm','(ret >= 0 && ret != dlen)','false'),
    'msm_module_init_unwind_lost':('msm','platform_driver_unregister(&rng_driver);','(void)0;'),
    'msm_large_crypto_length':('msm','if (dlen > INT_MAX)','if (false)'),
   }
   report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    src=generated[group];assert before in src
    if name=='qcom_zero_length_reads_register':
     before='\twhile (currsize < max) {';after='\tdo {';src=src.replace('\n\t}\n\n\treturn currsize;','\n\t} while (currsize < max);\n\n\treturn currsize;',1)
    report['negative_controls'][name]=execute(name,src.replace(before,after,1),True)
 pins=json.loads((H/'rng-imports.json').read_text());report['source_sha256']={x['path']:hashlib.sha256((root/x['path']).read_bytes()).hexdigest() for x in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('*'))};report['limits']='Extracted production source; mocked crypto/device/VFS reference accounting, register I/O, clocks, ICC and pthread mutexes. No entropy-quality, real device-core, PM or hardware validation.';a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
