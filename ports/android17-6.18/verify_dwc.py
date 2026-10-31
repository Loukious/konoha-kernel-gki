#!/usr/bin/env python3
"""Check source-extracted DWC helpers and the pinned common hook ABI (not hardware)."""
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

def run(source,path):
    (path/'test.c').write_text(source)
    subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Werror=implicit-function-declaration','-pthread',str(path/'test.c'),'-o',str(path/'test')],check=True)
    r=subprocess.run([str(path/'test')],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stderr)
    return json.loads(r.stdout)

def hooks(workspace,ops):
    names=re.findall(r'\bENTRY(?:_EXIT)?\((\w+)\)',ops[ops.index('static struct kretprobe dwc3_msm_probes[]'):]);assert len(names)==16
    symmap=(workspace/'bazel-bin/common/kernel_aarch64/System.map').read_text();symbols=[x.split()[-1] for x in symmap.splitlines()]
    specs={
      'dwc3_gadget_run_stop':('drivers/usb/dwc3/gadget.c','struct dwc3 *dwc, int is_on'),
      'dwc3_send_gadget_ep_cmd':('drivers/usb/dwc3/gadget.c','struct dwc3_ep *dep, unsigned int cmd, struct dwc3_gadget_ep_cmd_params *params'),
      'dwc3_gadget_reset_interrupt':('drivers/usb/dwc3/gadget.c','struct dwc3 *dwc'),
      '__dwc3_gadget_ep_enable':('drivers/usb/dwc3/gadget.c','struct dwc3_ep *dep, unsigned int action'),
      'dwc3_host_exit':('drivers/usb/dwc3/host.c','struct dwc3 *dwc'),
      'dwc3_gadget_pullup':('drivers/usb/dwc3/gadget.c','struct usb_gadget *g, int is_on'),
      'android_work':('drivers/usb/gadget/android_configfs_uevent.c','struct work_struct *data'),
      'usb_add_gadget':('drivers/usb/gadget/udc/core.c','struct usb_gadget *gadget'),
      'dwc3_gadget_disconnect_interrupt':('drivers/usb/dwc3/gadget.c','struct dwc3 *dwc'),
      'dwc3_suspend_common':('drivers/usb/dwc3/core.c','struct dwc3 *dwc, pm_message_t msg'),
      'uas_sdev_configure':('drivers/usb/storage/uas.c','struct scsi_device *sdev, struct queue_limits *lim'),
    }
    trace_specs={
      'dwc3_log_request':'struct dwc3_request *req',
      'dwc3_log_gadget_ep_cmd':'struct dwc3_ep *dep, unsigned int cmd, struct dwc3_gadget_ep_cmd_params *params, int cmd_status',
      'dwc3_log_trb':'struct dwc3_ep *dep, struct dwc3_trb *trb',
      'dwc3_log_event':'u32 event, struct dwc3 *dwc',
      'dwc3_log_ep':'struct dwc3_ep *dep',
    }
    trace=(workspace/'common/drivers/usb/dwc3/trace.h').read_text();macro=(workspace/'common/include/trace/trace_events.h').read_text();assert 'trace_event_raw_event_##call(void *__data, proto)' in macro
    normalize=lambda s:re.sub(r'\s+',' ',s).strip()
    result=[]
    for name in names:
        assert symbols.count(name)==1,(name,'missing/ambiguous built symbol')
        if name in specs:
            path,expected=specs[name];src=(workspace/'common'/path).read_text();body=function(src,name);actual=body[body.index('(')+1:body.index(')')];assert normalize(actual)==expected,(name,actual)
        else:
            cls=name.removeprefix('trace_event_raw_event_');expected=trace_specs[cls];m=re.search(r'DECLARE_EVENT_CLASS\('+cls+r',\s*TP_PROTO\((.*?)\)',trace,re.S);assert m and normalize(m[1])==expected;path='drivers/usb/dwc3/trace.h';expected='void *__data, '+expected
        entry=function(ops,'entry_'+name);registers=sorted({int(x) for x in re.findall(r'regs->regs\[(\d+)\]',entry)});assert registers
        if name.startswith('trace_'):assert 0 not in registers and registers[0]==1
        result.append(dict(symbol=name,path=path,parameters=expected,arm64_argument_registers=registers))
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args();w=a.workspace.resolve();root=w/a.package
    core=(root/'drivers/usb/dwc3/dwc3-msm-core.c').read_text();ops=(root/'drivers/usb/dwc3/dwc3-msm-ops.c').read_text()
    sets={
      'resources':[(core,n) for n in ['dwc3_msm_free_gsi_buffers','dwc3_gsi_event_buf_alloc','dwc3_msm_register_interrupts','dwc3_msm_config_gdsc','vbus_regulator_toggle','dwc3_clk_enable_disable','dwc3_msm_record_irq']],
      'callbacks':[(core,n) for n in ['dwc3_msm_id_notifier','dwc3_msm_vbus_notifier','dwc3_msm_extcon_register','dwc3_msm_extcon_unregister']],
      'pm_hooks':[(core,n) for n in ['dwc3_msm_pin_module','dwc3_msm_pm_owner','dwc3_msm_core_remove','dwc3_msm_override_pm_ops','dwc3_host_prepare','dwc3_host_complete','dwc3_core_prepare','dwc3_core_complete','dwc3_msm_role_owner','dwc3_msm_usb_role_switch_get_role','dwc3_msm_usb_role_switch_set_role']]+[(ops,n) for n in ['dwc3_msm_owns_core','entry_usb_add_gadget','entry_dwc3_host_exit','dwc3_msm_kretprobe_init','dwc3_msm_kretprobe_exit']],
    }
    result={}
    with tempfile.TemporaryDirectory(prefix='onyx-dwc-') as temp:
        for name,items in sets.items():
            template=(HERE/'tests'/f'dwc_{name}.c').read_text();source=template.replace('/* FUNCTIONS */','\n'.join(function(src,n) for src,n in items));result[name]=run(source,Path(temp))
    result['hooks']=hooks(w,ops)
    cleanup=function(core,'dwc3_msm_cleanup')
    ordered=['WRITE_ONCE(mdwc->removing, true)','dwc3_msm_remove_files','dwc3_msm_extcon_unregister','disable_work_sync(&mdwc->restart_usb_work)','disable_work_sync(&mdwc->resume_work)','disable_work_sync(&mdwc->sm_work)','disable_delayed_work_sync','usb_unregister_notify','of_platform_depopulate','dwc3_msm_free_gsi_buffers','pm_runtime_disable','devm_free_irq','usb_put_redriver','destroy_workqueue','dwc3_msm_debug_exit','dev_set_drvdata']
    indexes=[cleanup.index(x) for x in ordered];assert indexes==sorted(indexes)
    assert 'usb_role_switch_set_drvdata' not in core
    removal=function(core,'dwc3_msm_core_remove');assert removal.index('dwc3_msm_original_core_remove(pdev)')<removal.index('dwc3_msm_free_gsi_buffers')
    assert 'IRQ_NOAUTOEN' not in core
    assert 'return -ENODEV' in function(core,'dwc3_msm_set_role')
    assert 'goto rollback_host' in function(core,'dwc3_otg_start_host')
    assert 'usb_role_switch_put(mdwc->dwc3_drd_sw)' in function(core,'dwc3_msm_core_init')
    for name in ['dwc3_resume_work','dwc3_restart_usb_work','dwc3_otg_sm_work','msm_dwc3_perf_vote_work','msm_dwc3_pwr_irq','msm_dwc3_pwr_irq_thread']:assert 'READ_ONCE(mdwc->removing)' in function(core,name)
    result['cleanup_order_checks']=ordered
    result['source_sha256']={row['path']:hashlib.sha256((root/row['path']).read_bytes()).hexdigest() for row in json.loads((HERE/'imports.json').read_text()) if row['path'] in ['drivers/usb/dwc3/dwc3-msm-core.c', 'drivers/usb/dwc3/dwc3-msm-ops.c', 'drivers/usb/dwc3/debug_ipc.c', 'drivers/usb/dwc3/debug-ipc.h', 'include/linux/usb/redriver.h', 'include/linux/soc/qcom/wcd939x-i2c.h', 'drivers/usb/dwc3/core.h', 'drivers/usb/dwc3/gadget.h', 'drivers/usb/dwc3/debug.h', 'drivers/usb/dwc3/io.h', 'drivers/usb/dwc3/trace.h', 'drivers/usb/host/xhci.h', 'drivers/usb/host/xhci-ext-caps.h', 'drivers/usb/host/pci-quirks.h', 'drivers/usb/host/xhci-port.h', 'drivers/usb/host/xhci-caps.h']}
    result['test_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE/'tests').glob('dwc_*.c')}
    result['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
    result['limits']='Extracted C functions with mock resources and pthread SRCU model; source-level hook signatures plus built System.map symbols. No actual kprobe registration, kernel SRCU/IRQ/PM/workqueue behavior, USB hardware, firmware or phone boot.'
    out=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(out)
    print(out,end='')
if __name__=='__main__':main()
