#!/usr/bin/env python3
"""Compile exact common/provider/manager helpers, writing only to this directory."""
from pathlib import Path
import argparse,re,hashlib,json,subprocess,resource
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--source-root',type=Path,default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/onyx'));parser.add_argument('--cc',default='/home/loukious/Android/konoha-android17-6.18-port-20261031/prebuilts/clang/host/linux-x86/clang-r596125/bin/clang');args=parser.parse_args()
def function(source,name):
 m=re.search(r'^(?:static )?[^\n]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 pos=m.end();depth=1
 while depth:depth+=(source[pos]=='{')-(source[pos]=='}');pos+=1
 return source[m.start():pos]+'\n'
def sha(text):return hashlib.sha256(text.encode()).hexdigest()
root=args.source_root/'qcom/opensource/audio-kernel';common=(root/'asoc/msm_common.c').read_text();provider=(root/'asoc/codecs/audio-ext-clk-up.c').read_text();before=(HERE/'before.c').read_text();candidate=(HERE/'candidate.c').read_text()
assert before.count('IS_ERR_OR_NULL(vote_handle) || !dev')==2
assert candidate.count('IS_ERR_OR_NULL(vote_handle) || !dev')==0
(HERE/'common.inc').write_text(function(common,'mi2s_tdm_hw_vote_req'))
enum=provider[provider.index('\nenum {')+1:provider.index('\n};',provider.index('\nenum {'))+3]
(HERE/'provider.inc').write_text(enum+'\n'+'\n'.join(function(provider,n) for n in ['lpass_hw_set_vote','lpass_hw_vote_prepare','lpass_hw_vote_unprepare']))
body=candidate[candidate.index('static DEFINE_MUTEX'):];baseline=before[before.index('static DEFINE_MUTEX'):]
mutations={
 'before_null_device_rejection':baseline,
 'enable_only_null_device_rejection':body.replace('if (IS_ERR_OR_NULL(vote_handle))','if (IS_ERR_OR_NULL(vote_handle) || !dev)',1),
 'disable_only_null_device_rejection':body.replace('if (IS_ERR_OR_NULL(vote_handle))\n\t\treturn;','if (IS_ERR_OR_NULL(vote_handle) || !dev)\n\t\treturn;',1),
 'bypassed_real_clock_enable':body.replace('ret = clk_prepare_enable(vote_handle);','ret = 0;'),
 'leaked_failed_first_request':body.replace('} else if (!vote->requested) {','} else if (false && !vote->requested) {'),
 'lost_manager_readiness':body.replace('if (!is_init_done) {','if (false && !is_init_done) {'),
}
assert all(v!=body for v in mutations.values())
results={}
for name,code in [('positive',body)]+list(mutations.items()):
 (HERE/'manager.inc').write_text(code);(HERE/(name+'.manager.inc')).write_text(code);binary=HERE/name
 compile=subprocess.run([args.cc,'-std=gnu11','-g','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',str(HERE/'harness.c'),'-o',str(binary)],capture_output=True,text=True);assert compile.returncode==0,compile.stderr
 run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
 assert (run.returncode==0)==(name=='positive'),(name,run.stdout,run.stderr)
 (HERE/(name+'.txt')).write_text(run.stdout+run.stderr)
 results[name]={'exit_code':run.returncode,'stdout':run.stdout.strip(),'stderr':run.stderr.strip()[:320],'manager_fragment_sha256':sha(code)}
 print(name,run.returncode,run.stdout.strip())
(HERE/'manager.inc').write_text(body)
report={'status':'PASS','checks':results,'source_sha256':{'common':sha(common),'provider':sha(provider),'before_manager':sha(before),'candidate_manager':sha(candidate),'harness':sha((HERE/'harness.c').read_text())},'functions':{'common':['mi2s_tdm_hw_vote_req'],'provider':['lpass_hw_set_vote','lpass_hw_vote_prepare','lpass_hw_vote_unprepare'],'manager':['hw_vote_find','digital_cdc_rsc_mgr_hw_vote_enable','digital_cdc_rsc_mgr_hw_vote_disable','digital_cdc_rsc_mgr_hw_vote_reset','digital_cdc_rsc_mgr_init','digital_cdc_rsc_mgr_exit']},'limits':'Exact production common caller, manager and provider callbacks; CCF refcounts, locking/allocation and firmware PRM are modeled. No hardware audio, firmware failure policy or concurrency verification. Shared source/recipe/device unchanged.'}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
