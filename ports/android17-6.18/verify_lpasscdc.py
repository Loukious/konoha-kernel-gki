#!/usr/bin/env python3
from pathlib import Path
import re,json,subprocess,tempfile,hashlib,argparse
H=Path(__file__).parent;PREFIX='qcom/opensource/audio-kernel/asoc/codecs/lpass-cdc/'
GROUPS={
'compander':('lpass-cdc-comp.c',['lpass_cdc_load_compander_coeff','lpass_cdc_update_compander_setting']),
'proc':('lpass-cdc.c',['regdump_read']),
'lowio':('lpass-cdc.c',['lpass_cdc_ahb_write_device','lpass_cdc_ahb_read_device','__lpass_cdc_reg_read','__lpass_cdc_reg_write']),
'ports':('lpass-cdc.c',['lpass_cdc_is_valid_codec_dev','lpass_cdc_set_port_map_active','lpass_cdc_set_port_map']),
'clock':('lpass-cdc-clk-rsc.c',['lpass_cdc_clk_rsc_get_clk_muxsel','lpass_cdc_clk_get','lpass_cdc_clock_enable','lpass_cdc_clock_disable','lpass_cdc_rsc_clk_reset','lpass_cdc_clk_rsc_all_clocks','lpass_cdc_clock_request','lpass_cdc_clk_rsc_request_clock','lpass_cdc_fs_gen_request']),
'io':('lpass-cdc-utils.c',['lpass_cdc_get_macro_id','regmap_bus_read','regmap_bus_gather_write','regmap_bus_write']),
'pm':('lpass-cdc.c',['lpass_cdc_is_valid_child_dev','lpass_cdc_runtime_resume','lpass_cdc_runtime_suspend']),
'gate':('lpass-cdc.c',[]),
'children':('lpass-cdc.c',['lpass_cdc_remove_child_devices','lpass_cdc_add_child_devices']),
'macro':('lpass-cdc.c',['lpass_cdc_is_valid_child_dev','lpass_cdc_unregister_component','lpass_cdc_copy_dais_from_macro','lpass_cdc_register_macro','lpass_cdc_unregister_macro']),
'dmic':('lpass-cdc.c',['lpass_cdc_dmic_clk_div_get','lpass_cdc_dmic_clk_enable_active']),
'component':('lpass-cdc.c',['lpass_cdc_component_cleanup','lpass_cdc_soc_codec_probe','lpass_cdc_soc_codec_remove']),
'ssr':('lpass-cdc.c',['lpass_cdc_macro_event','lpass_cdc_ssr_down','lpass_cdc_ssr_enable_active','lpass_cdc_ssr_disable_active']),
}
MUTATIONS={
 'clock_fallback_dropped_early':('lpass-cdc-clk-rsc.c','if (switch_mux && last)','if (false)','clock'),
 'compander_masked_failure':('lpass-cdc-comp.c','if (ret)\n\t\t\treturn ret;','if (false)\n\t\t\treturn ret;','compander'),
 'compander_signed_overflow':('lpass-cdc-comp.c','lower_gain_int == INT_MIN ||','false ||','compander'),
 'proc_offset_on_copy_fault':('lpass-cdc.c','else\n\t\t*ppos = i - base_reg;','*ppos = i - base_reg;','proc'),
 'lowio_masked_resume_error':('lpass-cdc.c','if (ret < 0)\n\t\t\tgoto ssr_err;','if (false)\n\t\t\tgoto ssr_err;','lowio'),
 'lowio_missing_votes':('lpass-cdc.c','ret = -ENOTSYNC;','ret = 0;','lowio'),
 'port_map_invalid_id':('lpass-cdc.c','if (map[idx].id >= MAX_MACRO)','if (false)','ports'),
 'invalid_clock_id':('lpass-cdc-clk-rsc.c','id >= MAX_CLK || default_id < 0','id > MAX_CLK || default_id < 0','clock'),
 'clock_partial_commit':('lpass-cdc-clk-rsc.c','memcpy(requested, priv->request_count, sizeof(requested));','priv->request_count[id] += enable ? 1 : -1;\n memcpy(requested, priv->request_count, sizeof(requested));','clock'),
 'clock_missing_rollback':('lpass-cdc-clk-rsc.c','while (priv->owned[i] > before[i])','while (false)','clock'),
 'clock_steals_reference':('lpass-cdc-clk-rsc.c','while (priv->owned[clk_id])','while (priv->clk[clk_id]->refs)','clock'),
 'fs_count_on_error':('lpass-cdc-clk-rsc.c','goto rollback;\n\t\t}\n\t}\n\tpriv->reg_seq_en_cnt','priv->reg_seq_en_cnt++; goto rollback;\n\t\t}\n\t}\n\tpriv->reg_seq_en_cnt','clock'),
 'io_cross_macro':('lpass-cdc-utils.c','last = first + (val_size - 1) * 4;','last = first;','io'),
 'io_masked_error':('lpass-cdc-utils.c','return ret;','return 0;','io'),
 'pm_missing_unwind':('lpass-cdc.c','if (core)\n\t\tdigital_cdc_rsc_mgr_hw_vote_disable','if (false)\n\t\tdigital_cdc_rsc_mgr_hw_vote_disable','pm'),
 'pm_suspend_underflow':('lpass-cdc.c','if (!priv->core_clk_vote_count ||','if (false ||','pm'),
 'gate_missing_drain':('lpass-cdc.c','wait_event(priv->op_wait, !atomic_read(&priv->active_ops));','(void)0;','gate'),
 'component_missing_drain':('lpass-cdc.c','wait_event(priv->op_wait, !atomic_read(&priv->component_ops));','(void)0;','gate'),
 'children_borrow_node':('lpass-cdc.c','child->dev.of_node = of_node_get(node);','child->dev.of_node = node;','children'),
 'children_wrong_order':('lpass-cdc.c','priv->child_phase[i] != phase','priv->child_phase[i] != 2 - phase','children'),
 'macro_stale_dais':('lpass-cdc.c','priv->num_dais -= priv->macro_params[macro_id].num_dais;','priv->num_dais -= 0;','macro'),
 'macro_missing_unregister':('lpass-cdc.c','snd_soc_unregister_component(priv->dev);','(void)0;','macro'),
 'dmic_count_on_error':('lpass-cdc.c','\tif (ret) {\n\t\tif (regmap_update_bits','\tif (ret) {\n\t\t*count += enable ? 1 : -1;\n\t\tif (regmap_update_bits','dmic'),
 'component_missing_typec_exit':('lpass-cdc.c','typec_analog_acc_exit(component);','(void)0;','component'),
 'component_missing_snd_drain':('lpass-cdc.c','snd_event_client_deregister(priv->dev);','(void)0;','component'),
 'ssr_sync_error_ignored':('lpass-cdc.c','ret = regcache_sync(priv->regmap);','regcache_sync(priv->regmap); ret = 0;','ssr'),
}
def extract(s,name):
 m=re.search(r'^(?:(?:static|const)\s+)*(?:[\w]+\s+|\*\s*)+\b'+re.escape(name)+r'\([^;{}]*\)\n\{',s,re.M)
 assert m,name
 end=s.index('\n}',m.end())+2
 return s[m.start():end]
