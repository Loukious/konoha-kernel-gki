#!/usr/bin/env python3
"""Exercise extracted USB support-driver helpers with simulated kernel services."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def function(source,name):
    source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
    match=re.search(r'(?m)^(?:static )?(?:inline )?(?:const )?(?:enum \w+|struct \w+|unsigned(?: int)?|[A-Za-z_]\w*)[ \t\n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{',source)
    if not match: raise ValueError('missing '+name)
    end=match.end();depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]+'\n'

def defines(source):
    lines=[];active=False
    for line in source.splitlines():
        if line.startswith('#define') or active:
            lines.append(line);active=line.endswith('\\')
    return '\n'.join(lines)+'\n'

def baseline(source,names):
    result='\n'.join(function(source,n) for n in names)
    for n in names: result=re.sub(r'\b'+n+r'\b','baseline_'+n,result)
    return result

def run_harness(text,path):
    (path/'harness.c').write_text(text)
    subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Werror=implicit-function-declaration','-pthread',str(path/'harness.c'),'-o',str(path/'harness')],check=True)
    run=subprocess.run([str(path/'harness')],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr)
    return json.loads(run.stdout)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);args=ap.parse_args()
    root=args.workspace.resolve();base=root/args.package
    imports=[e for e in json.loads((HERE/'imports.json').read_text()) if e['path'] in ['drivers/usb/phy/phy-msm-ssusb-qmp.c', 'drivers/soc/qcom/eud.c', 'drivers/usb/redriver/redriver.c']];sources={e['path']:(base/e['path']).read_text() for e in imports}
    original={e['path']:subprocess.check_output(['git','-C',str(root/e['project']),'cat-file','blob',e['blob']],text=True) for e in imports}
    qmp=sources['drivers/usb/phy/phy-msm-ssusb-qmp.c'];old=original['drivers/usb/phy/phy-msm-ssusb-qmp.c']
    dt=json.loads((HERE/'tests/qmp_dt.json').read_text());arrays='static u32 dt_offsets[USB3_PHY_REG_MAX] = {'+','.join(map(str,dt['offsets']))+'};\nstatic u32 dt_sequence[] = {'+','.join(map(str,dt['sequence']))+'};\n'
    names=['msm_ssusb_qmp_gdsc','qmp_supply_disable','qmp_supply_enable','qmp_power_off','msm_ssusb_qmp_ldo_enable','qmp_offset_valid','qmp_validate_tables','msm_ssphy_qmp_enable_clks','msm_ssphy_qmp_reset','msm_ssphy_qmp_dp_combo_reset','get_cable_status_str','msm_ssusb_qmp_clr_lfps_rxterm_int','msm_ssusb_qmp_clamp_enable','msm_ssusb_qmp_enable_autonomous','configure_phy_regs','msm_ssphy_qmp_setmode','usb_qmp_powerup_phy','usb_qmp_update_portselect_phymode']
    old_names=[n for n in names if n not in ['qmp_supply_disable','qmp_supply_enable','qmp_power_off','qmp_offset_valid','qmp_validate_tables']]
    types=defines(qmp)+defines((base/'include/linux/usb/dwc3-msm.h').read_text()) if (base/'include/linux/usb/dwc3-msm.h').exists() else defines(qmp)+defines((root/'onyx/include/linux/usb/dwc3-msm.h').read_text())
    types+='\n'.join(re.findall(r'enum \w+\s*\{[^}]*\};',qmp))+'\n'
    types+=re.search(r'struct qmp_reg_val \{.*?\n\};',qmp,re.S)[0]+'\n'+re.search(r'struct msm_ssphy_qmp \{.*?\n\};',qmp,re.S)[0]
    tests=HERE/'tests/qmp_harness.c';text=tests.read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */','\n'.join(function(qmp,n) for n in names)).replace('/* BASELINE */',baseline(old,old_names)).replace('/* DT_ARRAYS */',arrays)
    with tempfile.TemporaryDirectory(prefix='onyx-usbaux-') as tmp:
        result={'qmp':run_harness(text,Path(tmp))}
        eud=sources['drivers/soc/qcom/eud.c']
        eud_names=['msm_eud_clkref_en','msm_eud_enable_irqs','msm_eud_hw_is_enabled','set_eud_utmi_switch_delay','check_eud_mode_mgr2','enable_eud','disable_eud','param_eud_set','is_usb_psy_available','eud_event_notifier','usb_attach_detach','chgr_enable_disable','pet_eud','eud_serial_irqs','eud_stop_tx','eud_start_tx','eud_stop_rx','eud_startup','eud_shutdown','eud_config_port','eud_uart_rx','eud_uart_tx','handle_eud_irq','msm_eud_cleanup']
        types=defines(eud)+re.search(r'struct eud_chip \{.*?\n\};',eud,re.S)[0]
        text=(HERE/'tests/eud_harness.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */','\n'.join(function(eud,n) for n in eud_names))
        result['eud']=run_harness(text,Path(tmp))
        cleanup=function(eud,'msm_eud_cleanup')
        assert cleanup.index('eud_private = NULL') < cleanup.index('devm_free_irq') < cleanup.index('cancel_work_sync') < cleanup.index('uart_remove_one_port') < cleanup.index('power_supply_put')
        probe=function(eud,'msm_eud_probe')
        assert probe.index('INIT_WORK') < probe.index('devm_request_irq')
        assert probe.index('spin_lock_init') < probe.index('devm_request_irq')
        assert probe.index('uart_add_one_port') < probe.index('devm_request_irq') < probe.index('eud_private = pdev')
        assert 'port->state->xmit' not in eud and 'struct circ_buf' not in eud
        redriver=sources['drivers/usb/redriver/redriver.c']
        public=(base/'include/linux/usb/redriver.h').read_text()
        types=re.search(r'struct usb_redriver \{.*?\n\};',public,re.S)[0]
        names=re.findall(r'EXPORT_SYMBOL_GPL\((\w+)\)',redriver)
        text=(HERE/'tests/redriver_harness.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */','\n'.join(function(redriver,n) for n in names))
        result['redriver']=run_harness(text,Path(tmp))
    result['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
    result['source_sha256']={k:hashlib.sha256((base/k).read_bytes()).hexdigest() for k in sources}
    result['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'tests').iterdir() if p.name in ['qmp_harness.c', 'eud_harness.c', 'redriver_harness.c', 'qmp_dt.json']}
    result['limits']='Extracted functions with mocked services; no real hardware, kernel locking, IRQs, workqueues, runtime PM, firmware or device boot.'
    output=json.dumps(result,indent=2)+'\n'
    if args.output: args.output.write_text(output)
    print(output,end='')
if __name__=='__main__':main()
