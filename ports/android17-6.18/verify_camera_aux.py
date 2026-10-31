#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile
H=Path(__file__).resolve().parent

def strip(s):return re.sub(r'^#include[^\n]*\n','',s,flags=re.M)
def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=a.workspace/a.source_dir;B=root/'qcom/opensource/camera-kernel/drivers';fake=(H/'tests/camera_fake.h').read_text();header=strip((B/'cam_msger/cam_msger.h').read_text())
 generated={'logger':fake+strip((B/'cam_log/cam_log.h').read_text())+'\n'+strip((B/'cam_log/cam_log.c').read_text())+'\n'+(H/'tests/camera_logger.c').read_text(),'messenger':fake+header+'\n'+(H/'tests/camera_msger_stubs.h').read_text()+'\n'+strip((B/'cam_msger/cam_msger.c').read_text())+'\n'+(H/'tests/camera_messenger.c').read_text()}
 generated['scheduler']=fake+header+'\n'+strip((B/'cam_msger/cam_sched.h').read_text())+'\nunsigned int msger_siwtches;\nu8 debug(int t){return 0;}\n'+strip((B/'cam_msger/cam_msger_common.c').read_text())+'\n'+strip((B/'cam_msger/cam_sched.c').read_text())+'\n'+(H/'tests/camera_scheduler.c').read_text()
 report={'tests':{}}
 with tempfile.TemporaryDirectory(prefix='camera-aux-test-') as d:
  tmp=Path(d)
  def execute(name,s,negative=False):
   f=tmp/(name+'.c');f.write_text(s);exe=f.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-missing-field-initializers','-Wno-sign-compare','-Wno-pointer-sign','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie',str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40)
   if negative:
    assert r.returncode,(name,'mutation survived');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for name,src in generated.items():report['tests'][name]=execute(name,src)
  if a.negative:
   mutations={
    'scheduler_last_allowed_cpu_denied':('scheduler','if (cpumask_empty(&fallback))','if (false)'),
    'logger_short_read_overflow':('logger','if (length < message_len)','if (false)'),
    'logger_consume_before_copy':('logger','kfifo_out_peek(&message_kfifo, &s_read_buf','kfifo_out(&message_kfifo, &s_read_buf'),
    'logger_fifo_unwind_leak':('logger','free_fifo:\n\tkfifo_free(&message_kfifo);','free_fifo:\n\t(void)0;'),
    'logger_last_message_unterminated':('logger','strscpy(s_last_message, s_write_buf.m_camlog_message, sizeof(s_last_message));','memcpy(s_last_message, s_write_buf.m_camlog_message, strlen(s_write_buf.m_camlog_message));'),
    'messenger_write_bound_lost':('messenger','length <= 4 || length >= MSG_LEN_MAX','length <= 4'),
    'messenger_sender_overflow':('messenger','sender_len >= READER_ID_MAX ||','false ||'),
    'messenger_receiver_overflow':('messenger','receiver_len >= READER_ID_MAX','false'),
    'messenger_duplicate_reader_zero':('messenger','if (reader_idx < READER_COUNT_MAX)\n\t\treturn CREATE_READER_ERROR_EXIST;','if (reader_idx && reader_idx < READER_COUNT_MAX)\n\t\treturn CREATE_READER_ERROR_EXIST;'),
    'messenger_reader_zero_release_lost':('messenger','if (reader_idx < READER_COUNT_MAX) {','if (reader_idx && reader_idx < READER_COUNT_MAX) {'),
    'messenger_unregistered_read_lock_leak':('messenger','idx == READER_COUNT_MAX)\n\t\tgoto out;','idx == READER_COUNT_MAX)\n\t\treturn ret;'),
    'messenger_copy_fault_consumes':('messenger','ret = READER_ERROR_COPY_TO_USER_FAILED;','reader->front = (reader->front + 1) % MSG_COUNT_MAX;\n\t\tret = READER_ERROR_COPY_TO_USER_FAILED;'),
    'messenger_short_read_consumes':('messenger','if (length < len)','if (false)'),
    'messenger_unpublished_open':('messenger','return cameramsger_is_ready() ? 0 : -ENODEV;','return 0;'),
    'scheduler_duplicate_hook_retry':('scheduler','if (!(registered_hooks & BIT(0)))','if (true)'),
    'scheduler_partial_hook_callbacks_active':('scheduler','!READ_ONCE(sched_ready) ||','false ||'),
    'scheduler_failed_init_dangling_hooks':('scheduler','if (ret && !registered_hooks)','if (ret)'),
    'scheduler_cpu_array_bound_lost':('scheduler','cpu >= CPU_NR ||','false ||'),
    'scheduler_overrides_core_denial':('scheduler','if (effect_cpu_action(rcu_dereference(csdata), p, cpu))','*allowed = true;\n\tif (effect_cpu_action(rcu_dereference(csdata), p, cpu))'),
    'scheduler_uninitialized_mask':('scheduler','data = kzalloc(sizeof(*data), GFP_KERNEL);','data = kmalloc(sizeof(*data), GFP_KERNEL);'),
    'pid_lookup_reference_lost':('scheduler','get_task_struct(task);','(void)0;'),
    'pid_lookup_allocation_error_deref':('scheduler','candidate && !strncmp','!strncmp'),
   };report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    src=generated[group];assert before in src;report['negative_controls'][name]=execute(name,src.replace(before,after,1),True)
 pins=json.loads((H/'camera-aux-imports.json').read_text());report['source_sha256']={x['path']:hashlib.sha256((root/x['path']).read_bytes()).hexdigest() for x in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('camera_*'))};report['limits']='Production source with mocked device/char/FIFO/user-copy/hooks and pthread RCU/mutex services; not actual VFS, scheduler, hardware or camera-stack validation.';a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
