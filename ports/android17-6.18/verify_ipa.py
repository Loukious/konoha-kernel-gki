#!/usr/bin/env python3
"""Run extracted native IPA lifetime/fault functions with sanitizer mocks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

H = Path(__file__).resolve().parent
IPA = 'qcom/opensource/dataipa/drivers/platform/msm'
MEM = 'qcom/opensource/datarmnet-ext/mem'


def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(name)+r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError('Function extraction failed: '+name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]+'\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--source-dir', default='onyx')
    ap.add_argument('--output', type=Path)
    ap.add_argument('--negative', action='store_true')
    args = ap.parse_args()
    root = args.workspace/args.source_dir
    fixture = H/('ipa-imports.json' if (H/'ipa-imports.json').exists() else 'imports.json')
    pins = json.loads(fixture.read_text())
    def native(path):
        pin = next(item for item in pins if item['path'] == path)
        return subprocess.check_output(['git','-C',str(args.workspace/pin['project']),'cat-file','blob',pin['blob']], text=True)
    usb = (root/IPA/'ipa/ipa_clients/ipa_usb.c').read_text()
    core = (root/IPA/'ipa/ipa_v3/ipa.c').read_text()
    net = (root/IPA/'ipa/ipa_v3/ipa_net.c').read_text()
    mem = (root/MEM/'rmnet_mem_main.c').read_text()
    gsi = (args.workspace/'onyx/drivers/usb/gadget/function/f_gsi.c').read_text()
    intf = (root/IPA/'ipa/ipa_v3/ipa_intf.c').read_text()
    hal = (root/IPA/'gsi/gsihal/gsihal_reg.c').read_text()
    groups = {
        'usb_bounds': [(usb,n) for n in ['ipa3_usb_check_chan_params','ipa_usb_xdci_connect']],
        'gsi_ring': [(hal,'gsihal_check_ring_length_valid')],
        'usb_lifecycle': [(usb,n) for n in ['ipa3_usb_reset_notify','ipa3_usb_deregister_pm','ipa3_usb_register_pm','ipa_usb_init_teth_prot','ipa_usb_deinit_teth_prot','ipa3_usb_init','ipa3_usb_exit','ipa3_usb_register_ready_cb']],
        'notify': [(usb,n) for n in ['ipa3_usb_set_notify_data','ipa3_usb_reset_notify','ipa3_usb_notify_do','ipa3_usb_wq_notify_remote_wakeup']]+[(gsi,n) for n in ['gsi_ipa_token_activate','gsi_ipa_token_retire','ipa_usb_notify_cb']],
        'pool': [(mem,n) for n in ['rmnet_mem_add_page','rmnet_mem_free_all','rmnet_mem_get_pages_entry','rmnet_mem_adjust','rmnet_mem_module_init','rmnet_mem_module_exit']],
        'ready': [(core,n) for n in ['ipa_register_ipa_ready_cb','ipa3_unregister_ipa_ready_cb','ipa_trigger_ipa_ready_cbs']],
        'net': [(net,n) for n in ['ipa_late_init','ipa_late_exit']],
        'message': [(intf,'ipa3_read')],
        'napi': [(core,n) for n in ['ipa3_enable_napi_netdev','ipa3_disable_napi_netdev']],
    }
    result = {}
    generated = {}
    with tempfile.TemporaryDirectory(prefix='onyx-ipa-tests-') as temp:
        for group, functions in groups.items():
            source = (H/'tests'/('ipa_'+group+'.c')).read_text().replace('/* FUNCTIONS */', '\n'.join(function(s,n) for s,n in functions))
            if group == 'usb_bounds':
                public = (args.workspace/'onyx/include/linux/ipa_usb.h').read_text()
                names = ['ipa_usb_teth_prot','ipa_usb_max_usb_packet_size','ipa_usb_gsi_chan_dir','ipa_usb_teth_prot_params','ipa_usb_xdci_connect_params','ipa_usb_xdci_chan_scratch','ipa_usb_xdci_chan_params','ipa_req_chan_out_params']
                definitions = []
                for name in names:
                    match = re.search(r'(?:struct|enum) '+name+r' \{.*?\n\};', public, re.S)
                    assert match, name
                    definitions.append(match.group())
                match = re.search(r'struct ipa_usb_xdci_connect_params_internal \{.*?\n\};', usb, re.S)
                assert match
                definitions.append(match.group())
                source = source.replace('/* TYPES */', '\n'.join(definitions))
            if group == 'message':
                header = (root/IPA/'include/uapi/linux/msm_ipa.h').read_text()
                definition = re.search(r'struct ipa_msg_meta \{.*?\n\};', header, re.S).group()
                source = source.replace('/* MESSAGE TYPE */', definition)
            if group == 'gsi_ring':
                header = (root/IPA/'gsi/gsihal/gsihal_reg_i.h').read_text()
                names = ['GSI_EE_n_GSI_CH_k_CNTXT_1_R_LENGTH_BMSK','GSI_V2_9_EE_n_GSI_CH_k_CNTXT_1_R_LENGTH_BMSK','GSI_V3_0_EE_n_GSI_CH_k_CNTXT_1_R_LENGTH_BMSK','GSI_V3_0_MAX_ELEMENTS_PER_RING']
                defines = [re.search(r'(?m)^#define '+name+r' .*$', header).group() for name in names]
                source = source.replace('/* REGISTER CONSTANTS */', '\n'.join(defines))
            generated[group] = source
            path = Path(temp)/(group+'.c')
            path.write_text(source)
            exe = path.with_suffix('')
            subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-pthread','-I',str(H/'tests'),str(path),'-o',str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
            if run.returncode:
                raise RuntimeError(group+': '+run.stderr)
            result[group] = json.loads(run.stdout)
        if args.negative:
            mutations = {
                'native_ring_bounds': ('gsi_ring', function(hal, 'gsihal_check_ring_length_valid'),
                    function(native(IPA+'/gsi/gsihal/gsihal_reg.c'), 'gsihal_check_ring_length_valid')),
                'native_late_init': ('net', function(net, 'ipa_late_init'),
                    function(native(IPA+'/ipa/ipa_v3/ipa_net.c'), 'ipa_late_init')),
                'message_payload_bounds': ('message', 'count - sizeof(msg->meta) < msg->meta.msg_len', 'false'),
                'message_header_copy_leak': ('message',
                    'if (msg->buff)\n\t\t\t\t\tmsg->callback(msg->buff, msg->meta.msg_len,\n\t\t\t\t\t\t      msg->meta.msg_type);', '(void)0;'),
                'napi_failure_masked': ('napi', 'return -ENOMEM;', 'return 0;'),
                'pool_unchecked_client': ('pool', ' || id >= ARRAY_SIZE(rmnet_mem_id_req)', ''),
                'usb_protocol_mismatch': ('usb_bounds',
                    'dl_chan_params->teth_prot != connect_params->teth_prot ||', ''),
                'pm_handle_not_cleared': ('usb_lifecycle', 'pm_ctx->hdl = ~0U;', 'pm_ctx->hdl = 1;'),
            }
            controls = {}
            for name, (group, before, after) in mutations.items():
                source = generated[group]
                assert source.count(before) == 1, (name, source.count(before))
                source = source.replace(before, after)
                path = Path(temp)/(name+'.c')
                path.write_text(source)
                exe = path.with_suffix('')
                subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-Wno-unused-variable','-Wno-unused-function','-Wno-unused-parameter','-Wno-sign-compare',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer','-no-pie','-pthread','-I',str(H/'tests'),str(path),'-o',str(exe)], check=True)
                run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
                assert run.returncode, name+' did not reject the fault'
                diagnostic = next((line for line in run.stderr.splitlines() if
                    any(marker in line for marker in ['Assertion', 'runtime error:', 'ERROR: AddressSanitizer'])), '')
                assert diagnostic, (name, run.returncode, run.stderr)
                controls[name] = {'rejected': True, 'returncode': run.returncode, 'diagnostic': diagnostic}
            result['negative_controls'] = controls
    result['source_sha256'] = {e['path']:hashlib.sha256((root/e['path']).read_bytes()).hexdigest() for e in pins}
    result['provider_source_sha256'] = {p:hashlib.sha256((args.workspace/'onyx'/p).read_bytes()).hexdigest() for p in ['drivers/usb/gadget/function/f_gsi.c', 'include/linux/ipa_usb.h']}
    result['harness_sha256'] = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('ipa_*')) if p.is_file()}
    result['limits'] = 'Extracted native driver functions with simulated Linux services; does not establish live kernel locks, IRQ/DMA/IOMMU ordering, IPA firmware/modem or phone hardware operation.'
    output = json.dumps(result, indent=2)+'\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')


if __name__ == '__main__':
    main()
