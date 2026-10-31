from pathlib import Path
import re,json,subprocess,tempfile,hashlib,argparse
H=Path(__file__).parent;SOURCE='qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c';CDC='qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/'
GROUPS={
 'dec':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_wake_enable','lpass_cdc_va_macro_queue_work','lpass_cdc_va_macro_cancel_work','is_msm_dmic_enabled','is_amic_enabled','lpass_cdc_va_macro_update_bits','lpass_cdc_va_macro_enable_dmic','lpass_cdc_va_macro_enable_dec','lpass_cdc_va_macro_tx_hpf_corner_freq_callback','lpass_cdc_va_macro_mute_update_callback','mute_stream_dec_unmute'],
 'dapm':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_core_vote','lpass_cdc_va_macro_mclk_enable','lpass_cdc_va_macro_mclk_event','lpass_cdc_va_macro_enable_tx'],
 'divs':['lpass_cdc_va_macro_update_clk_div_factor','lpass_cdc_va_macro_parse_divs'],
 'ssr':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_core_vote','lpass_cdc_va_macro_event_handler'],
 'component':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_wake_enable','lpass_cdc_va_macro_queue_work','lpass_cdc_va_macro_cancel_work','lpass_cdc_va_macro_init','lpass_cdc_va_macro_deinit'],
 'votes':['lpass_cdc_va_macro_core_vote','lpass_cdc_va_macro_mclk_enable'],
 'swrclk':['lpass_cdc_va_macro_core_vote','lpass_cdc_va_macro_mclk_enable','lpass_cdc_va_macro_tx_va_mclk_enable','lpass_cdc_va_macro_swrm_clock'],
 'work':['lpass_cdc_va_macro_wake_enable','lpass_cdc_va_macro_queue_work','lpass_cdc_va_macro_cancel_work'],
 'children':['lpass_cdc_va_macro_notify','lpass_cdc_va_macro_remove_children','lpass_cdc_va_macro_add_child_devices'],
 'micbias':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_enable_micbias'],
 'teardown':['lpass_cdc_va_macro_core_vote','lpass_cdc_va_macro_mclk_enable','lpass_cdc_va_macro_tx_va_mclk_enable','lpass_cdc_va_macro_release_clocks','lpass_cdc_va_macro_disable_pm','lpass_cdc_va_macro_remove'],
 'mixer':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_tx_mixer_put'],
 'rates':['lpass_cdc_va_macro_get_data','lpass_cdc_va_macro_hw_params','lpass_cdc_va_macro_get_channel_map','lpass_cdc_va_macro_update_clk_div_factor','lpass_cdc_va_macro_clk_div_get'],
}
MUTATIONS=json.loads((H/'audio_va_mutations.json').read_text())
def extract(s,name):
 m=re.search(r'^(?:(?:static|inline)\s+)*(?:[\w]+\s+|\*\s*)+\b'+re.escape(name)+r'\([^;{}]*\)\n\{',s,re.M);assert m,name
 return s[m.start():s.index('\n}',m.end())+2]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();p=a.workspace/a.source_dir/SOURCE;original=p.read_text();headers={n:a.workspace/'onyx'/(CDC+n) for n in ['lpass-cdc.h','lpass-cdc-registers.h']};results={}
 with tempfile.TemporaryDirectory(prefix='onyx-va-test-') as tmp:
  for key in MUTATIONS if a.negative else GROUPS:
   source=original;group=key
   if a.negative:
    before,after,group=MUTATIONS[key];assert before in source,key;source=source.replace(before,after)
   ctrl=(a.workspace/'onyx/qcom/opensource/audio-kernel/soc/swr-mstr-ctrl.h').read_text();ctrl=ctrl[ctrl.index('struct swr_ctrl_platform_data {'):ctrl.index('\n};',ctrl.index('struct swr_ctrl_platform_data {'))+3]
   pub=headers['lpass-cdc.h'].read_text();pub=pub[pub.index('enum {'):pub.index('\nenum {\n\tG_21_DB')]
   va=source[source.index('enum {'):source.index('static int lpass_cdc_va_macro_wake_enable(')]
   prefix=source[:source.index('enum {')];defines=[];keep=False
   for line in prefix.splitlines():
    keep=keep or line.startswith('#define ')
    if keep:defines.append(line)
    keep=keep and line.endswith('\\')
   constants=headers['lpass-cdc-registers.h'].read_text()+'\n'+'\n'.join(defines)+'\n'
   services=(H/'tests/audio_va_services.h').read_text().replace('/* PUBLIC_LAYOUT */',pub).replace('/* VA_LAYOUT */',va+'\n'+ctrl+'\n_Static_assert(sizeof(struct swr_ctrl_platform_data)==sizeof(struct lpass_cdc_va_macro_swr_ctrl_platform_data), "controller platform data size");\n'+''.join('_Static_assert(offsetof(struct swr_ctrl_platform_data, '+k+')==offsetof(struct lpass_cdc_va_macro_swr_ctrl_platform_data, '+v+'), "controller '+k+' offset");\n' for k,v in [('handle','handle'),('read','read'),('write','write'),('bulk_write','bulk_write'),('clk','clk'),('core_vote','core_vote'),('reg_irq','handle_irq')]))
   code=(H/f'tests/audio_va_{group}.c').read_text().replace('/* SERVICES */',constants+services).replace('/* SOURCE */','\n'.join(extract(source,n) for n in GROUPS[group]))
   path=Path(tmp)/'test.c';binary=path.with_suffix('');path.write_text(code)
   run=subprocess.run(['cc','-std=gnu11','-g','-O1','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if run.returncode:Path('/tmp/va-test-failed.c').write_text(code);raise AssertionError(group+'\n'+run.stderr)
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
   if a.negative:assert run.returncode,key;results[key]={'rejected':True,'diagnostic':run.stderr[:500]}
   else:
    if run.returncode:Path('/tmp/va-test-failed.c').write_text(code);raise AssertionError(group+'\n'+run.stderr)
    results.update(json.loads(run.stdout))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report={'tests':results,'source_sha256':{SOURCE:sha(p)},'header_sha256':{CDC+n:sha(p) for n,p in headers.items()},'controller_header_sha256':sha(a.workspace/'onyx/qcom/opensource/audio-kernel/soc/swr-mstr-ctrl.h'),'harness_sha256':{p.name:sha(p) for p in (H/'tests').glob('audio_va_*')},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Extracted production source/layouts and frozen CDC headers with mocked ALSA, regmap, clock, PM, work, regulator and platform services. No live kernel/audio/SSR validation.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results,indent=2))
if __name__=='__main__':main()
