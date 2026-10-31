#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
    source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
    m=re.search(r'(?m)^(?:static )?[\w \t*\n]+?\b'+name+r'\([^;]*?\)\n\{',source);assert m,name
    e,d=m.end(),1
    while d:d+=(source[e]=='{')-(source[e]=='}');e+=1
    return source[m.start():e]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args()
    base=a.workspace/a.package/'drivers/power/supply/mca/mca_hardware_monitor';source=(base/'mca_bmd.c').read_text();header=(base/'inc/mca_bmd.h').read_text()
    structs=source[source.index('enum bmd_scheme'):source.index('static int mca_bmd_get_btb_status')]
    constants='\n'.join(re.findall(r'^#define (?:BTB_|MONITOR_|DELAY_|MCA_BMD_).*',source,re.M))
    table=source[source.index('enum btb_attr_list'):source.index('static ssize_t btb_sysfs_show',source.index('static const struct attribute_group'))]
    names=['mca_bmd_get_btb_status','mca_bmd_get_status','mca_bmd_update_status','mca_bmd_monitor_workfunc','mca_bmd_delay_report_bmd_sts_work','mca_bmd_process_event','mca_bmd_parse_dt','btb_sysfs_show','btb_sysfs_create_group','mca_bmd_stop','mca_bmd_probe','mca_bmd_remove','mca_bmd_shutdown']
    declarations='\n'.join(function(source,n).split('{',1)[0].strip()+';' for n in names)
    functions='\n'.join(function(source,n) for n in names)
    dtbase=a.workspace/'baseline/kernel/xiaomi/sm8735-devicetrees/qcom';dtfiles=[dtbase/'mca.dtsi',dtbase/'onyx-charger-common.dtsi'];schemes=None
    for f in dtfiles:
        text=re.sub(r'/\*.*?\*/|//[^\n]*','',f.read_text(),flags=re.S)
        block=re.search(r'(?:mca_bmd:\s*|&)mca_bmd\s*\{(.*?)\n\s*\};',text,re.S);assert block,f
        match=re.search(r'btb_bmd_scheme\s*=\s*<([^>]*)>',block.group(1))
        if match:schemes=[int(v,0) for v in match.group(1).split()]
    assert schemes==[3,1],schemes
    tests=(HERE/'tests/bmd_lifecycle.c').read_text();values=sorted(set(re.findall(r'\b(?:MCA_EVENT_\w+|STRATEGY_(?:FUNC|STATUS)_\w+|MIEVENT_STATE_\w+|CHARGE_DFX_\w+)\b',source+tests)))
    enums='enum {'+','.join(values)+'};\n#define STOCK_MASTER_SCHEME '+str(schemes[0])+'\n#define STOCK_SLAVE_SCHEME '+str(schemes[1])+'\n'
    tests=tests.replace('/* ENUMS */',enums).replace('/* HEADER */',header+'\n'+constants+'\n'+structs).replace('/* DECLARATIONS */',declarations).replace('/* TABLE */',table).replace('/* FUNCTIONS */',functions)
    with tempfile.TemporaryDirectory(prefix='onyx-bmd-tests-') as tmp:
        d=Path(tmp);(d/'test.c').write_text(tests);proc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
        if proc.returncode:raise RuntimeError(proc.stderr)
        run=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
        if run.returncode:raise RuntimeError(run.stdout+run.stderr)
        report=json.loads(run.stdout)
    report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['stock_device_tree']={'schemes':schemes,'source_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in dtfiles}}
    report['source_sha256']={str(f.relative_to(base)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [base/'mca_bmd.c',base/'inc/mca_bmd.h']}
    report['limits']='Extracted production BMD functions with pthread locks and mocked registration/sysfs/devm/workqueue/GPIO/IIO/FG APIs. No live kernel workqueue/callback, hardware battery detection, charging, firmware or phone validation.'
    out=json.dumps(report,indent=2)+'\n';print(out,end='')
    if a.output:a.output.write_text(out)

if __name__=='__main__':main()
