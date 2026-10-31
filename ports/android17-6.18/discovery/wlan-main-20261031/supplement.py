#!/usr/bin/env python3
"""Reproduce source-byte, missing-header, API-call and transport comparisons."""
from pathlib import Path
import hashlib,json,re,shlex,subprocess
H=Path(__file__).parent;W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031');S=W/'baseline/kernel/xiaomi/onyx-wlan';P=Path('/home/loukious/Android/konoha-6.18-port-control/ports/android17-6.18');R=Path('/home/loukious/Android/custom-rom/kernel/xiaomi/sm8735-modules/qcom/opensource/wlan');K=Path('/home/loukious/Android/custom-rom/kernel/xiaomi/konoha-abi/src')
def save(n,v):(H/n).write_text(json.dumps(v,indent=2)+'\n')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
pins=json.loads((H/'new-wlan-pins.json').read_text());mismatches=[];missing=[]
for pin in pins:
 p=R/pin['path']
 if not p.exists():missing.append(pin['path']);continue
 b=p.read_bytes();actual=hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
 if actual!=pin['blob']:mismatches.append({'path':pin['path'],'selected_blob':pin['blob'],'native_current_blob':actual})
save('native-source-comparison.json',{'checked':len(pins),'equal':len(pins)-len(mismatches)-len(missing),'mismatches':mismatches,'missing':missing,'limit':'Current native source-file contents are compared to selected pin; receipts prove filenames and options, not source bytes used by the past compiler.'})
central={p['path']:p for p in json.loads((P/'imports.json').read_text())};report=[]
for p in json.loads((H/'kernel-header-dependencies.json').read_text()):
 if p.startswith('../src/'):path=p[7:]
 elif '/konoha-abi/src/' in p:path=p.split('/konoha-abi/src/',1)[1]
 else:continue
 if not (W/'common'/path).exists():report.append({'path':path,'central_pin':central.get(path)})
save('missing-common-headers.json',report)
main=R/'qcacld-3.0/core/hdd/src/.wlan_hdd_main.o.cmd';args=shlex.split(main.read_text().splitlines()[0].split(' := ',1)[1]);groups=json.loads((H/'define-groups.json').read_text());common=set.intersection(*(set(g['defines_in_order']) for g in groups.values()));extra=[{'defines':sorted(set(g['defines_in_order'])-common),'objects':g['objects']} for g in groups.values() if set(g['defines_in_order'])-common]
save('compile-selection.json',{'forced_includes':[args[i+1] for i,t in enumerate(args) if t=='-include'],'per_object_extra_defines':extra,'transport_defines':[t for t in args if t.startswith('-D') and any(x in t for x in ['ICNSS','IPCI','CNSS','WCN7750'])],'all_args_main':args})
functions=['cfg80211_ch_switch_notify','cfg80211_ch_switch_started_notify','cfg80211_rx_control_port','cfg80211_rx_mgmt','cfg80211_links_removed','cfg80211_sta_opmode_change_notify','cfg80211_new_sta','cfg80211_del_sta_sinfo','cfg80211_rx_assoc_resp','cfg80211_assoc_comeback','cfg80211_roamed','cfg80211_connect_done','cfg80211_disconnected'];calls=[]
for name in functions:
 item={'name':name}
 for key,p in [('native_rom',K/'include/net/cfg80211.h'),('port_6_18',W/'common/include/net/cfg80211.h')]:
  s=re.sub(r'/\*.*?\*/','',p.read_text(),flags=re.S);m=re.search(r'(?m)^(?:static inline )?(?:void|bool|int|struct \w+ \*)\s+'+name+r'\([^;{]*[;{]',s);item[key]=' '.join(m.group().split()) if m else None
 calls.append(item)
save('cfg80211-notification-signatures.json',calls)
modules={}
for name,p in [('main',R/'qcacld-3.0/qca_cld3_wcn7750.ko'),('icnss2',Path('/home/loukious/Android/custom-rom/out/target/product/onyx/vendor_dlkm/lib/modules/icnss2.ko'))]:
 imports=subprocess.check_output(['modprobe','--show-modversions',str(p)],text=True).splitlines();modules[name]={'path':str(p),'sha256':sha(p),'depends':subprocess.check_output(['modinfo','-F','depends',str(p)],text=True).strip(),'pci_pcie_mhi_imports':[s for s in imports if re.search(r'\t(?:pci_|pcie_|mhi_)',s)]}
files=['qca-wifi-host-cmn/hif/src/ipcie/if_ipci.c','qcacld-3.0/core/pld/src/pld_ipci.c','qcacld-3.0/core/pld/src/pld_ipci.h','platform/icnss2/main.c'];refs={}
for f in files:
 s=(S/f).read_text();refs[f]={'sha256':sha(S/f),'pci_pcie_mhi_calls':[{'line':s.count('\n',0,m.start())+1,'symbol':m.group(1)} for m in re.finditer(r'\b((?:pci_|pcie_|mhi_)\w+)\s*\(',s)]}
save('transport-evidence.json',{'modules':modules,'selected_route':refs,'shared_pci_helpers':{'source':'qcacld-3.0/core/pld/src/pld_common.c','sha256':sha(S/'qcacld-3.0/core/pld/src/pld_common.c'),'lines':[2006,2033,2059,2085],'behavior':'Each shared helper calls Linux PCI config APIs only for PLD_BUS_TYPE_PCIE. PLD_BUS_TYPE_IPCI explicitly breaks without calling them. pld_common.c is linked, so references remain in the native module.'},'conclusion':'HIF_IPCI selects the integrated ICNSS2 platform route. Native main WLAN does require four generic PCI core symbols for this exact linked composition. These symbols and the IPCI name do not establish a runtime requirement for the standard PCIe root driver or Linux MHI controller; selected IPCI/ICNSS2 sources register platform callbacks and map firmware/DT MMIO.'})
print('Supplemental discovery reports complete')
