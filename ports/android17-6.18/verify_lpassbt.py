#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
HERE=Path(__file__).parent;SOURCE='qcom/opensource/audio-kernel/asoc/codecs/lpass-bt-swr.c';CTRL='qcom/opensource/audio-kernel/soc/swr-mstr-ctrl.h';SCENARIOS=['probe_failures','children','votes','mclk','ssr','lifetime','optional','module_init']
MUTATIONS={
 'unbounded_children':('if (selected) {','if (false) {','children'),
 'borrowed_child_node':('selected = of_node_get(node);','selected = node;','children'),
 'missing_child_unregister':('platform_device_unregister(priv->swr_pdev);','(void)0;','children'),
 'missing_ssr_drain':('snd_event_client_deregister(priv->dev);','(void)0;','lifetime'),
 'early_parent_pm_teardown':('/* The controller drains callbacks while parent clocks/PM remain valid. */','pm_runtime_disable(priv->dev);','children'),
 'missing_failed_vote_unwind':('lpass_bt_swr_release_votes(priv);','(void)0;','votes'),
 'masked_runtime_resume_error':('mutex_unlock(&priv->vote_lock);\n\treturn ret;','mutex_unlock(&priv->vote_lock);\n\treturn 0;','votes'),
 'missing_pm_put':('pm_runtime_put_autosuspend(priv->dev);','(void)0;','mclk'),
 'leaked_first_mclk':('clk_disable_unprepare(priv->clk_handle);','(void)0;','mclk'),
 'masked_ssr_restore_error':('if (ret)\n\t\tgoto unlock;\n\tWRITE_ONCE(priv->dev_up, true);','WRITE_ONCE(priv->dev_up, true);','ssr'),
 'masked_module_init_error':('return lpass_bt_swr_drv_init();','lpass_bt_swr_drv_init();\n\treturn 0;','module_init'),
}
def clean(s):return '\n'.join(l for l in s.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)',l))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();src=a.workspace/a.source_dir/SOURCE;header=a.workspace/'onyx'/CTRL;body=src.read_text();layout=re.search(r'struct swr_ctrl_platform_data \{.*?\n\};',header.read_text(),re.S).group();results={}
 with tempfile.TemporaryDirectory(prefix='onyx-lpassbt-test-') as temporary:
  for key in MUTATIONS if a.negative else ['positive']:
   current=body;scenarios=SCENARIOS
   if a.negative:
    before,after,scenario=MUTATIONS[key];assert before in current,key;current=current.replace(before,after);scenarios=[scenario]
   code=(HERE/'tests/lpassbt_lpassbt.c').read_text().replace('/* SERVICES */',(HERE/'tests/lpassbt_services.h').read_text()).replace('/* CONTROLLER_LAYOUT */',layout).replace('/* SOURCE */',clean(current));path=Path(temporary)/'lpassbt.c';binary=path.with_suffix('');path.write_text(code)
   run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if run.returncode:Path('/tmp/lpassbt-test-failed.c').write_text(code);raise AssertionError(run.stdout+run.stderr)
   for scenario in scenarios:
    run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=15)
    if a.negative:
     assert run.returncode,key;results[key]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR:' in l or 'runtime error:' in l),run.stderr[:200])}
    else:
     if run.returncode:Path('/tmp/lpassbt-test-failed.c').write_text(code);raise AssertionError(scenario+': '+run.stdout+run.stderr)
     results.update(json.loads(run.stdout))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report={'tests':results,'source_sha256':{SOURCE:sha(src)},'controller_header_sha256':{CTRL:sha(header)},'harness_sha256':{p.name:sha(p) for p in (HERE/'tests').glob('lpassbt_*')},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full production parent source and actual frozen controller platform-data structure; DT/devres/PM/clock/pinctrl/platform/SSR event services mocked; pthread callback drain. No actual child driver probe, Linux runtime PM or snd_event framework scheduling, IRQ/SSR hardware, or Bluetooth audio operation claimed.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'tests':results},indent=2))
if __name__=='__main__':main()
