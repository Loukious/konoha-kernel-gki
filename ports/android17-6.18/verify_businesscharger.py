#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
    source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
    m=re.search(r'(?m)^(?:static )?[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source)
    assert m,name
    e,d=m.end(),1
    while d:
        d+=(source[e]=='{')-(source[e]=='}');e+=1
    return source[m.start():e]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args()
    b=a.workspace/a.package/'drivers/power/supply/mca/mca_business/business_charger'
    sources={k:(b/f'mca_{k}.c').read_text() for k in ['business_charger','charger_usb_psy','charger_wireless_psy']}
    headers='\n'.join(re.sub(r'^#include[^\n]*\n','',(b/'inc'/f'mca_{k}.h').read_text(),flags=re.M) for k in ['charger_usb_psy','charger_wireless_psy','business_charger'])
    alltext='\n'.join(sources.values())+'\n'+'\n'.join(f.read_text() for f in (HERE/'tests').glob('business_*.c'))
    powerprops=sorted(set(re.findall(r'\bPOWER_SUPPLY_PROP_\w+',alltext)))
    powertypes=sorted(set(re.findall(r'\bPOWER_SUPPLY_TYPE_\w+',alltext)))
    generic=sorted(set(re.findall(r'\b(?:MCA_EVENT_\w+|MCA_CHARGE_LOG_ID_\w+|XM_CHARGER_TYPE_\w+|STRATEGY_(?:FUNC|STATUS|CONFIG)_\w+|MAIN_BUCK_CHARGER)\b',alltext)))
    generic.remove('XM_CHARGER_TYPE_UNKNOW');generic.insert(0,'XM_CHARGER_TYPE_UNKNOW = 0')
    enums='enum power_supply_property {'+','.join(powerprops)+'};\nenum power_supply_type {'+','.join(powertypes)+'};\nenum {'+','.join(generic)+'};\n'
    selected=['business_charger_parse_dt','business_charger_event_size','business_charger_add_event_node','business_charger_next_event','business_charger_event_work','business_charger_stop','business_charger_probe','business_charger_remove','business_charger_shutdown']+[f'business_charger_notifier_{n}_cb' for n in ['connect','type','hw','chg_sts','cp_info','debug']]
    functions='\n'.join(function(sources['business_charger'],n) for n in selected)
    globals=sources['business_charger'][sources['business_charger'].index('static DEFINE_MUTEX'):sources['business_charger'].index('#define QUICK_CHG')]
    declarations='\n'.join(function(sources['business_charger'],n).split('{',1)[0].strip()+';' for n in selected)
    dtbase=a.workspace/'baseline/kernel/xiaomi/sm8735-devicetrees/qcom'
    dtfiles=[dtbase/'mca.dtsi',dtbase/'onyx-charger-common.dtsi'];props={}
    for f in dtfiles:
        text=re.sub(r'/\*.*?\*/|//[^\n]*','',f.read_text(),flags=re.S)
        block=re.search(r'(?:mca_business_charger:\s*|&)mca_business_charger\s*\{(.*?)\n\s*\};',text,re.S)
        assert block,f
        for name,value in re.findall(r'([\w,\-]+)\s*=\s*<([^>]*)>',block.group(1)):
            props[name]=int(value.strip(),0)
    assert props['support-wls']==0,props
    enums+='\n#define STOCK_WLS '+str(props['support-wls'])+'\n#define STOCK_TEST '+str(props['business,charger-core-test'])+'\n'
    common=(HERE/'tests/business_common.h').read_text().replace('/* ENUMS */',enums).replace('/* HEADERS */',headers)
    tests={'lifecycle':common.replace('/* DECLARATIONS */',globals+'\n'+declarations)+(HERE/'tests/business_lifecycle.c').read_text().replace('/* FUNCTIONS */',functions)}
    psy='\n'.join(re.sub(r'^#include[^\n]*\n','',sources[k],flags=re.M) for k in ['charger_usb_psy','charger_wireless_psy'])
    tests['psy']=common.replace('/* DECLARATIONS */','')+(HERE/'tests/business_psy.c').read_text().replace('/* FUNCTIONS */',psy)
    src=sources['business_charger']
    selected_policy=['business_charger_strategy_process','update_usb_pd_type','business_charger_battery_property','business_charger_process_handle_logic','business_charger_process_lpd_status_change','business_charger_process_antiburn_change','business_charger_process_sbu_lpd_status_change','business_charger_get_power_max','business_charger_get_quick_charge_type','business_charger_partition_set_status','business_charger_process_debug_ctrl_input','business_charger_process_is_eu_model','business_charger_process_plate_shock','bussiness_charger_sysfs_store','business_charge_dump_log_context']
    policy_functions='\n'.join(function(src,n) for n in selected_policy)
    policy_declarations='\n'.join(function(src,n).split('{',1)[0].strip()+';' for n in selected_policy)
    table=src[src.index('enum bussiness_charger_attr_list'):src.index('static ssize_t bussiness_charger_sysfs_show')]
    table+=src[src.index('struct mca_sysfs_attr_info bussiness_charger_sysfs_field_tbl[]'):src.index('static struct attribute\n')]
    tests['policy']=common.replace('/* DECLARATIONS */',policy_declarations)+(HERE/'tests/business_policy.c').read_text().replace('/* TABLE */',table).replace('/* FUNCTIONS */',policy_functions)
    reports={}
    for group,test in tests.items():
        with tempfile.TemporaryDirectory(prefix='onyx-business-'+group+'-') as temp:
            d=Path(temp);(d/'test.c').write_text(test)
            proc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
            if proc.returncode:raise RuntimeError(proc.stderr)
            run=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
            if run.returncode:raise RuntimeError(run.stdout+run.stderr)
            reports[group]=json.loads(run.stdout)
    reports['stock_device_tree']={'properties':props,'source_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in dtfiles}}
    reports['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
    reports['source_sha256']={str(f.relative_to(b)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [b/f'mca_{k}.c' for k in sources]+sorted((b/'inc').glob('*.h'))}
    reports['limits']='Extracted production queue/notifier/probe/stop/PSY functions with pthread locks and mocked kernel registration/workqueue/PSY/provider APIs. No live kernel callback/workqueue, firmware, electrical, charging or phone validation.'
    out=json.dumps(reports,indent=2)+'\n';print(out,end='')
    if a.output:a.output.write_text(out)

if __name__=='__main__':main()
