from pathlib import Path
import sys,subprocess,tempfile,shutil,json
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'audio_machine'))
from verify_smartpa import function
src='drivers/input/fingerprint/mi_fp/fp_platform.c'
fixture=r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
struct fp_device { bool irq_request; int irq_enabled,irq_num; };
static int disables,enables;
static void disable_irq(int n) { assert(n==7);disables++; }
static void enable_irq(int n) { assert(n==7);enables++; }
/* SOURCE */
int main(void) {
 struct fp_device p={.irq_num=7};
 assert(fp_disable_irq(&p)==0);assert(!disables);
 assert(fp_enable_irq(&p)==-ENODEV);assert(!enables);
 p.irq_request=true;
 assert(fp_disable_irq(&p)==0);assert(!disables);
 assert(fp_enable_irq(&p)==0);assert(enables==1 && p.irq_enabled);
 assert(fp_enable_irq(&p)==0);assert(enables==1);
 assert(fp_disable_irq(&p)==0);assert(disables==1 && !p.irq_enabled);
 assert(fp_disable_irq(&p)==0);assert(disables==1);
}
"""
result=[]
for version in ['candidate','before']:
 s=(HERE/version/src).read_text();code=fixture.replace('/* SOURCE */',function(s,'fp_disable_irq')+'\n'+function(s,'fp_enable_irq'))
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'irq.c';p.write_text(code);exe=Path(tmp)/'irq';subprocess.run([shutil.which('cc'),'-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True,capture_output=True);r=subprocess.run([str(exe)],capture_output=True)
 assert (r.returncode==0)==(version=='candidate')
 result.append({'version':version,'exit':r.returncode})
report={'status':'PASS','results':result}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
