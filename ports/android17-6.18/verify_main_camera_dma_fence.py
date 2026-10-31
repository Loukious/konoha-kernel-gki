from pathlib import Path
import argparse
import hashlib
import json
import re
import resource
import subprocess
import tempfile

H = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--source-root', type=Path, default=H / 'private-source')
ap.add_argument('--negative', action='store_true')
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
base = a.source_root / 'qcom/opensource/camera-kernel/drivers/cam_sync'
sources = {n: (base / n).read_text() for n in ['cam_sync_dma_fence.c', 'cam_sync_dma_fence.h']}
code = re.sub(r'^#include[^\n]*\n', '', sources['cam_sync_dma_fence.c'], flags=re.M)
for name in ['__cam_dma_fence_get_monitor_entries', '__cam_dma_fence_get_prev_monitor_entries',
             '__cam_dma_fence_dump_monitor_array', '__cam_dma_fence_save_previous_monitor_data']:
    m = re.search(r'^(?:static[ \t]+)?(?:inline[ \t]+)?(?:\w+[ \t]+)*\w+[ \t*]+(?:\n[ \t]*)?' + name + r'\([^;{]*\{', code, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (code[end] == '{') - (code[end] == '}')
        end += 1
    code = code[:m.start()] + code[end:]
header = re.sub(r'^#include[^\n]*\n', '', sources['cam_sync_dma_fence.h'], flags=re.M)
controls = []
with tempfile.TemporaryDirectory(prefix='main-camera-dma-') as td:
    d = Path(td)
    fragment = d / 'dma.inc'
    fragment.write_text(code)
    (d / 'dma_header.inc').write_text(header)
    cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
           '-Wno-unused-parameter', '-g', '-pthread', '-fsanitize=address,undefined',
           '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
           '-I', str(d), str(H / 'tests/main_camera_dma_fence.c'), '-o', str(d / 'dma')]
    subprocess.run(cmd, check=True)
    r = subprocess.run([str(d / 'dma')], check=True, capture_output=True, text=True, timeout=25)
    result = json.loads(r.stdout)
    print(r.stdout.strip(), flush=True)
    if a.negative:
        variants = [
            ('callback_not_published', 'row->callback = callback;', 'row->callback = NULL;'),
            ('pending_callback_not_removed', 'dma_fence_remove_callback(fence, &callback->cb);', '(void)fence;'),
            ('running_work_not_drained', 'cancel_work_sync(&callback->work);', '(void)callback;'),
            ('slot_reused_before_drain', '/* Removal drains the IRQ callback; then drain any work it queued. */', 'clear_bit(idx, dev->bitmap);'),
            ('import_table_reference_missing', '/* Keep separate table and caller references for a first-time import. */\n\tdma_fence_get(fence);', '/* Missing table reference. */'),
            ('foreign_fence_forced_signal', 'if (!external)\n\t\t__cam_dma_fence_signal_fence', 'if (external || !external)\n\t\t__cam_dma_fence_signal_fence'),
            ('module_reference_missing', 'if (!try_module_get(THIS_MODULE)) {', 'if (false) {'),
            ('sequence_reused_after_reopen', '/* Context/sequence pairs remain unique across camera file reopen. */', 'atomic64_set(&g_cam_dma_fence_seq_no, 0);'),
            ('already_signaled_import_lost', 'if (rc == -ENOENT) {', 'if (false) {'),
            ('wrong_fence_identity_accepted', '(expected && row->fence != expected)', '(false && expected)'),
            ('camera_work_in_irq_context', 'queue_work(callback->dev->work_queue, &callback->work);', '__cam_dma_fence_signal_work(&callback->work);'),
            ('recycled_fd_identity_ignored', 'row->fd == fd && row->fence == fence', 'row->fd == fd'),
            ('incoming_callbacks_not_quiesced', 'void cam_dma_fence_quiesce(void)\n{', 'void cam_dma_fence_quiesce(void)\n{\n\treturn;'),
        ]
        for name, old, new in variants:
            # Withdrawal also occurs in quiesce; these controls alter the first release site.
            assert 1 <= code.count(old) <= 2, (name, code.count(old))
            fragment.write_text(code.replace(old, new, 1))
            subprocess.run(cmd, check=True, capture_output=True)
            try:
                r = subprocess.run([str(d / 'dma')], capture_output=True, timeout=10,
                                   preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                assert r.returncode, name
                returncode = r.returncode
            except subprocess.TimeoutExpired:
                returncode = 'bounded-deadlock-timeout'
            controls.append({'name': name, 'rejected': True, 'returncode': returncode})
            print('REJECTED', name, flush=True)
            fragment.write_text(code)
report = {'status': 'PASS', 'tests': result, 'negative_controls': controls,
          'source_sha256': {str((base / n).relative_to(a.source_root)): hashlib.sha256(s.encode()).hexdigest() for n, s in sources.items()},
          'harness_sha256': {n: hashlib.sha256((H / 'tests' / n).read_bytes()).hexdigest() for n in ['main_camera_dma_fence.c', 'main_camera_fence_mock.h']},
          'limits': 'Actual DMA table, registration, IRQ callback, work, signaling, creation, import, close and deinit functions; mocked DMA-fence/refcount/workqueue/module/fd services. Monitor helpers and downstream sync-generation validation mocked; no native kernel or hardware execution.'}
a.output.write_text(json.dumps(report, indent=2) + '\n')
