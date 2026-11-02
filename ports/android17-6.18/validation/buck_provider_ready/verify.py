#!/usr/bin/env python3
from pathlib import Path
import ast
import hashlib
import importlib.util
import json
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
RECIPE = HERE.parent.parent
WORK = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
REL = Path('drivers/power/supply/mca/mca_strategy/strategy_charger/mca_strategy_buckchg.c')
spec = importlib.util.spec_from_file_location('buck_extract', RECIPE / 'verify_buck.py')
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)
tree = ast.parse((RECIPE / 'verify_buck.py').read_text())
names = next(ast.literal_eval(n.value) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'names' for t in n.targets))
names.append('strategy_buckchg_set_vterm')
names.append('strategy_buckchg_process_multi_vterm')

TEST = r'''
static void test_readiness(void) {
 const int readiness[] = {-1, 0, -ENODEV, -EIO, 1};
 for (int cycle=0; cycle<500; cycle++) {
  for (unsigned j=0;j<ARRAY_SIZE(readiness);j++) {
   reset_fault();pmic_ready_status=readiness[j];
   provider_error=pmic_ready_status==1 ? 0 : -ENODEV;
   struct platform_device p={0};int before_votes=vote_calls;
   int rc=strategy_buckchg_class_probe(&p);
   if(pmic_ready_status<=0) {
    assert(rc==(pmic_ready_status==-EIO ? -EIO : -EPROBE_DEFER));
    assert(!live_voters && !live_wq && !registrations && !p.data);
    assert(vote_calls==before_votes);
   } else {
    assert(!rc && p.data && live_voters==11 && registrations==5);
    strategy_buckchg_class_remove(&p);
    assert(!live_voters && !live_wq && !registrations && !p.data);
   }
   free_info();
  }
 }
}
static void test_voltage(void) {
 struct strategy_buckchg_dev info={0};
 assert(strategy_buckchg_set_vterm(NULL,NULL,4400,"test")==-EINVAL);
 info.stopping=true;
 assert(strategy_buckchg_set_vterm(NULL,&info,4400,"test")==-ENODEV);
 info.stopping=false;info.pmic_fv_compensation=10;
 for(int voltage=2000;voltage<=6000;voltage++) {
  int before_calls=term_voltage_calls;
  assert(!strategy_buckchg_set_vterm(NULL,&info,voltage,"test"));
  assert(term_voltage_calls==before_calls+1 && term_voltage_value==voltage+10);
 }
 const int invalid[]={-1,0,1,1999,6001,INT32_MAX};
 for(unsigned j=0;j<ARRAY_SIZE(invalid);j++) {
  int before_calls=term_voltage_calls;
  assert(strategy_buckchg_set_vterm(NULL,&info,invalid[j],"test")==-ERANGE);
  assert(term_voltage_calls==before_calls);
 }
 sensor_error=3;
 assert(strategy_buckchg_set_vterm(NULL,&info,4400,"test")==-EIO);
 sensor_error=0;
}
int main(int argc,char **argv) {
 assert(argc==2);
 if(!strcmp(argv[1],"readiness"))test_readiness();
 else if(!strcmp(argv[1],"voltage"))test_voltage();
 else abort();
 puts("PASS");return 0;
}
'''

def harness(source):
    header = (WORK / 'onyx' / REL.parent / 'inc/mca_strategy_buckchg.h').read_text()
    stepper = (WORK / 'onyx/drivers/power/supply/mca/include/mca/strategy/strategy_soc_limit_stepper.h').read_text()
    decl = '\n'.join(extract.function(source, n).split('{', 1)[0].strip() + ';' for n in names)
    funcs = '\n'.join(extract.function(source, n) for n in names)
    constants = '\n'.join(line for line in source.splitlines() if re.match(r'#define (BASE_FLIP_VTERM_|VTERM_DYN_TEMP_)', line))
    adapter = '''
static int term_voltage_calls,term_voltage_value;
static int strategy_class_fg_get_fastcharge(void) { return 0; }
static int platform_class_buckchg_ops_set_term_volt(int role,int value) {
 (void)role;term_voltage_calls++;term_voltage_value=value;return 0;
}
'''
    common = (RECIPE / 'tests/buck_common.h').read_text().replace('#define strategy_buckchg_set_vterm sink_cb\n', '')
    return common.replace('/* HEADER */', header + '\n' + stepper).replace('/* DECLARATIONS */', decl).replace('/* FUNCTIONS */', constants + '\n' + adapter + funcs) + TEST

reports = {}
with tempfile.TemporaryDirectory(prefix='onyx-buck-ready-') as temp:
    temp = Path(temp)
    for variant in ['before', 'candidate']:
        source = (HERE / variant / REL).read_text()
        file = temp / (variant + '.c')
        binary = temp / variant
        file.write_text(harness(source))
        compiled = subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-pthread', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(file), '-o', str(binary)], capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(compiled.stderr)
        reports[variant] = {}
        for mode in ['readiness', 'voltage']:
            result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=30)
            reports[variant][mode] = {'returncode': result.returncode, 'stdout': result.stdout.strip(), 'stderr_excerpt': result.stderr[:700]}
            assert (result.returncode == 0) == (variant == 'candidate'), reports[variant][mode]
report = {'status': 'PASS', 'readiness_cases': 2500, 'voltage_valid_cases': 4001, 'cases': reports,
          'source_sha256': {n: hashlib.sha256((HERE / n / REL).read_bytes()).hexdigest() for n in ['before', 'candidate']},
          'limits': 'Exact production probe/stop and voltage callback under ASan/UBSan. PMIC readiness, transport error, workqueue and voter dispatch are host adapters. No live firmware readiness or charging validation.'}
(HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'status': report['status'], 'readiness_cases': report['readiness_cases'], 'voltage_valid_cases': report['voltage_valid_cases'], 'before_rejected': True}, indent=2))
