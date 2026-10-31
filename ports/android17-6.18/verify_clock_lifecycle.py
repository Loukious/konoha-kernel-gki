from pathlib import Path
import argparse,subprocess,tempfile,hashlib,json,re,resource
ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);ap.add_argument('--negative-controls',action='store_true');a=ap.parse_args()
h=Path(__file__).resolve().parent;s=a.workspace/a.package/'drivers/clk/qcom'
dfs=(s/'clk-rcg2.c').read_text();debug=(s/'clk-debug.c').read_text();c=(s/'common.c').read_text();v=(s/'vdd-class.c').read_text();r=(s/'clk-regmap.c').read_text()
# Extract complete declaration lines, including multiline return signatures.
def function(text,name):
 match=re.search(r"^(?:static )?(?:int|void|bool)\s+"+re.escape(name)+r"\(",text,re.M)
 assert match,name
 start=match.start();brace=text.index('{',match.end());depth=1;i=brace+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+'\n'
owner=c[c.index('/*\n * This private devres owner'):c.index('\nint qcom_clk_crm_init(')]
vdd='static DEFINE_MUTEX(vdd_lock);\n'+''.join(function(v,n) for n in ['clk_aggregate_vdd','clk_vote_vdd_class_level','clk_unvote_vdd_class_level','clk_vote_vdd_level','clk_unvote_vdd_level','clk_regulator_init','clk_regulator_deinit'])
regmap='static LIST_HEAD(clk_regmap_list);\nstatic DEFINE_SPINLOCK(clk_regmap_lock);\n'+r[r.index('struct clk_regmap_saved {'):r.index('\nvoid clk_restore_critical_clocks(')]
registration=function(c,'qcom_cc_really_probe')+function(c,'qcom_cc_sync_state')
runtime=c[c.index('int qcom_cc_runtime_init('):c.index('\nstatic void __exit qcom_clk_exit(')]
debug_decl=debug[debug.index('struct hw_debug_clk {'):debug.index('static DEFINE_SPINLOCK(clk_reg_lock);')]+'static DEFINE_MUTEX(clk_debug_lock);\nDEFINE_STATIC_SRCU(clk_debug_srcu);\nstatic LIST_HEAD(clk_hw_debug_list);\n'
debug_code=''.join(function(debug,n) for n in ['clock_debug_print_enabled_clocks','clk_hw_debug_release','clk_debug_unregister','clk_hw_debug_register'])
assert '.terminate = clk_rcg2_dfs_terminate,' in dfs
dfs_code=''.join(function(dfs,n) for n in ['clk_rcg2_dfs_populate_freq_table','clk_rcg2_dfs_terminate'])+'static const struct clk_ops clk_rcg2_dfs_ops = {.terminate=clk_rcg2_dfs_terminate};\n'+''.join(function(dfs,n) for n in ['clk_rcg2_enable_dfs','qcom_cc_register_rcg_dfs'])+dfs[dfs.index('struct clk_rcg_dfs_saved {'):dfs.index('\nstatic int clk_rcg2_dp_set_rate(')]
leaf_names={'cambistmclkcc-tuna':'cam_bist_mclk_cc_tuna','camcc-tuna':'cam_cc_tuna','dispcc-tuna':'disp_cc_tuna','evacc-tuna':'eva_cc_tuna','videocc-tuna':'video_cc_tuna'}
leaf_bodies=[function((s/(name+'.c')).read_text(),prefix+'_probe') for name,prefix in leaf_names.items()]
leaf_globals=sorted(set(re.findall(r'&_?([A-Za-z0-9_]+)', ''.join(line for body in leaf_bodies for line in body.splitlines() if '_pll_configure(' in line))))
leaf_code=re.search(r'^#define DISP_CC_MISC_CMD.*$',(s/'dispcc-tuna.c').read_text(),re.M).group()+'\n'+''.join('static int '+name+';\n' for name in leaf_globals)+''.join('static struct qcom_cc_desc '+prefix+'_desc;\n' for prefix in leaf_names.values())+''.join(leaf_bodies)
controls=[]
with tempfile.TemporaryDirectory(prefix='clock-lifecycle-tests-') as td:
 d=Path(td)
 for n,t in [('vdd.inc',vdd),('owner.inc',owner),('regmap.inc',regmap),('registration.inc',registration),('runtime.inc',runtime),('debug_decl.inc',debug_decl),('debug.inc',debug_code),('dfs.inc',dfs_code),('leaves.inc',leaf_code)]: (d/n).write_text(t)
 command=['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-sign-compare','-Wno-unused-value','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie','-I',str(d),str(h/'tests/clock_provider_lifetime.c'),'-o',str(d/'provider')]
 subprocess.run(command,check=True);subprocess.run([str(d/'provider')],check=True)
 debug_command=command.copy();debug_command[debug_command.index(str(h/'tests/clock_provider_lifetime.c'))]=str(h/'tests/clock_debug_lifetime.c');debug_command[-1]=str(d/'debug');debug_command.insert(1,'-pthread');subprocess.run(debug_command,check=True);subprocess.run([str(d/'debug')],check=True)
 if a.negative_controls:
  variants=[
   ('detach_owner_lookup','owner.inc','if (life->dev == dev) {','if (life->dev == dev && dev->nr) {',command),
   ('lost_init_retry','regmap.inc','rclk->hw.init = saved->init;','(void)saved->init;',command),
   ('lost_regmap_membership','regmap.inc','list_del_init(&rclk->list_node);','(void)rclk;',command),
   ('lost_probe_reference','owner.inc','pm_runtime_put_noidle(dev);','(void)dev;',command),
   ('lost_resume_rollback','runtime.inc','if (ret && qcom_cc_runtime_resources_put(life))','if (false)',command),
   ('lost_rollback_poison','vdd.inc','vdd_class->cur_level = -1;','vdd_class->cur_level = 0;',command),
   ('unsafe_exposed_restore','owner.inc','if (!READ_ONCE(life->exposed))','if (true)',command),
   ('lost_nested_parent_pm_put','regmap.inc','pm_runtime_put_sync(parent_dev);','(void)parent_dev;',command),
   ('ignored_leaf_write_error','leaves.inc','ret = regmap_update_bits(regmap, 0x40f8, BIT(0), BIT(0));','regmap_update_bits(regmap, 0x40f8, BIT(0), BIT(0)); ret = 0;',command),
   ('lost_dfs_template_restore','dfs.inc','*saved->data->init = saved->init;','(void)saved;',command),
   ('lost_dfs_table_rollback','dfs.inc','kfree(freq_tbl);','(void)freq_tbl;',command),
   ('lost_dfs_terminate','dfs.inc','kfree(rcg->freq_tbl);','(void)rcg;',command),
   ('lost_debug_reader_drain','debug.inc','synchronize_srcu(&clk_debug_srcu);','(void)0;',debug_command),
  ]
  for name,fragment,old,new,compile_command in variants:
   file=d/fragment;original=file.read_text();assert old in original,name;file.write_text(original.replace(old,new,1))
   subprocess.run(compile_command,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   result=subprocess.run([compile_command[-1]],stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
   file.write_text(original)
   assert result.returncode!=0,'Negative control survived: '+name
   controls.append({'name':name,'rejected':True,'returncode':result.returncode})
   print('REJECTED',name)

changed=json.loads((h/'clock-lifecycle-sources.json').read_text())
source_hashes={name:hashlib.sha256((a.workspace/a.package/name).read_bytes()).hexdigest() for name in changed}
contracts={}
expected=json.loads((h/'clock-lifecycle-contracts.json').read_text())
for name in ['qcom_cc_desc','clk_regmap','clk_vdd_class','clk_vdd_class_data','clk_rcg2']:
 filename={'qcom_cc_desc':'common.h','clk_regmap':'clk-regmap.h','clk_vdd_class':'vdd-class.h','clk_vdd_class_data':'vdd-class.h','clk_rcg2':'clk-rcg.h'}[name]
 new=(s/filename).read_text();pattern=r'struct '+name+r' \{.*?\n\};'
 assert expected['layouts'][name]['definition']==re.search(pattern,new,re.S).group(),name
 contracts['layout_'+name]='unchanged'
for name,prefix in leaf_names.items():
 new=(s/(name+'.c')).read_text()
 for key,pattern in [('pll_calls',r'clk_\w+_pll_configure\([^;]+\)'),('register_writes',r'regmap_update_bits\([^;]+\)')]:
  assert expected['leaves'][name][key]==re.findall(pattern,new),name
 assert 'qcom_cc_runtime_acquire(pdev,' in new and 'qcom_cc_runtime_release(pdev)' in new
 assert 'pm_runtime_get_sync(&pdev->dev)' not in function(new,prefix+'_probe')
 contracts[name]='native PLL calls and register writes/order preserved; owned PM calls'
for name in ['gcc-tuna','gpucc-tuna','tcsrcc-tuna']+list(leaf_names):
 new=(s/(name+'.c')).read_text();assert new.count('.suppress_bind_attrs = true,')==new.count('.of_match_table ='),name
contracts['fixed_controller_manual_bind_attributes']='suppressed on all nine platform drivers'
assert hashlib.sha256((s/'clk-opp.c').read_bytes()).hexdigest()==expected['clk_opp_sha256']
quarantine=re.search(r'struct qcom_cc_quarantine \{.*?\n\};',c,re.S).group()
assert not any(token in quarantine for token in ['desc','clk_hw','module','work_struct','(*'])
contracts['quarantine']='data-only device/framework handles and error'
exit_body=function(debug,'clk_debug_exit');assert re.search(r'if \(debug_suspend\)\n\t\tunregister_trace_suspend_resume\(.*?;\n\t/\*.*?\*/\n\ttracepoint_synchronize_unregister\(\);',exit_body,re.S)
contracts['module_exit_trace_drain']='unconditional after optional unregister'

report={'status':'PASS','negative_controls':controls,'source_sha256':source_hashes,'harness_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted([h/'tests/clock_provider_lifetime.c', h/'tests/clock_debug_lifetime.c'])},'contracts':contracts,'scope':'exact selected provider/DFS/leaf/debug functions with userspace dependency fakes; not hardware/kernel runtime validation'}
if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
