#!/usr/bin/env python3
"""Run extracted performance-provider helpers with ASan/UBSan and mock services."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
    source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
    match=re.search(r'(?m)^(?:static )?(?:inline )?(?:const )?(?:enum \w+|struct \w+|unsigned(?: int| long)?|[A-Za-z_]\w*)[ \t\n*]+(?:(?:__init|__exit)\s+)?\b'+re.escape(name)+r'\([^;]*?\)\n\{',source)
    if not match:raise ValueError(name)
    end=match.end();depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]+'\n'

def run(text,path):
    (path/'harness.c').write_text(text)
    subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Werror=implicit-function-declaration','-pthread',str(path/'harness.c'),'-o',str(path/'harness')],check=True)
    result=subprocess.run([str(path/'harness')],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    return json.loads(result.stdout)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args();root=a.workspace.resolve()/a.package
    perf=(root/'drivers/soc/qcom/msm_performance.c').read_text();sysstats=(root/'kernel/msm_sysstats.c').read_text();public=(root/'include/soc/qcom/msm_performance.h').read_text()
    types='#define QUEUE_POOL_SIZE 512\n#define INST_EV 8\n#define CYC_EV 17\n'
    types+='\n'.join(re.findall(r'enum \w+\s*\{[^}]*\};',perf+public))+'\n'
    for n in ['cpu_status','gpu_data','queue_indicies','event_data']:types+=re.search(r'struct '+n+r' \{.*?\n\};',perf,re.S)[0]+'\n'
    names=['freq_qos_request_init','msm_perf_parse_freq','msm_perf_set_freq','show_perf_gfx_evts','msm_perf_events_update','release_pmu_counters','init_pmu_counter','msm_perf_read_event','add_module_params','init_events_group','init_notify_group','msm_performance_cleanup','msm_performance_init']
    text=(HERE/'tests/perf_harness.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */','\n'.join(function(perf,n) for n in names))
    with tempfile.TemporaryDirectory(prefix='onyx-perf-') as tmp:
        result={'performance':run(text,Path(tmp))}
        names=['sysstats_kgsl_stats','sysstats_register_kgsl_stats_cb','sysstats_unregister_kgsl_stats_cb','get_task_unreclaimable_info','nla_strdup_cust']
        text=(HERE/'tests/sysstats_harness.c').read_text().replace('/* FUNCTIONS */','\n'.join(function(sysstats,n) for n in names));result['sysstats']=run(text,Path(tmp))
    assert 'GENL_DONT_VALIDATE_DUMP' not in sysstats
    assert '.policy\t\t= sysstats_cmd_get_policy' in sysstats
    assert 'genl_info_dump(cb)->attrs[SYSSTATS_TASK_CMD_ATTR_FOREACH]' in sysstats
    assert 'NSEC_PER_USEC' in function(sysstats,'sysstats_task_cmd_attr_pid')
    cleanup=function(perf,'msm_performance_cleanup')
    assert cleanup.index('complete_all')<cleanup.index('sysfs_remove_group')<cleanup.index('release_pmu_counters')
    assert cleanup.index('cpuhp_remove_state_nocalls')<cleanup.index('kthread_stop')<cleanup.index('sysfs_remove_group')
    assert cleanup.index('core_ctl_notifier_unregister')<cleanup.index('cancel_work_sync')<cleanup.index('sysfs_remove_group')
    entries=[e for e in json.loads((HERE/'imports.json').read_text()) if e['path'] in ['kernel/msm_sysstats.c','drivers/soc/qcom/msm_performance.c','include/linux/msm_sysstats.h','include/uapi/linux/msm_sysstats.h','include/soc/qcom/msm_performance.h']];result['source_sha256']={e['path']:hashlib.sha256((root/e['path']).read_bytes()).hexdigest() for e in entries};result['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'tests').glob('*.c') if p.name in ['perf_harness.c','sysstats_harness.c']};result['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];result['limits']='Extracted C helpers with mocked CPU/PMU/QoS/netlink/locking services. pthread models exercise ordering, not real kernel SRCU/IRQ/workqueue/CPU hotplug or device boot.'
    output=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(output)
    print(output,end='')
if __name__=='__main__':main()
