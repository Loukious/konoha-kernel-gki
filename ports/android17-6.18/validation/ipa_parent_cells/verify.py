#!/usr/bin/env python3
"""Read-only recipe/archive verification; all compilation and overlay outputs stay here."""
from pathlib import Path
import argparse,struct,json,re,subprocess,hashlib,sys
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--workspace',type=Path,default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031'));parser.add_argument('--recipe',type=Path,default=Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18'));parser.add_argument('--archive',type=Path,default=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/artifacts/20261101T224209314131Z'));args=parser.parse_args()
sys.path.insert(0,str(args.recipe));from build_devicetrees import read_fdt
PARENT='/soc/qcom,ipa@3e00000'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def words(v):assert len(v)%4==0;return list(struct.unpack('>'+'I'*(len(v)//4),v))
def parent(n):return n.rsplit('/',1)[0] or '/'
def count(t,n,p):
 while p not in t[n]:
  assert n!='/';n=parent(n)
 return words(t[n][p])[0]
def number(v):
 n=0
 for x in v:n=(n<<32)|x
 return n

def audit(t):
 ph={words(p['phandle'])[0]:n for n,p in t.items() if 'phandle' in p};reservations=[];nodes=set();pool=[];omissions=[]
 for n,p in t.items():
  if 'iommu-addresses' not in p:continue
  v=words(p['iommu-addresses']);pos=0
  while pos<len(v):
   assert v[pos] in ph,(n,v[pos]);target=ph[v[pos]];par=parent(target);na=count(t,par,'#address-cells');ns=count(t,par,'#size-cells');assert 0<na<=2 and 0<ns<=2
   end=pos+1+na+ns;assert end<=len(v),(n,pos,end,len(v));length=number(v[pos+1+na:end]);assert length>0
   start=number(v[pos+1:pos+1+na]);translate=start;bus=par;chain=[]
   while True:
    assert '#address-cells' in t[bus] and '#size-cells' in t[bus],(n,target,bus)
    assert 'dma-ranges' not in t[bus] and 'dma-parent' not in t[bus],bus
    assert b'dma-mem\0' not in t[bus].get('interconnect-names',b''),bus
    assert t[bus].get('device_type') not in [b'pci\0',b'pciex\0'],bus
    chain.append(bus);nodes.add(bus)
    if bus=='/':break
    nextbus=parent(bus);translate &= (1 << (32*count(t,nextbus,'#address-cells')))-1;bus=nextbus
   nodes.add(target)
   reservations.append({'owner':n,'target':target,'parent':par,'address_cells':na,'size_cells':ns,'tuple':v[pos:end],'address':start,'length':length,'translated':translate,'dma_ancestors':chain})
   pos=end
 for n,p in t.items():
  if 'qcom,iommu-dma-addr-pool' not in p:continue
  par=parent(n);na=count(t,par,'#address-cells');ns=count(t,par,'#size-cells');v=words(p['qcom,iommu-dma-addr-pool']);assert len(v)==na+ns
  start=number(v[:na]);length=number(v[na:]);assert length>0 and start>0 and start+length < 1<<64
  assert na==ns==1 and v==[0x20000000,0x20000000],(n,v)
  if '#address-cells' not in t[par] or '#size-cells' not in t[par]:omissions.append(par)
  pool.append({'target':n,'parent':par,'address_cells':na,'size_cells':ns,'tuple':v,'address':start,'length':length,'reserved_complement':[[0,start],[start+length,(1<<64)-(start+length)]]});nodes.add(n)
  while n!='/':n=parent(n);nodes.add(n)
 assert len(reservations)==47 and len(pool)==2
 assert not any(r['target'].startswith(PARENT+'/') for r in reservations)
 return reservations,pool,sorted(nodes,key=lambda n:(n.count('/'),n)),sorted(set(omissions))

report={'status':'PASS','archive':str(args.archive),'source_functions':json.loads((HERE/'extraction.json').read_text()),'boards':{},'limits':['Exact extracted OF translation and vendor pool/reservation helpers; lightweight device/list/allocation/parent stubs, all tested ancestors validated as generic with no dma-ranges, dma-mem redirection, or indirect I/O.','Generic-bus numeric widening/narrowing is executed by exact pinned translation functions, not an identity-translation mock.','No kernel/module or shared recipe mutation; host overlay/compilation only. Hardware boot is untested.']}
compiler=args.workspace/'prebuilts/clang/host/linux-x86/clang-r596125/bin/clang'
for board in ['tuna.dtb','tuna7.dtb']:
 old=args.archive/'devicetrees/checked'/board;new=HERE/board
 t=read_fdt(old);u=read_fdt(new);expected={n:dict(p) for n,p in t.items()};expected[PARENT].update({'#address-cells':struct.pack('>I',1),'#size-cells':struct.pack('>I',1)});assert expected==u
 rb,pb,nb,om=audit(t);ra,pa,na,oa=audit(u);assert (rb,pb,nb)==(ra,pa,na);assert om==[PARENT] and oa==[]
 assert all(t[n]['iommu-addresses']==u[n]['iommu-addresses'] for n in t if 'iommu-addresses' in t[n])
 idx={n:i for i,n in enumerate(na)}
 def fixture(tree,warning_count):
  text='static struct device_node nodes[]={\n'
  for n in na:
   props=tree[n];address=words(props['#address-cells'])[0] if '#address-cells' in props else -1;size=words(props['#size-cells'])[0] if '#size-cells' in props else -1;pool=props.get('qcom,iommu-dma-addr-pool',b'')
   parentexpr='NULL' if n=='/' else '&nodes[%d]'%idx[parent(n)]
   poolwords=[int.from_bytes(pool[i:i+4],'little') for i in range(0,len(pool),4)]
   text+='{.parent=%s,.address_cells=%d,.size_cells=%d,.pool={%s},.pool_len=%d},\n'%(parentexpr,address,size,','.join('0x%xU'%x for x in poolwords) or '0',len(pool))
  text+='};\nstruct reservation_fixture {struct device_node *dev;__be32 cells[4];int na,ns;u64 translated,length;};\nstatic struct reservation_fixture reservations[]={\n'
  for r in ra:
   vals=r['tuple'][1:];be=[int.from_bytes(struct.pack('>I',x),'little') for x in vals]
   text+='{.dev=&nodes[%d],.cells={%s},.na=%d,.ns=%d,.translated=0x%xULL,.length=0x%xULL},\n'%(idx[r['target']],','.join('0x%xU'%x for x in be),r['address_cells'],r['size_cells'],r['translated'],r['length'])
  text+='};\nstruct pool_fixture {struct device_node *dev;u64 start,length;};\nstatic struct pool_fixture pool_fixtures[]={\n'
  for r in pa:text+='{&nodes[%d],0x%xULL,0x%xULL},\n'%(idx[r['target']],r['address'],r['length'])
  text+='};\nstatic unsigned int expected_warnings=%d;\n'%warning_count
  return text
 cases={'baseline_inherited_cells':(t,2,True),'correct_explicit_cells':(u,0,True)}
 for name,change in [('missing_address_cells',{'#address-cells':None}),('missing_size_cells',{'#size-cells':None}),('wrong_address_width',{'#address-cells':struct.pack('>I',2)}),('wrong_size_width',{'#size-cells':struct.pack('>I',2)})]:
  mutant={n:dict(p) for n,p in u.items()}
  for k,v in change.items():
   if v is None:mutant[PARENT].pop(k)
   else:mutant[PARENT][k]=v
  cases[name]=(mutant,0,False)
 mutant={n:dict(p) for n,p in u.items()};mutant[PARENT+'/ipa_smmu_ap']['qcom,iommu-dma-addr-pool']=mutant[PARENT+'/ipa_smmu_ap']['qcom,iommu-dma-addr-pool'][:4];cases['truncated_pool_tuple']=(mutant,0,False)
 checks={}
 malformed_checks={}
 owner=ra[0]['owner']
 for name in ['truncated_iommu_tuple','invalid_iommu_phandle','zero_iommu_reservation_length']:
  malformed={n:dict(p) for n,p in u.items()};raw=bytearray(malformed[owner]['iommu-addresses'])
  if name=='truncated_iommu_tuple':raw=raw[:-4]
  elif name=='invalid_iommu_phandle':raw[:4]=struct.pack('>I',0xffffffff)
  else:
   first=ra[0];begin=4*(1+first['address_cells']);raw[begin:begin+4*first['size_cells']]=bytes(4*first['size_cells'])
  malformed[owner]['iommu-addresses']=bytes(raw)
  try:audit(malformed)
  except (AssertionError,KeyError) as error:malformed_checks[name]={'rejected':True,'reason':str(error)}
  else:raise AssertionError('malformed global reservation accepted: '+name)
 for name,(tree,warnings,success) in cases.items():
  (HERE/'fixtures.inc').write_text(fixture(tree,warnings));binary=HERE/(board+'.'+name)
  command=[str(compiler),'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(HERE/'harness.c'),'-o',str(binary)]
  compiled=subprocess.run(command,capture_output=True,text=True);assert compiled.returncode==0,compiled.stderr
  result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10);assert (result.returncode==0)==success,(name,result.stdout,result.stderr)
  (HERE/(board+'.'+name+'.txt')).write_text(result.stdout+result.stderr)
  checks[name]={'exit_code':result.returncode,'stdout':result.stdout.strip(),'stderr':result.stderr.strip()[:300]}
 report['boards'][board]={'old_sha256':digest(old),'new_sha256':digest(new),'only_changes':[PARENT+'/#address-cells=1',PARENT+'/#size-cells=1'],'all_iommu_address_tuples':ra,'qcom_pools':pa,'before_missing_pool_parents':om,'after_missing_pool_parents':oa,'compiled_checks':checks,'global_reservation_negative_controls':malformed_checks}
 print('PASS',board,'47 exact OF translations, 2 IPA pools/complements unchanged; five regressions rejected')
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
