from pathlib import Path
import re,json,subprocess,tempfile,hashlib,argparse
H=Path(__file__).parent;SOURCE='qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-tx-macro.c';CDC='qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/';P='lpass_cdc_tx_macro_'
GROUPS={
 'enum':[P+'get_data',P+'update_bits',P+'put_dec_enum'],
 'dapm':[P+'get_data',P+'mclk_enable',P+'mclk_event'],
 'remove':[P+'close_headset',P+'remove'],
 'component':[P+'get_data',P+'wake_enable',P+'queue_work',P+'cancel_work',P+'init',P+'close_headset',P+'deinit'],
 'dec':[P+'get_data',P+'wake_enable',P+'queue_work',P+'cancel_work','is_msm_dmic_enabled','is_amic_enabled',P+'update_bits',P+'enable_dmic',P+'enable_dec',P+'tx_hpf_corner_freq_callback',P+'mute_update_callback'],
 'ssr':[P+'get_data',P+'event_handler'],
 'controls':[P+'get_data',P+'bcs_ch_put',P+'swr_dmic_put',P+'put_dec_gain_bypass',P+'set_bcs',P+'put_bcs_ch_sel'],
 'teardown':[P+'mclk_enable',P+'component_cleanup',P+'release_clocks',P+'disable_pm'],
 'work':[P+'wake_enable',P+'queue_work',P+'cancel_work'],
 'mixer':[P+'get_data',P+'tx_mixer_put'],
 'divs':[P+'update_clk_div_factor',P+'parse_divs'],
 'clock':[P+'mclk_enable'],
 'headset':['tx_macro_hs_unmute_dwork',P+'mute_hs',P+'close_headset'],
 'stream':[P+'get_data','is_msm_dmic_enabled','mute_stream_dec_unmute','lpass_cdc_tx_mute_stream'],
 'rates':[P+'get_data',P+'hw_params',P+'get_channel_map',P+'clk_div_get'],
}
MUTATIONS=json.loads((H/'audio_tx_mutations.json').read_text())
def extract(s,name):
 m=re.search(r'^(?:(?:static|inline)\s+)*(?:[\w]+\s+|\*\s*)+\b'+re.escape(name)+r'\([^;{}]*\)\n\{',s,re.M);assert m,name
 return s[m.start():s.index('\n}',m.end())+2]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();p=a.workspace/a.source_dir/SOURCE;original=p.read_text();headers={n:a.workspace/'onyx'/(CDC+n) for n in ['lpass-cdc.h','lpass-cdc-registers.h']};results={}
 with tempfile.TemporaryDirectory(prefix='onyx-tx-test-') as tmp:
  for key in MUTATIONS if a.negative else GROUPS:
   source=original;group=key
   if a.negative:
    before,after,group=MUTATIONS[key];assert before in source,key;source=source.replace(before,after)
   pub=headers['lpass-cdc.h'].read_text();pub=pub[pub.index('enum {'):pub.index('\nenum {\n\tG_21_DB')]
   layout=source[source.index('enum {'):source.index('static int lpass_cdc_tx_macro_wake_enable(')]
   prefix=source[:source.index('enum {')];defines=[];keep=False
   for line in prefix.splitlines():
    if line.startswith(('#if ', '#ifdef ', '#ifndef ', '#else', '#elif ', '#endif', '#undef ')):defines.append(line)
    keep=keep or line.startswith('#define ')
    if keep:defines.append(line)
    keep=keep and line.endswith('\\')
   constants=headers['lpass-cdc-registers.h'].read_text()+'\n'+'\n'.join(defines)+'\n'
   globals='static struct lpass_cdc_tx_macro_priv *g_lpass_cdc_tx_priv;\nstatic DEFINE_MUTEX(tx_global_lock);\n'
   services=(H/'tests/audio_tx_services.h').read_text().replace('/* PUBLIC_LAYOUT */',pub).replace('/* TX_LAYOUT */',layout+'\n'+globals+'\n'+source[source.index('static const struct lpass_cdc_tx_macro_reg_mask_val'):source.index('static int lpass_cdc_tx_macro_component_cleanup(')])
   code=(H/f'tests/audio_tx_{group}.c').read_text().replace('/* SERVICES */',constants+services).replace('/* SOURCE */','\n'.join(extract(source,n) for n in GROUPS[group]))
   path=Path(tmp)/'test.c';binary=path.with_suffix('');path.write_text(code)
   run=subprocess.run(['cc','-std=gnu11','-g','-O1','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if run.returncode:Path('/tmp/tx-test-failed.c').write_text(code);raise AssertionError(group+'\n'+run.stderr)
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
   if a.negative:assert run.returncode,key;results[key]={'rejected':True,'diagnostic':run.stderr[:500]}
   else:
    if run.returncode:Path('/tmp/tx-test-failed.c').write_text(code);raise AssertionError(group+'\n'+run.stderr)
    results.update(json.loads(run.stdout))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report={'tests':results,'source_sha256':{SOURCE:sha(p)},'header_sha256':{CDC+n:sha(p) for n,p in headers.items()},'harness_sha256':{p.name:sha(p) for p in (H/'tests').glob('audio_tx_*')},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Extracted production source/layouts and frozen CDC headers with mocked ALSA, regmap, clock, PM, work and platform services. No live kernel/audio/SSR validation.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results,indent=2))
if __name__=='__main__':main()
