from pathlib import Path
import hashlib,json,re,subprocess
H=Path(__file__).parent;W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031');S=W/'baseline/kernel/xiaomi/onyx-wlan';old=Path('/home/loukious/Android/custom-rom/kernel/xiaomi/konoha-abi/src/include/net/cfg80211.h');new=W/'common/include/net/cfg80211.h';hdrs={'native_rom':old.read_text(),'port_6_18':new.read_text()}
def clean(s):return re.sub(r'/\*.*?\*/','',s,flags=re.S)
def signatures(s):
 s=clean(s);s=s[s.index('struct cfg80211_ops {'):];s=s[:s.index('\n};')];out={}
 for m in re.finditer(r'\(\*(\w+)\)\(',s):
  start=m.start();start=s.rfind(';',0,start)+1;out[m.group(1)]=' '.join(re.sub(r'^#.*$','',s[start:s.index(';',m.end())+1],flags=re.M).split())
 return out
sigs={k:signatures(s) for k,s in hdrs.items()};driver=S/'qcacld-3.0/core/hdd/src/wlan_hdd_cfg80211.c';body=driver.read_text();ops=body[body.index('static struct cfg80211_ops wlan_hdd_cfg80211_ops = {'):];ops=ops[:ops.index('\n};')];assignments=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)',ops));diffs=[]
for name,fn in assignments.items():
 a,b=sigs['native_rom'].get(name),sigs['port_6_18'].get(name)
 if a!=b:diffs.append({'member':name,'driver_callback':fn,'native_rom':a,'port_6_18':b})
(H/'cfg80211-ops-diff.json').write_text(json.dumps(diffs,indent=2)+'\n')
(H/'cfg80211-header-evidence.json').write_text(json.dumps({k:{'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for k,p in [('native_rom',old),('port_6_18',new)]},indent=2)+'\n')
print(json.dumps(diffs,indent=2))