def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--source-dir',default='onyx');ap.add_argument('--negative',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();base=a.workspace/a.source_dir;sources={p: (base/p).read_text() for p in [pin['path'] for pin in json.loads((H/'audio-lpasscdc-imports.json').read_text())]};results={}
 with tempfile.TemporaryDirectory(prefix='onyx-cdc-tests-') as tmp:
  for key in MUTATIONS if a.negative else GROUPS:
   current=dict(sources);group=key
   if a.negative:
    file,before,after,group=MUTATIONS[key];path=PREFIX+file;assert before in current[path],key;current[path]=current[path].replace(before,after)
   core=current[PREFIX+'lpass-cdc.c'];pub=current[PREFIX+'lpass-cdc.h'];internal=current[PREFIX+'internal.h'];clock=current[PREFIX+'lpass-cdc-clk-rsc.c']
   layout=pub[pub.index('enum {'):pub.index('\nenum {\n\tG_21_DB')]
   private=internal[internal.index('struct wcd_ctrl_platform_data'):internal.index('int lpass_cdc_clk_rsc_all_clocks')]
   gates='\n'.join(extract(core,n) for n in ['lpass_cdc_op_get','lpass_cdc_op_put','lpass_cdc_quiesce','lpass_cdc_reopen','lpass_cdc_component_drain'])
   services=(H/'tests/audio_lpasscdc_services.h').read_text().replace('/* PUBLIC_LAYOUT */',layout).replace('/* PRIVATE_LAYOUT */',private).replace('/* GATES */',gates)
   constants='\n'.join(l for l in current[PREFIX+'lpass-cdc-registers.h'].splitlines() if l.startswith('#define '))
   source='\n'.join(extract(current[PREFIX+GROUPS[group][0]],n) for n in GROUPS[group][1])
   clocklayout=re.search(r'struct lpass_cdc_clk_rsc \{.*?\n\};',clock,re.S).group()
   code=(H/f'tests/audio_lpasscdc_{group}.c').read_text().replace('/* SERVICES */',services).replace('/* CONSTANTS */',constants).replace('/* CLOCK_LAYOUT */',clocklayout).replace('/* SOURCE */',source).replace('/* COMP_LAYOUT */',re.search(r'struct comp_coeff_val \{.*?struct lpass_cdc_comp_setting \{.*?\n\};',current[PREFIX+'lpass-cdc-comp.h'],re.S).group())
   path=Path(tmp)/'test.c';binary=path.with_suffix('');path.write_text(code)
   cc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(path),'-o',str(binary)],capture_output=True,text=True)
   if cc.returncode:Path('/tmp/lpasscdc-test-failed.c').write_text(code);raise AssertionError(group+'\n'+cc.stderr)
   run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=25)
   if a.negative:
    assert run.returncode,key;results[key]={'rejected':True,'diagnostic':next((l for l in run.stderr.splitlines() if 'Assertion' in l or 'ERROR:' in l or 'runtime error:' in l),run.stderr[:150])}
   else:
    if run.returncode:Path('/tmp/lpasscdc-test-failed.c').write_text(code);raise AssertionError(group+'\n'+run.stderr)
    results.update(json.loads(run.stdout))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report={'tests':results,'source_sha256':{p:sha(base/p) for p in sources},'harness_sha256':{p.name:sha(p) for p in (H/'tests').glob('audio_lpasscdc_*')},'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Actual extracted production functions and private/public layouts; pthread gates and mocked CCF, regmap, PM, ALSA, platform and event services. No live kernel, IRQ, audio hardware, SSR firmware or phone validation.'};a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(results,indent=2))
if __name__=='__main__':main()
