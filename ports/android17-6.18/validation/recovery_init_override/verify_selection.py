"""Exercise the production init script-selection block in both build modes."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
V=Path(__file__).resolve().parent
prefix=r"""
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
struct ActionManager{};struct ServiceList{};
static std::string configured;
static bool serial,missing;
static std::vector<std::string> calls,late_import_paths;
static std::string GetProperty(const char *name,const char *fallback){assert(std::string(name)=="ro.boot.init_rc");return configured.empty()?fallback:configured;}
static bool GetBoolProperty(const char *name,bool fallback){assert(std::string(name)=="ro.boot.konoha_serial_modules"&&!fallback);return serial;}
struct Parser{bool ParseConfig(const std::string& p){calls.push_back(p);return !(missing&&p==configured);}};
static Parser CreateParser(ActionManager&,ServiceList&){return {};}
"""
suffix=r"""
static void reset(){calls.clear();late_import_paths.clear();missing=false;}
static void defaults(){assert(calls==std::vector<std::string>({"/system/etc/init/hw/init.rc","/system/etc/init","/system_ext/etc/init","/vendor/etc/init","/odm/etc/init","/product/etc/init"}));}
int main(){ActionManager a;ServiceList s;int cases=0;
 for(bool diagnostic:{false,true}){
  reset();serial=diagnostic;configured="";LoadBootScripts(a,s);defaults();cases++;
  reset();serial=diagnostic;configured="/metadata/ota/konoha-serial-20261101-v2/init.rc";LoadBootScripts(a,s);
#ifdef RECOVERY
  if(diagnostic)defaults();else assert(calls==std::vector<std::string>({configured}));
#else
  assert(calls==std::vector<std::string>({configured}));
#endif
  cases++;
  reset();missing=true;LoadBootScripts(a,s);
#ifdef RECOVERY
  if(diagnostic){defaults();assert(late_import_paths.empty());}else assert(calls==std::vector<std::string>({configured}));
#else
  assert(calls==std::vector<std::string>({configured}));
#endif
  cases++;
  reset();configured="/custom-recovery-init.rc";serial=false;LoadBootScripts(a,s);assert(calls==std::vector<std::string>({configured}));cases++;
 }
 printf("PASS: %d production init-selection cases\n",cases);return 0;
}
"""
def run(source,recovery):
 text=source.read_text();begin=text.index('static void LoadBootScripts(');brace=text.index('{',begin);end=brace+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 block=text[begin:end]+'\n'
 with tempfile.TemporaryDirectory(prefix='onyx-init-selection-') as tmp:
  d=Path(tmp);f=d/'test.cpp';f.write_text(prefix+block+suffix)
  compile=subprocess.run(['c++','-std=c++20','-O1','-g','-fsanitize=address,undefined',*(['-DRECOVERY'] if recovery else []),str(f),'-o',str(d/'test')],capture_output=True,text=True)
  assert compile.returncode==0,compile.stderr
  proc=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=15)
 return {'returncode':proc.returncode,'stdout':proc.stdout,'stderr':proc.stderr,'extracted_selection_sha256':hashlib.sha256(block.encode()).hexdigest()}
results={}
for recovery in [False,True]:
 result=run(V/'candidate/init.cpp',recovery);assert result['returncode']==0,result;results['recovery' if recovery else 'normal']=result
old=run(V/'before/init.cpp',True);assert old['returncode']!=0 and 'Assertion' in old['stderr'],old
report={'status':'PASS','candidate':results,'before_rejected':old,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Production init-selection block compiled in both modes; mocked property/config parser records selected paths. Full native RC syntax/selection and device recovery behavior checked separately.'}
(V/'selection-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
