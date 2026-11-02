from pathlib import Path
import json,sys,tempfile,subprocess,shutil
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'audio_machine'))
from verify_smartpa import function
fixture=r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
struct fp_device { bool irq_request; int irq_enabled,irq_num; };
#define IRQF_TRIGGER_RISING 1
#define IRQF_ONESHOT 2
static void fp_irq(void) {}
static int calls,fail,enables,disables;
static int request_threaded_irq(int irq, void *top, void (*fn)(void), int flags, const char *name, struct fp_device *data) {
 assert(irq==7 && !top && fn==fp_irq && flags==3 && name && data);calls++;return fail;
}
static void enable_irq(int n) { assert(n==7);enables++; }
static void disable_irq(int n) { assert(n==7);disables++; }
/* HELPERS */
static int legacy_enable(struct fp_device *fp_dev) { /* ENABLE */ }
int main(void) {
 struct fp_device p={.irq_num=7};
 assert(fp_disable_irq(&p)==0 && !disables);
 fail=-EBUSY;assert(legacy_enable(&p)==-EBUSY);
 assert(calls==1 && !p.irq_request && !p.irq_enabled && !enables);
 fail=0;assert(legacy_enable(&p)==0);
 assert(calls==2 && p.irq_request && p.irq_enabled && !enables);
 assert(legacy_enable(&p)==0 && calls==2 && !enables);
 assert(fp_disable_irq(&p)==0 && disables==1 && !p.irq_enabled);
 assert(legacy_enable(&p)==0 && enables==1 && calls==2 && p.irq_enabled);
 assert(fp_disable_irq(&p)==0 && disables==2);
 p.irq_request=false;p.irq_enabled=false;
 assert(fp_disable_irq(&p)==0 && disables==2);
 assert(legacy_enable(&p)==0 && calls==3 && p.irq_enabled);
}
"""
rel='drivers/input/fingerprint/mi_fp/fp_driver.c';plat='drivers/input/fingerprint/mi_fp/fp_platform.c'
platform=(HERE.parent/'fingerprint_irq/candidate'/plat).read_text()
results=[]
for version in ['candidate','before','mark-before-request','ignore-request-failure']:
 text=(HERE/('before' if version=='before' else 'candidate')/rel).read_text()
 enable=text.split('case FP_IOC_ENABLE_IRQ:',1)[1].split('break;',1)[0]
 enable=enable.replace('pr_debug("FP_IOC_ENABLE_IRQ ======\\n");','').strip()
 assert 'pr_debug' not in enable,enable
 enable='int retval;'+enable+'return retval;'
 helpers=function(platform,'fp_enable_irq')+'\n'+function(platform,'fp_disable_irq')
 if version!='before':helpers+='\n'+function(text,'fp_request_irq')
 if version=='mark-before-request':helpers=helpers.replace('ret = request_threaded_irq','fp_dev->irq_request = true;ret = request_threaded_irq')
 if version=='ignore-request-failure':enable=enable.replace('if (!retval)','if (1)')
 code=fixture.replace('/* HELPERS */',helpers).replace('/* ENABLE */',enable)
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'irq.c';p.write_text(code);exe=Path(tmp)/'irq';subprocess.run([shutil.which('cc'),'-Wall','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True,capture_output=True);r=subprocess.run([str(exe)],capture_output=True)
 assert (r.returncode==0)==(version=='candidate'),(version,r.stderr)
 results.append({'version':version,'exit':r.returncode})
report={'status':'PASS','results':results}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
