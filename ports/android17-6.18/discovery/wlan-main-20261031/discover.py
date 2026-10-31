#!/usr/bin/env python3
"""Read native WLAN receipts and report exact source/flag/header closure; no staging."""
from pathlib import Path
import collections,hashlib,json,os,re,shlex,subprocess
H=Path(__file__).resolve().parent;W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031');P=Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18');R=Path('/home/loukious/Android/custom-rom/kernel/xiaomi/sm8735-modules');V=R/'qcom/opensource/wlan';Q=V/'qcacld-3.0';S=W/'baseline/kernel/xiaomi/onyx-wlan';REV='6cc986b6e08783b8a14b155c1531c86959a1398a';MOD='qca_cld3_wcn7750'
assert subprocess.check_output(['git','-C',str(S),'rev-parse','HEAD'],text=True).strip()==REV
central=json.loads((P/'imports.json').read_text());central_paths={i['path']:i for i in central}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,obj):(H/n).write_text(json.dumps(obj,indent=2)+'\n')
def tree(repo,rev):
 out={}
 for item in subprocess.check_output(['git','-C',str(repo),'ls-tree','-r','-z',rev]).split(b'\0'):
  if not item:continue
  m,path=item.decode().split('\t',1);mode,kind,blob=m.split();out[path]=(blob,mode,kind)
 return out
wlan_tree=tree(S,REV);vendor_rev=subprocess.check_output(['git','-C',str(W/'baseline/kernel/xiaomi/sm8735-modules'),'rev-parse','HEAD'],text=True).strip();vendor_tree=tree(W/'baseline/kernel/xiaomi/sm8735-modules',vendor_rev)
def canonical(path):
 # Native Kbuild receipts contain both absolute and ../../sm8735-modules paths.
 if 'sm8735-modules/' in path:
  rel=path.split('sm8735-modules/',1)[1]
  normalized=(R/rel).resolve(strict=False)
  if normalized.is_relative_to(V):return 'wlan',normalized.relative_to(V).as_posix()
  if normalized.is_relative_to(R):return 'vendor',normalized.relative_to(R).as_posix()
 if str(V) in path:return 'wlan',Path(path).resolve().relative_to(V).as_posix()
 return 'kernel',path
objects=[];dep_files={};cmd_hashes={};flag_sets={};config_lookups=set();include_groups={};missing_cmd=[];membership=[]
raw_objects=(Q/(MOD+'.mod')).read_text().splitlines()
for obj in raw_objects:
 op=Path(obj);cp=op.parent/('.'+op.name+'.cmd')
 if not cp.exists():missing_cmd.append(str(cp));continue
 text=cp.read_text();cmd_hashes[str(cp)]=hashlib.sha256(text.encode()).hexdigest();saved=text.splitlines()[0].split(' := ',1)[1];tokens=shlex.split(saved)
 source=next(line.split(' := ',1)[1] for line in text.splitlines() if line.startswith('source_'))
 category,path=canonical(source);assert category=='wlan',(source,category)
 membership.append({'object':obj,'source_receipt':source,'path':path,'command_receipt':str(cp),'command_sha256':cmd_hashes[str(cp)]})
 flags=[t for t in tokens if t.startswith('-D') and not t.startswith(('-DKBUILD_BASENAME=','-DKBUILD_MODNAME=','-D__KBUILD_MODNAME='))]
 fhash=hashlib.sha256(json.dumps(flags).encode()).hexdigest();flag_sets.setdefault(fhash,{'defines_in_order':flags,'objects':[]})['objects'].append(path)
 includes=[t[2:] for t in tokens if t.startswith('-I')];ihash=hashlib.sha256(json.dumps(includes).encode()).hexdigest();include_groups.setdefault(ihash,{'include_dirs_in_order':includes,'objects':[]})['objects'].append(path)
 deps=text.split('deps_',1)[1].split(' := ',1)[1].split('\n\n',1)[0]
 for line in deps.splitlines():
  d=line.strip().removesuffix('\\').strip()
  if not d:continue
  if d.startswith('$(wildcard '):config_lookups.add(d);continue
  cat,dp=canonical(d);dep_files.setdefault((cat,dp),set()).add(path)
