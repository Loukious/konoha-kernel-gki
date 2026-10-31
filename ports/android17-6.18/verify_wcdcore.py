#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
HERE=Path(__file__).parent;PREFIX='qcom/opensource/audio-kernel/'
GROUPS={'pinctrl':('msm-cdc-pinctrl.c',['bounds','failures','states','wake','lifetime']), 'irq':('wcd-irq.c',['init_failure','cleanup','mapping']), 'supply':('msm-cdc-supply.c',['votes','errors','rollback','initialize','parse','dapm']), 'init':('wcd9xxx-core-init.c',['init'])}
MUTATIONS={
 'unbounded_pinctrl_arrays':('pinctrl','count <= 0 || count > MAX_GPIOS','count <= 0','bounds'),
 'unbounded_mask_shift':('pinctrl','gpio_data->chip_wakeup_maskbit[i] >= 32','false','bounds'),
 'missing_mmio_check':('pinctrl','if (!gpio_data->chip_wakeup_register[i])','if (false)','failures'),
 'leaked_lookup_reference':('pinctrl','put_device(&pdev->dev);','(void)0;','states'),
 'false_pinctrl_state':('pinctrl','if (!ret)\n\t\tgpio_data->state = false;','gpio_data->state = false;','states'),
 'undrained_pinctrl_api':('pinctrl','mutex_lock(&msm_cdc_pinctrl_lock);','(void)0;','lifetime'),
 'leaked_irq_mapping':('irq','irq_dispose_mapping(ctx->parent_irq);','(void)0;','cleanup'),
 'missing_irq_devm_cleanup':('irq','ret = devm_add_action_or_reset(irq_info->dev, wcd_irq_domain_release, ctx);','ret = 0;','cleanup'),
 'invalid_irq_enable':('irq','if (irq > 0)','if (true)','mapping'),
 'failed_enable_vote':('supply','else\n\t\t\t\tcdc_vreg[i].vote = true;','cdc_vreg[i].vote = true;','votes'),
 'masked_voltage_error':('supply','return i == num_supplies ? -EINVAL : rc;','return 0;','errors'),
 'lost_first_regulator_error':('supply','return first_error;','return rc;','errors'),
 'missing_supply_unwind':('supply','devres_release_group(dev, group);','(void)0;','initialize'),
 'leaked_supply_node':('supply','of_node_put(regulator_node);','(void)0;','parse'),
 'masked_dapm_error':('supply','rc = snd_soc_dapm_sync(','snd_soc_dapm_sync(','dapm'),
 'missing_init_unwind':('init','wcd9xxx_irq_drv_exit();','(void)0;','init'),
}
def clean(s):return '\n'.join(l for l in s.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)',l))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();root=a.workspace/a.source_dir;results={};paths=json.loads((HERE/'audio-wcdcore-sources.json').read_text());bodies={g:(root/PREFIX/'asoc/codecs'/info[0]).read_text() for g,info in GROUPS.items()};bodies['pinctrl']=bodies['pinctrl'].split('static const struct of_device_id msm_cdc_pinctrl_match[]')[0]
 with tempfile.TemporaryDirectory(prefix='onyx-wcdcore-test-') as temporary:
  for key in MUTATIONS if a.negative else GROUPS:
   group,before,after,scenario=MUTATIONS[key] if a.negative else (key,None,None,None);body=bodies[group]
   if a.negative:assert before in body,key;body=body.replace(before,after)
   header=''
   if group in ['irq','supply']:header=clean((root/PREFIX/'include/asoc'/('wcd-irq.h' if group=='irq' else 'msm-cdc-supply.h')).read_text())
   fixture=HERE/'tests'/('wcdcore_'+group+'.c');code=fixture.read_text().replace('/* SERVICES */',(HERE/'tests/wcdcore_services.h').read_text()).replace('/* HEADER */',header).replace('/* SOURCE */',clean(body));src=Path(temporary)/('wcdcore_'+group+'.c');binary=src.with_suffix('');src.write_text(code)
   run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(src),'-o',str(binary)],capture_output=True,text=True)
   if run.returncode:Path('/tmp/wcdcore-test-failed.c').write_text(code);raise AssertionError(run.stdout+run.stderr)
   for scenario in [scenario] if a.negative else GROUPS[group][1]:
    run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=15)
    if a.negative:
     assert run.returncode,key;results[key]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR:' in l or 'runtime error:' in l),run.stderr[:200])}
    else:
     if run.returncode:Path('/tmp/wcdcore-test-failed.c').write_text(code);raise AssertionError(group+'/'+scenario+': '+run.stdout+run.stderr)
     results.update(json.loads(run.stdout))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report={'tests':results,'source_sha256':{p:sha(root/p) for p in paths},'harness_sha256':{p.name:sha(p) for p in (HERE/'tests').glob('wcdcore_*')},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'coverage':'Complete four production C units except pinctrl driver registration tables; actual public IRQ/supply headers. Init framework calls mocked, including native core-v2 disabled alternative core/IRQ calls. Host DT/devres/regulator/IRQ/pinctrl/ASoC services mocked; pthread removal drain. No Linux driver-core, physical IRQ, regulator, audio or phone operation claimed.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'tests':results},indent=2))
if __name__=='__main__':main()
