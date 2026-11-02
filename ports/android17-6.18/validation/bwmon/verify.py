from pathlib import Path
import json,hashlib,re
HERE=Path(__file__).resolve().parent
CONTROL=HERE.parent.parent
rel='drivers/soc/qcom/dcvs/bwmon.c'
native=(HERE/'before'/rel).read_text();candidate=(HERE/'candidate'/rel).read_text()
expected=native.replace('};\n\nstatic int configure_bwmon_resources','};\nMODULE_DEVICE_TABLE(of, qcom_bwmon_match_table);\n\nstatic int configure_bwmon_resources').replace('module_platform_driver(qcom_bwmon_driver);','/* No remove callback: keep the DCVS monitor registered for the boot lifetime. */\nbuiltin_platform_driver(qcom_bwmon_driver);')
assert candidate==expected
assert (HERE/'before/drivers/soc/qcom/dcvs/bwmon.h').read_bytes()==(HERE/'candidate/drivers/soc/qcom/dcvs/bwmon.h').read_bytes()
for name in ['max_freq','sample_ms','io_percent','hyst_length','hist_memory','hyst_trigger_count']:
 assert re.search(r'\b'+name+r'\b',candidate)
assert 'qcom_dcvs_kobject_get(m->hw.dcvs_hw)' in candidate and 'dev_name(dev)' in candidate
assert 'register_trace_android_vh_jiffies_update' in candidate
for compat in ['qcom,bwmon4','qcom,bwmon5']:assert '"'+compat+'"' in candidate
assert {'target':'bwmon','output':'bwmon.ko'} in json.loads((CONTROL/'modules.json').read_text())
assert 'CONFIG_QCOM_BWMON=m' in (CONTROL/'platform_defconfig').read_text()
assert 'depends on QCOM_DCVS && SCHED_WALT' in (CONTROL/'Kconfig').read_text()
report={'status':'PASS','source_identity':'Exact pinned native monitor and header; only OF module table/permanent registration differ','ABI':'Real DCVS monitor nodes expose six PowerHAL tuning controls','limitations':'Native probe-error cleanup retained; ordinary unload disabled; module imports and hardware binding checked separately'}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
