#!/usr/bin/env python3
"""Exercise exact staged native controller functions against host kernel mocks."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
PREFIX='qcom/opensource/audio-kernel/'
FUNCTIONS=['swrm_get_clk_div','swrm_get_clk_div_rate','swrm_is_port_en','copy_port_tables','swrm_get_port_config','swrm_is_fractional_sample_rate','swrm_adjust_sample_rate','swrm_get_master_port','swrm_get_packed_reg_val','swrm_get_port_req','swrm_cleanup_disabled_port_reqs','swrm_connect_port','swrm_disconnect_port','swrm_init_port_params','swrm_set_port_config','swrm_cmd_fifo_rd_cmd','swrm_read','swrm_write','swrm_bulk_write','swrm_free','swrm_master_release','swrm_drop_platform','swrm_free_port_requests','swrm_stop','swrm_probe','swrm_remove','swrm_register_wake_irq','swrm_wcd_notify']
SCENARIOS=['ports','port_failures','configuration','io','fifo','packing','lifecycle','parent_lifecycle','probe_failures','drain','notification_drain']
MUTATIONS={
 'signed_fifo_shift':('((u32)(data) << 24)','((data) << 24)','packing'),
 'response_on_failed_request':('fail:\n\tfor (i = 0; i < portinfo->num_port; i++)','fail:\n\tswr_port_response(master, portinfo->tid);\n\tfor (i = 0; i < portinfo->num_port; i++)','port_failures'),
 'missing_config_usecase_guard':('port_cfg->uc >= SWR_UC_MAX','false','configuration'),
 'missing_frame_device_guard':('!uc_arr || !dev_num || dev_num > SWR_MAX_DEV_NUM ||','!uc_arr || !dev_num ||','configuration'),
 'read_only_first_byte':('for (i = 0; i < len; i++) {\n\t\tret = swrm_cmd_fifo_rd_cmd','for (i = 0; i < 1; i++) {\n\t\tret = swrm_cmd_fifo_rd_cmd','io'),
 'masked_fifo_error':('ret = -EIO;','ret = 0;','fifo'),
 'missing_bus_drain':('wait_event(swrm->master.call_wait,\n\t\t\t   !atomic_read(&swrm->master.active_calls));','(void)0;','drain'),
 'missing_notification_drain':('wait_event(swrm->notify_wait, !atomic_read(&swrm->notify_calls));','(void)0;','notification_drain'),
 'missing_master_release_wait':('wait_for_completion(&swrm->master_released);\n\t\tswrm->master_registered = false;','(void)0;\n\t\tswrm->master_registered = false;','lifecycle'),
 'leaked_second_irq':('if (swrm->wake_irq_registered) {','if (false) {','lifecycle'),
 'lost_probe_error':('ret = swrm->irq;','ret = 0;','probe_failures'),
}
def function(source,name):
 pattern=r'^(?:static )?(?:int|void|bool|u8|u32|u64|irqreturn_t|struct swr_port_info \*)\s*'+re.escape(name)+r'\([^;]*?\)\n\{'
 m=re.search(pattern,source,re.M)
 assert m,name
 i=m.end();depth=1
 while depth:
  if source[i]=='{':depth+=1
  elif source[i]=='}':depth-=1
  i+=1
 return source[m.start():i]
def clean(body):
 return '\n'.join(line for line in body.splitlines() if not re.match(r'\s*#include',line))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--output',type=Path,required=True);ap.add_argument('--negative',action='store_true');args=ap.parse_args();root=args.workspace/args.source_dir
 source=(root/PREFIX/'soc/swr-mstr-ctrl.c').read_text()
 def read(path):
  p=root/PREFIX/path
  if not p.exists():p=args.workspace/'onyx'/PREFIX/path
  return p.read_text()
 headers='\n'.join(clean(read(p)) for p in ['include/soc/audio_mod_devicetable.h','include/soc/soundwire.h','include/soc/swr-common.h','include/soc/swr-wcd.h','soc/swr-mstr-ctrl.h','soc/swr-mstr-registers.h','soc/swr-slave-registers.h'])
 results={}
 with tempfile.TemporaryDirectory(prefix='onyx-swrctrl-test-') as temporary:
  for key in (list(MUTATIONS) if args.negative else ['positive']):
   body=source
   if args.negative:
    before,after,scenario=MUTATIONS[key];assert before in body,key;body=body.replace(before,after);scenarios=[scenario]
   else:scenarios=SCENARIOS
   code=(HERE/'tests/swrctrl.c').read_text().replace('/* HEADERS */',headers).replace('/* CONSTANTS */',body[body.index('#define SWR_NUM_PORTS'):body.index('static bool swrm_lock_sleep')]).replace('/* SOURCE */','\n\n'.join(function(body,n) for n in FUNCTIONS))
   path=Path(temporary)/'swrctrl.c';binary=path.with_suffix('');path.write_text(code)
   build=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if build.returncode:
    Path('/tmp/swrctrl-test-failed.c').write_text(code)
    raise AssertionError(build.stdout+build.stderr)
   for scenario in scenarios:
    run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=20)
    if args.negative:
     assert run.returncode,key
     results[key]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR:' in l or 'runtime error:' in l),run.stderr[:200])}
    else:
     assert not run.returncode,scenario+': '+run.stdout+run.stderr
     results.update(json.loads(run.stdout))
 paths=json.loads((HERE/'audio-swrctrl-sources.json').read_text())
 report={'tests':results,'source_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'harness_sha256':{'swrctrl.c':hashlib.sha256((HERE/'tests/swrctrl.c').read_bytes()).hexdigest()},'extracted_production_functions':FUNCTIONS,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Exact staged functions and native headers with mocked device core, PM, IRQ, clocks, register transport and DT. Pthread waits test controller drain and retained master release. No physical bus, firmware, real IRQ/workqueue scheduler or audio validation.'}
 args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'tests':results},indent=2))
if __name__=='__main__':main()
