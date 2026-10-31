#!/usr/bin/env python3
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
H=Path(__file__).resolve().parent

def function(s,n):
 s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
 m=re.search(r'(?m)^(?:static )?(?:inline )?[\w \n*]+\b'+re.escape(n)+r'\s*\([^;#]*?\)\n\{',s)
 if not m:raise ValueError(n)
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+'\n'

def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--negative',action='store_true');p.add_argument('--output',required=True,type=Path);a=p.parse_args();root=a.workspace/a.source_dir;s=(root/'drivers/leds/leds-qti-flash.c').read_text()
 prefix=s[s.index('#define FLASH_LED_REVISION1'):s.index('static int timeout_to_code')]
 common=(H/'tests/flash_fake.h').read_text().replace('/* CONSTANTS AND TYPES */',prefix)
 names=re.findall(r'(?m)^(?:static )?(?:int|bool|void|ssize_t|enum led_brightness|enum hrtimer_restart|irqreturn_t|struct flash_switch_data \*)\s*([a-zA-Z_]\w*)\s*\(',s[:s.index('static int parse_switch_device')])
 names=[n for n in names if n not in ['qti_flash_lock','qti_flash_time_valid']]
 # Retain the source order and real attributes/ops declarations between functions.
 controls=s[s.index('static int timeout_to_code'):s.index('static int parse_switch_device')]
 generated={'controls':common+'\n'+controls+'\n'+(H/'tests/flash_controls.c').read_text()}
 generated['device']=common+'\n'+(H/'tests/flash_device_fake.h').read_text()+'\n'+s[s.index('static int timeout_to_code'):s.index('static const struct of_device_id')]+'\n'+(H/'tests/flash_device.c').read_text()
 generated['races']=common+'\n'+controls+'\n'+function(s,'qti_flash_led_stop')+'\n'+(H/'tests/flash_controls.c').read_text().split('int main(void)')[0]+'\n'+(H/'tests/flash_races.c').read_text()
 report={'tests':{}}
 with tempfile.TemporaryDirectory(prefix='flash-test-') as d:
  tmp=Path(d)
  def execute(name,src,negative=False):
   f=tmp/(name+'.c');f.write_text(src);exe=f.with_suffix('');subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-unused-variable','-Wno-missing-field-initializers','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie',str(f),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40)
   if negative:
    assert r.returncode,(name,'mutation survived');diag=next((l for l in r.stderr.splitlines() if 'Assertion' in l or 'runtime error:' in l or 'ERROR: AddressSanitizer' in l),'');assert diag,(name,r.stderr);return {'rejected':True,'diagnostic':diag}
   assert not r.returncode,(name,r.stderr);return json.loads(r.stdout)
  for name,src in generated.items():report['tests'][name]=execute(name,src)
  if a.negative:
   mutations={
    'canceling_worker_rearms':('controls','if (snode->canceling)','if (false)'),
    'timer_does_io':('controls','schedule_work(&snode->off_work);','qti_flash_switch_disable(snode);'),
    'queued_on_not_drained':('controls','cancel_work_sync(&snode->on_work);','(void)0;'),
    'queued_brightness_not_drained':('controls','qti_flash_flush_brightness(led);','(void)0;'),
    'timeout_underflow':('controls','timeout < SAFETY_TIMER_MIN_TIMEOUT_MS','false'),
    'timeout_rounded_above_limit':('controls','timeout / SAFETY_TIMER_STEP_SIZE - 1','DIV_ROUND_CLOSEST(timeout, SAFETY_TIMER_STEP_SIZE) - 1'),
    'timeout_u64_overflow':('controls','return value <= (u64)KTIME_MAX / NSEC_PER_MSEC;','return true;'),
    'ten_ms_strobe_rejected':('controls','if (state && fnode->enabled)','if (!fnode->duration) return -EINVAL;\n\tif (state && fnode->enabled)'),
    'symmetry_interior_pointer_oob':('controls','led->snode[i].off_time_ms);','snode[i].off_time_ms);'),
    'native_battery_error_hidden':('controls','if (rc < 0) {\n\t\tpr_err("Failed to get battery resistance','if (false) {\n\t\tpr_err("Failed to get battery resistance'),
    'secure_current_output_missing':('controls','*max_current_ma = MAX_FLASH_CURRENT_MA;\n\t\treturn 0;\n\t}\n\n\trc = qti_flash_led_calc_max_avail_current','(void)0;\n\t\treturn 0;\n\t}\n\n\trc = qti_flash_led_calc_max_avail_current'),
    'off_timer_retry_lost':('controls','hrtimer_start(&snode->off_timer, ms_to_ktime(100), HRTIMER_MODE_REL);','(void)0;'),
    'channel_id_truncation':('device','if (val >= led->max_channels)','if (false)'),
    'strobe_selector_truncation':('device','if (val > HW_STROBE)','if (false)'),
    'paired_current_count_unchecked':('device','of_property_count_u32_elems(node, "xiaomi,time-for-currents") != count','false'),
    'configured_duration_limit_lost':('device','if (duration > fnode->max_duration)','if (false)'),
    'gpio_defer_ignored':('device','if (rc == -EPROBE_DEFER)\n\t\t\t\treturn rc;','if (rc == -EPROBE_DEFER)\n\t\t\t\tcontinue;'),
    'irq_defer_ignored':('device','if (led->all_ramp_up_done_irq == -EPROBE_DEFER)\n\t\treturn -EPROBE_DEFER;','if (false)\n\t\treturn -EPROBE_DEFER;'),
    'node_reference_lost':('device','of_node_get(temp));','temp);'),
    'battery_reference_leak':('device','power_supply_put(led->batt_psy);','(void)0;'),
    'controller_registry_entry_leaked':('device','list_del_init(&led->entry);','(void)0;'),
    'unpublished_and_stopping_gate_lost':('device','if (!led->ready || led->stopping)','if (false)'),
    'post_stop_operation_gate_lost':('races','if (!led->ready || led->stopping)','if (false)'),
   }
   report['negative_controls']={}
   for name,(group,before,after) in mutations.items():
    src=generated[group];assert before in src;report['negative_controls'][name]=execute(name,src.replace(before,after,1),True)
 pins=json.loads((H/'flash-led-imports.json').read_text());report['source_sha256']={x['path']:hashlib.sha256((root/x['path']).read_bytes()).hexdigest() for x in pins};report['harness_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((H/'tests').glob('flash_*'))};report['limits']='Extracted production source with mocked kernel, register I/O, LED core and pthread mutexes; no hardware, actual IRQ/workqueue/device-core or electrical validation.';a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['tests'],indent=2))
if __name__=='__main__':main()
