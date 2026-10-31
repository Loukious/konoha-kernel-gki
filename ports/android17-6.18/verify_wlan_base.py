import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
def function(source,name):
 source=re.sub(r'/\*.*?\*/','',source,flags=re.S)
 match=re.search(r'(?m)^(?:static (?:inline )?)?[\w \n*]+\b'+name+r'\([^;]*?\)\n\{',source);assert match,name
 end=match.end();depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[match.start():end]+'\n'
def main():
 p=argparse.ArgumentParser();p.add_argument('workspace',type=Path);p.add_argument('--source-dir',default='onyx');p.add_argument('--output',type=Path,required=True);args=p.parse_args()
 imports=HERE/'wlan-base-imports.json'
 if not imports.exists():imports=HERE/'imports.json'
 paths=[i['path'] for i in json.loads(imports.read_text())];assert len(paths)==5
 sources={path:(args.workspace/args.source_dir/path).read_bytes() for path in paths}
 pre=sources['platform/cnss_prealloc/cnss_prealloc.c'].decode();nl=sources['platform/cnss_genl/cnss_nl.c'].decode()
 cases={
  'wlan_prealloc':(pre,['cnss_pool_alloc_threshold','cnss_pool_init','cnss_pool_deinit','cnss_assign_prealloc_pool','cnss_initialize_prealloc_pool','cnss_deinitialize_prealloc_pool','wcnss_check_pool_lists','wcnss_find_pool_table_slot','wcnss_free_pool_table_slot','wcnss_prealloc_get','wcnss_prealloc_put']),
  'wlan_netlink':(nl,['cld80211_pre_doit','register_cld_cmd_cb','deregister_cld_cmd_cb','cld80211_doit'])}
 tests={};fixtures={}
 with tempfile.TemporaryDirectory(prefix='onyx-wlan-base-') as temporary:
  for name,(source,functions) in cases.items():
   fixture=(HERE/'tests'/(name+'.c')).read_bytes();fixtures[name+'.c']=hashlib.sha256(fixture).hexdigest()
   code=fixture.decode().replace('/* FUNCTIONS */','\n'.join(function(source,n) for n in functions))
   if name=='wlan_prealloc':code=code.replace('/* DECLARATIONS */',source[source.index('struct cnss_pool {'):source.index('/**\n * cnss_pool_alloc_threshold')])
   c=Path(temporary)/(name+'.c');c.write_text(code);binary=Path(temporary)/name
   subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-Werror=implicit-function-declaration',str(c),'-o',str(binary)],check=True)
   r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=45);assert not r.returncode,r.stdout+r.stderr;tests[name]=json.loads(r.stdout)
 report={'tests':tests,'source_sha256':{path:hashlib.sha256(data).hexdigest() for path,data in sources.items()},'harness_sha256':fixtures,'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Production callback/pool functions and native sizing tables with mocked slab/mempool/netlink APIs and pthread SRCU. No live reserve allocator, IRQ semantics, kernel module teardown, firmware, WLAN capture/injection or phone validation.'}
 args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
