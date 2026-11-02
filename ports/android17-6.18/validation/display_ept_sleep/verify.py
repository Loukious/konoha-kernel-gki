from pathlib import Path
import json,re,tempfile,subprocess,shutil
HERE=Path(__file__).resolve().parent
rel='qcom/opensource/display-drivers/msm/sde/sde_encoder.c'
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint64_t u64;
#define USEC_PER_MSEC 1000
#define min_t(type,x,y) ((type)(x)<(type)(y)?(type)(x):(type)(y))
static unsigned tests;
static uint64_t remaining;
static void usleep_range(uint64_t lo,uint64_t hi) {
 assert(hi==remaining && lo<=hi);
 assert(hi-lo<=1000);
 if(remaining<=1000)assert(lo==0);
 else assert(lo==remaining-1000);
 tests++;
}
static void check(uint64_t timeout_us) { remaining=timeout_us;/* SOURCE */ }
int main(void) {
 for(uint64_t i=0;i<=10000;i++)check(i);
 const uint64_t more[]={16666,33333,100000,1000000,9999999,10000000};
 for(unsigned i=0;i<sizeof(more)/sizeof(more[0]);i++)check(more[i]);
 assert(tests==10007);
}
"""
results=[]
for version in ['candidate','before']:
 text=(HERE/version/rel).read_text();matches=re.findall(r'usleep_range\([^;]*timeout_us[^;]*;',text);assert len(matches)==1;statement=matches[0]
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'ept.c';p.write_text(fixture.replace('/* SOURCE */',statement));exe=Path(tmp)/'ept';r=subprocess.run([shutil.which('cc'),'-Wall','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True)
  if r.returncode:raise RuntimeError(r.stderr.decode())
  r=subprocess.run([str(exe)],capture_output=True)
 assert (r.returncode==0)==(version=='candidate'),(version,r.stderr)
 results.append({'version':version,'exit':r.returncode})
report={'status':'PASS','cases':10007,'results':results,'contract':'sleep bounds remain ordered and within EPT deadline, original backoff preserved for durations>=1ms'}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
