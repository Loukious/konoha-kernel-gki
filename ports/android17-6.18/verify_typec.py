#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
HERE=Path(__file__).parent;SOURCE='qcom/opensource/audio-kernel/asoc/codecs/typec_analog_acc.c';UCSI='include/linux/usb/ucsi_glink.h';SCENARIOS=['failures','lifecycle','events','drain','duplicate']
MUTATIONS={
 'lost_allocation_error':('if (!priv) {','if (false) {','failures'),
 'overwritten_active_instance':('if (acc_priv) {','if (false) {','lifecycle'),
 'missing_module_pin':('if (!try_module_get(THIS_MODULE)) {','if (false) {','lifecycle'),
 'missing_notifier_drain':('unregister_ucsi_glink_notifier(&priv->ucsi_nb);','(void)0;','drain'),
 'wrong_component_cleanup':('if (!priv || priv->component != component)','if (!priv)','lifecycle'),
 'lost_jack_cleanup':('snd_device_free(component->card->snd_card, priv->typec_analog_jack.jack);','(void)0;','failures'),
 'unserialized_events':('mutex_lock(&priv->jack_lock);','(void)0;','duplicate'),
 'missing_null_payload_check':('if (!ptr)','if (false)','events'),
 'wrong_native_report_mask':('#define JACK_TYPEC_ANALOG_MASK (SND_JACK_VIDEOOUT | SND_JACK_HEADSET)','#define JACK_TYPEC_ANALOG_MASK SND_JACK_HEADSET','events'),
 'notifier_before_jack':('ret = snd_soc_card_jack_new(component->card, "Typec_analog Jack",','register_ucsi_glink_notifier(&priv->ucsi_nb);\n\tret = snd_soc_card_jack_new(component->card, "Typec_analog Jack",','lifecycle'),
}
def clean(s):return '\n'.join(l for l in s.splitlines() if not re.match(r'\s*(#include|EXPORT_SYMBOL|MODULE_|module_init|module_exit)',l))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();root=a.workspace/a.source_dir;src=root/SOURCE;header=a.workspace/'onyx'/UCSI;body=src.read_text();results={}
 with tempfile.TemporaryDirectory(prefix='onyx-typec-test-') as temporary:
  for key in MUTATIONS if a.negative else ['positive']:
   current=body;scenarios=SCENARIOS
   if a.negative:
    before,after,scenario=MUTATIONS[key];assert before in current,key;current=current.replace(before,after);scenarios=[scenario]
   code=(HERE/'tests/audio_typec_typec.c').read_text().replace('/* UCSI_HEADER */',clean(header.read_text())).replace('/* SOURCE */',clean(current));path=Path(temporary)/'typec.c';binary=path.with_suffix('');path.write_text(code)
   run=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if run.returncode:Path('/tmp/typec-test-failed.c').write_text(code);raise AssertionError(run.stdout+run.stderr)
   for scenario in scenarios:
    run=subprocess.run([str(binary),scenario],capture_output=True,text=True,timeout=15)
    if a.negative:
     assert run.returncode,key;results[key]={'rejected':True,'returncode':run.returncode,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR:' in l or 'runtime error:' in l),run.stderr[:200])}
    else:
     if run.returncode:Path('/tmp/typec-test-failed.c').write_text(code);raise AssertionError(scenario+': '+run.stdout+run.stderr)
     results.update(json.loads(run.stdout))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report={'tests':results,'source_sha256':{p:sha(root/p) for p in json.loads((HERE/'audio-typec-sources.json').read_text())},'ucsi_header_sha256':{UCSI:sha(header)},'harness_sha256':{'audio_typec_typec.c':sha(HERE/'tests/audio_typec_typec.c')},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Full production accessory source and actual frozen UCSI header; module/ALSA card/jack/notifier/allocation services mocked; pthread report serialization and callback drain. No actual ALSA/card unregister, UCSI hardware, USB accessory or sound report delivery claimed.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'tests':results},indent=2))
if __name__=='__main__':main()
