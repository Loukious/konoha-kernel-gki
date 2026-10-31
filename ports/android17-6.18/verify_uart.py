#!/usr/bin/env python3
"""Test staged UART FIFO, DMA cancellation, power rollback and RX bounds."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def function(source, name):
    match = re.search(r"(?m)^(?:static (?:inline )?)?[\w *]+\b" + name
                      + r"\([^;]*?\)\n\{", source)
    if not match:
        raise ValueError(f"missing function {name}")
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('--package', default='onyx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.workspace.resolve()
    source_path = root/args.package/'drivers/tty/serial/msm_geni_serial.c'
    source = source_path.read_text()
    common = (root/'common/drivers/soc/qcom/qcom-geni-se.c').read_text()
    headers = (root/'onyx/include/linux/qcom-geni-se-common.h').read_text()
    common_header = (root/'common/include/linux/soc/qcom/geni-se.h').read_text()
    assert re.search(r'#define\s+RX_GENI_CANCEL_IRQ\s+BIT\(14\)', common_header)
    assert not re.search(r'#define\s+RX_GENI_CANCEL_IRQ\b', source)
    definitions = ''
    names = ['msm_geni_serial_icc_disable', 'msm_geni_serial_resources_on',
             'msm_geni_serial_resources_off', 'msm_geni_tx_snapshot',
             'msm_geni_tx_unmap', 'msm_geni_tx_complete', 'msm_geni_gsi_put_tx_vote',
             'msm_geni_pack_fifo', 'msm_geni_uart_gsi_rx_cb',
             'msm_geni_uart_gsi_cancel_tx', 'msm_geni_uart_gsi_cancel_rx']
    for text, functions in [(headers, ['geni_se_common_clks_on', 'geni_se_common_clks_off']),
                            (common, ['geni_icc_enable', 'geni_icc_set_bw']), (source, names)]:
        definitions += '\n'.join(function(re.sub(r'/\*.*?\*/', '', text, flags=re.S), name)
                                 for name in functions)
    cleanup = function(source, 'msm_geni_serial_cleanup')
    assert cleanup.index('cancel_delayed_work_sync') < cleanup.index('uart_remove_one_port')
    assert cleanup.index('uart_remove_one_port') < cleanup.index('destroy_workqueue(port->tx_wq)')
    assert 'ipc_log_context_destroy(port->ipc_log_owned[i])' in cleanup
    startup = function(source, 'msm_geni_serial_startup')
    assert 'msm_geni_allocate_chan' in startup
    assert 'msm_geni_allocate_chan' not in function(source, 'msm_geni_uart_gsi_xfer_rx')
    template = HERE/'tests/uart_harness.c'
    with tempfile.TemporaryDirectory(prefix='onyx-uart-') as tmp:
        path = Path(tmp)
        (path/'harness.c').write_text(template.read_text().replace('/* STAGED_DEFINITIONS */', definitions))
        subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-Werror=implicit-function-declaration',
                        str(path/'harness.c'), '-o', str(path/'harness')], check=True)
        run = subprocess.run([str(path/'harness')], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(run.stderr)
    report = json.loads(run.stdout)
    report['sanitizers'] = ['AddressSanitizer', 'UndefinedBehaviorSanitizer']
    report['rx_cancel_bit'] = 14
    report['source_sha256'] = hashlib.sha256(source_path.read_bytes()).hexdigest()
    report['harness_sha256'] = hashlib.sha256(template.read_bytes()).hexdigest()
    report['limits'] = ('Actual extracted helpers with simulated FIFO/kernel services; '
                        'does not exercise real DMA, IRQs, workqueues, runtime PM, '
                        'Bluetooth hardware or phone boot. Failed GPI hardware reset '
                        'during final channel release is not proven quiescent.')
    output = json.dumps(report, indent=2)+'\n'
    if args.output:
        args.output.write_text(output)
    print(output, end='')


if __name__ == '__main__':
    main()