assert len(membership)==len(raw_objects),(len(membership),len(raw_objects),missing_cmd)
paths=set(i['path'] for i in membership);native_only=[];pins=[];existing=[];kernel_deps=[];vendor_new=[];aliases=[]
for (cat,path),users in sorted(dep_files.items()):
 if cat=='kernel':kernel_deps.append(path);continue
 src_tree=wlan_tree if cat=='wlan' else vendor_tree;project='baseline/kernel/xiaomi/onyx-wlan' if cat=='wlan' else 'baseline/kernel/xiaomi/sm8735-modules';rev=REV if cat=='wlan' else vendor_rev
 if path not in src_tree:native_only.append({'category':cat,'path':path,'referenced_by':sorted(users)});continue
 blob,mode,kind=src_tree[path];assert kind=='blob';entry={'project':project,'revision':rev,'path':path,'blob':blob,'mode':mode}
 if path in central_paths:existing.append({'pin':entry,'central_pin':central_paths[path],'referenced_by':sorted(users)})
 elif cat=='vendor':vendor_new.append(entry)
 else:paths.add(path)
for path in sorted(paths):
 if path not in wlan_tree:native_only.append({'category':'wlan_source','path':path});continue
 blob,mode,kind=wlan_tree[path];entry={'project':'baseline/kernel/xiaomi/onyx-wlan','revision':REV,'path':path,'blob':blob}
 if path in central_paths:continue
 pins.append(entry)
all_flags=set().union(*(set(v['defines_in_order']) for v in flag_sets.values()));common_flags=set.intersection(*(set(v['defines_in_order']) for v in flag_sets.values()));features={f:sorted(p for group in flag_sets.values() if f in group['defines_in_order'] for p in group['objects']) for f in sorted(all_flags) if re.search('MONITOR|INJECT|CAPTURE|CFR|WCN7750|DIRECT|WLAN_MAX_ML|WLAN_FEATURE_11BE|CFG80211|NL80211',f)}
main_cmd=Q/'core/hdd/src/.wlan_hdd_main.o.cmd';main_tokens=shlex.split(main_cmd.read_text().splitlines()[0].split(' := ',1)[1]);special_flags=[t for t in main_tokens if t in ['-w'] or t.startswith(('-Wno','-Werror','-include','-fsanitize','-fno','-std','--target'))]
configs=[p for p in sorted(wlan_tree) if p in ['qcacld-3.0/Makefile','qcacld-3.0/Kbuild','qcacld-3.0/Kconfig','qcacld-3.0/configs/wcn7750_defconfig','qcacld-3.0/configs/sun_gki_wcn7750_defconfig','qcacld-3.0/configs/sun_consolidate_wcn7750_defconfig']]
config_pins=[{'project':'baseline/kernel/xiaomi/onyx-wlan','revision':REV,'path':p,'blob':wlan_tree[p][0]} for p in configs]
module=Q/(MOD+'.ko');module_info={'path':str(module),'sha256':digest(module),'depends':subprocess.check_output(['modinfo','-F','depends',str(module)],text=True).strip(),'aliases':subprocess.check_output(['modinfo','-F','alias',str(module)],text=True).splitlines(),'vermagic':subprocess.check_output(['modinfo','-F','vermagic',str(module)],text=True).strip(),'imports':subprocess.check_output(['modprobe','--show-modversions',str(module)],text=True).splitlines()}
save('objects.json',membership);save('new-wlan-pins.json',pins);save('new-vendor-header-pins.json',vendor_new);save('existing-provider-headers.json',existing);save('unresolved-native-inputs.json',native_only);save('kernel-header-dependencies.json',kernel_deps);save('define-groups.json',flag_sets);save('include-groups.json',include_groups);save('feature-defines.json',features);save('config-pins.json',config_pins);save('config-dependencies.json',sorted(config_lookups));save('module-evidence.json',module_info)
summary={'source_revision':REV,'source_repository':str(S),'central_imports_sha256':digest(P/'imports.json'),'native_mod_sha256':digest(Q/(MOD+'.mod')),'native_main_cmd_sha256':digest(main_cmd),'object_count':len(membership),'distinct_sources':len({m['path'] for m in membership}),'new_wlan_pins':len(pins),'new_wlan_c_sources':sum(p['path'].endswith('.c') for p in pins),'new_wlan_headers':sum(p['path'].endswith('.h') for p in pins),'new_vendor_header_pins':len(vendor_new),'existing_provider_headers':len(existing),'unresolved_native_inputs':len(native_only),'kernel_header_paths':len(kernel_deps),'define_groups':len(flag_sets),'common_define_count':len(common_flags),'all_define_count':len(all_flags),'include_groups':len(include_groups),'main_compile_special_flags':special_flags,'depends':module_info['depends'],'path_mapping':'Native qcacld-3.0/cmn resolves to qca-wifi-host-cmn. Normalize symlinks and dot components; staged paths must use canonical paths plus rewritten include directories, never duplicate both cmn and qca-wifi-host-cmn.'}
save('inventory-summary.json',summary);print(json.dumps(summary,indent=2))
