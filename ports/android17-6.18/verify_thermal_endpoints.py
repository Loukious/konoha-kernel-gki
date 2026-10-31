#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=a.workspace/a.source_dir;D=root/'drivers/thermal/qcom'
 def source(name):
  s=(D/name).read_text();s=s[:s.index('static const struct of_device_id')];return re.sub(r'^#include.*\n','',s,flags=re.M)
 def protocol(name):
  s=re.sub(r'^#include.*\n','',(D/name).read_text(),flags=re.M);names=re.findall(r'extern struct qmi_elem_info\s+(\w+)\[\];',s);return s+'\n'+'\n'.join('struct qmi_elem_info '+n+'[1];' for n in names)+'\n'
 common=(H/'tests/thermal_endpoints_thermal_fake.h').read_text();generated={}
 generated['frequency']=common+'\n'+''.join(source(n) for n in ['qti_cpufreq_cdev.c','qti_devfreq_cdev.c','qti_userspace_cdev.c'])+(H/'tests/thermal_endpoints_frequency_cases.c').read_text()
 generated['cooling']=common+'\n'+protocol('thermal_mitigation_device_service_v01.h')+source('qmi_cooling.c')+(H/'tests/thermal_endpoints_cooling_cases.c').read_text()
 generated['sensor']=common+'\n'+protocol('thermal_sensor_service_v02.h')+(D/'qmi_sensors.h').read_text()+source('qmi_sensors_v2.c')+(H/'tests/thermal_endpoints_sensor_cases.c').read_text()
 report={'tests':{}}
 with tempfile.TemporaryDirectory(prefix='thermal-endpoints-test-') as d:
  tmp=Path(d)
  def execute(name,src,negative=False):
   f=tmp/(name+'.c');f.write_text(src);exe=f.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-but-set-variable','-Wno-unused-label','-Wno-missing-field-initializers','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie',str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40)
   if negative:
    assert r.returncode,(name,'mutation survived');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for name,src in generated.items():report['tests'][name]=execute(name,src)
  if a.negative:
   mutations={
    'cpu_sparse_entries_ignored':('frequency','data->freq_table[i++] = entry->frequency;','data->freq_table[i] = policy->freq_table[i].frequency; i++;'),
    'cpu_table_ascending':('frequency','return (first < second) - (first > second);','return (first > second) - (first < second);'),
    'cpu_qos_add_error_ignored':('frequency','if (ret < 0)\n\t\tgoto free_table;','if (false)\n\t\tgoto free_table;'),
    'cpu_qos_update_error_hidden':('frequency','if (ret < 0) {','if (false) {'),
    'cpu_deferred_work_not_drained':('frequency','cancel_delayed_work_sync(&data->register_work);','(void)0;'),
    'cpu_policy_reference_leak':('frequency','if (data->policy)\n\t\t\tcpufreq_cpu_put(data->policy);','if (false)\n\t\t\tcpufreq_cpu_put(data->policy);'),
    'cpu_registry_not_withdrawn':('frequency','cpufreq_controller = NULL;','(void)0;'),
    'gpu_opp_count_zero_allowed':('frequency','count = dev_pm_opp_get_opp_count(data->dev);\n\tif (count <= 0)','count = dev_pm_opp_get_opp_count(data->dev);\n\tif (count < 0)'),
    'gpu_qos_frequency_truncation':('frequency','if (!freq || khz > S32_MAX)','if (!freq)'),
    'gpu_node_reference_leak':('frequency','of_node_put(data->np);','(void)0;'),
    'userspace_registration_error_hidden':('frequency','ret = PTR_ERR(cdev_instances[idx].cdev);','ret = 0;'),
    'userspace_borrowed_root_put':('frequency','probe_error:\n\tof_node_put(subsys_np);','probe_error:\n\tof_node_put(np);\n\tof_node_put(subsys_np);'),
    'userspace_level_bounds_lost':('frequency','if (state > usr_cdev->max_level)','if (false)'),
    'cooling_stale_generation_accepted':('cooling','generation != tmd->generation','false'),
    'cooling_remote_error_positive':('cooling','return -EREMOTEIO;','return result->result;'),
    'cooling_failed_state_not_retried':('cooling','state != data->mtgn_state || data->pending','state != data->mtgn_state'),
    'cooling_desired_state_discarded':('cooling','data->mtgn_state = state;','(void)0;'),
    'cooling_reconnect_limit_not_clamped':('cooling','data->mtgn_state = min(data->mtgn_state, data->max_level);','(void)0;'),
    'cooling_core_max_not_updated':('cooling','thermal_cooling_device_update(data->cdev);','(void)0;'),
    'cooling_primary_list_bound_lost':('cooling','resp->mitigation_device_list_len > ARRAY_SIZE(resp->mitigation_device_list)','false'),
    'cooling_optional_list_flag_ignored':('cooling','for (i = 0; resp->mitigation_device_list_valid &&','for (i = 0; true &&'),
    'cooling_handle_release_before_init':('cooling','if (tmd->handle_ready)','if (true)'),
    'cooling_arrival_work_not_drained':('cooling','cancel_work_sync(&tmd->svc_arrive_work);','(void)0;'),
    'cooling_unrelated_server_deleted':('cooling','if (!service || (service->node == tmd->server.sq_node &&','if (true || (service->node == tmd->server.sq_node &&'),
    'sensor_stale_generation_accepted':('sensor','generation != ts->generation','false'),
    'sensor_suspend_ignored':('sensor','atomic_read(&ts->controller->in_suspend)','0'),
    'sensor_temperature_not_updated':('sensor','sensor->last_reading = ind->temp;','(void)0;'),
    'sensor_invalid_temperature_used':('sensor','!ind || !sq || !ind->temp_valid ||','!ind || !sq ||'),
    'sensor_source_not_checked':('sensor','sq->sq_node != ts->server.sq_node ||','false ||'),
    'sensor_high_threshold_equality_lost':('sensor','ind->temp >= sensor->high_thresh','ind->temp > sensor->high_thresh'),
    'sensor_remote_error_positive':('sensor','return -EREMOTEIO;','return result->result;'),
    'sensor_failed_threshold_not_retried':('sensor','sensor->trips_pending) {','false) {'),
    'sensor_high_sentinel_ignored':('sensor','req.temp_threshold_high != INT_MAX;','true;'),
    'sensor_list_bounds_lost':('sensor','resp->sensor_list_len > ARRAY_SIZE(resp->sensor_list)','false'),
    'sensor_notification_work_not_drained':('sensor','cancel_work_sync(&sensor->therm_notify_work);','(void)0;'),
    'sensor_notifier_unregistration_lost':('sensor','if (controller->notifier_ready)','if (false)'),
    'sensor_zone_unregistration_lost':('sensor','if (sensor->tz_dev)\n\t\t\t\tdevm_thermal_of_zone_unregister','if (false)\n\t\t\t\tdevm_thermal_of_zone_unregister'),
    'sensor_lookup_handle_leak':('sensor','if (ts->handle_ready)','if (false)'),
   }
   report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    src=generated[group];assert before in src;report['negative_controls'][name]=execute(name,src.replace(before,after) if name in ['cooling_stale_generation_accepted','sensor_stale_generation_accepted','sensor_suspend_ignored'] else src.replace(before,after,1),True)
 pins=json.loads((H/'thermal-endpoints-imports.json').read_text());report['source_sha256']={x['path']:hashlib.sha256((root/x['path']).read_bytes()).hexdigest() for x in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('thermal_endpoints_*'))};report['limits']='Extracted production source using native protocol structs/name tables with mocked kernel thermal/QoS/QMI/DT/device/workqueue services and pthread serialization. No actual QMI codec, kernel framework, modem/DSP, hardware or thermal-control validation.';a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
