#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
from verify_lpd import function

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    base = args.workspace / args.package / 'drivers/power/supply/mca/mca_hardware_monitor'
    source = (base / 'mca_connector_antiburn.c').read_text()
    header = (base / 'inc/mca_connector_antiburn.h').read_text()
    globals = source[source.index('static struct connector_antiburn __rcu'):source.index('int connector_antiburn_is_triggered')]
    constants = '\n'.join(re.findall(r'^#define (?:CONNECTOR_|NTC_|COMBINED_|THERMAL_|VBUS_).*', source, re.M))
    table = source[source.index('struct mca_sysfs_attr_info antiburn_sysfs_field_tbl[]'):source.index('static ssize_t antiburn_sysfs_show(', source.index('struct mca_sysfs_attr_info antiburn_sysfs_field_tbl[]'))]
    names = ['connector_antiburn_is_triggered', 'connector_antiburn_get_temperature',
             'connector_antiburn_get_temp_increase_rate', 'connector_antiburn_ensure_vbus_sense5V',
             'connector_antiburn_check_status', 'connector_antiburn_monitor_workfunc',
             'connector_antiburn_mos_ctrl_gpio_init', 'connector_antiburn_gpio_init',
             'antiburn_sysfs_show', 'antiburn_sysfs_store', 'connector_antiburn_validate_config',
             'connector_antiburn_stop', 'connector_antiburn_probe', 'connector_antiburn_remove',
             'connector_antiburn_shutdown', 'connector_antiburn_thermal_notifier_event',
             'connector_antiburn_debug_notifier_cb', 'connector_antiburn_dump_log_context']
    declarations = '\n'.join(function(source,n).split('{',1)[0].strip()+';' for n in names)
    functions = '\n'.join(function(source,n) for n in names)
    test = (HERE / 'tests/antiburn_lifecycle.c').read_text().replace('/* HEADER */', header).replace('/* GLOBALS */', constants+'\n'+globals).replace('/* TABLE */', table).replace('/* DECLARATIONS */', declarations).replace('/* FUNCTIONS */', functions)
    dtbase=args.workspace/'baseline/kernel/xiaomi/sm8735-devicetrees/qcom'
    dtfiles=[dtbase/'mca.dtsi',dtbase/'onyx-charger-common.dtsi']
    props={}
    for f in dtfiles:
        dt=re.sub(r'/\*.*?\*/|//[^\n]*','',f.read_text(),flags=re.S)
        block=re.search(r'(?:connector_antiburn:\s*|&)connector_antiburn\s*\{(.*?)\n\s*\};',dt,re.S)
        assert block,f
        for name,value in re.findall(r'([\w_-]+)\s*=\s*<([^>]*)>',block.group(1)):
            if '&' not in value:
                props[name]=int(value.strip(),0)
        for name,value in re.findall(r'([\w_-]+)\s*=\s*"([^"]+)"',block.group(1)):
            props[name]=value
    code='static void test_stock_config(void){struct connector_antiburn cfg={0};\n'
    parse=function(source,'connector_antiburn_parse_dt')
    for name,field,default in re.findall(r'mca_parse_dts_u32\(\s*np,\s*"([^"]+)",\s*&conn->(\w+),\s*(\w+)\)',parse):
        code+='cfg.'+field+' = '+(str(props[name]) if name in props else default)+';\n'
    for name,field in re.findall(r'mca_parse_dts_string\(np,\s*"([^"]+)",\s*&conn->(\w+)\)',parse):
        code+='cfg.'+field+' = '+json.dumps(props[name])+';\n'
    code+='assert(!connector_antiburn_validate_config(&cfg));}\n'
    test=test.replace('/* STOCK */',code)
    with tempfile.TemporaryDirectory(prefix='onyx-antiburn-tests-') as tmp:
        d=Path(tmp)
        (d/'test.c').write_text(test)
        subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],check=True)
        run=subprocess.run([str(d/'test')],text=True,capture_output=True,timeout=20)
        if run.returncode:
            raise RuntimeError(run.stdout+run.stderr)
        report=json.loads(run.stdout)
    report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
    report['stock_device_tree']={'properties':len(props),'source_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in dtfiles}}
    report['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_connector_antiburn.c',base/'inc/mca_connector_antiburn.h']}
    report['limits']='Extracted production protection/sensor/lifecycle/sysfs functions with pthread mutex/SRCU models and mocked thermal, GPIO, workqueue, protocol, sysfs, notifier and log APIs. No live thermal-zone lifetime, kernel workqueue/SRCU, electrical, firmware, temperature or phone validation.'
    out=json.dumps(report,indent=2)+'\n'
    print(out,end='')
    if args.output:
        args.output.write_text(out)

if __name__=='__main__':
    main()
