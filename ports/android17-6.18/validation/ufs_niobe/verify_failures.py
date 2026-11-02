#!/usr/bin/env python3
"""Execute selected production PHY functions with mocked hardware services."""
import argparse, hashlib, json, re, subprocess, tempfile
from pathlib import Path
H=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--cc',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
def function(text,name):
    m=re.search(r'(?m)^[^\n;]*\b'+name+r'\([^;{}]*\)\s*\{',text)
    assert m,name
    depth=1;pos=m.end()
    while depth:
        if text[pos]=='{':depth+=1
        elif text[pos]=='}':depth-=1
        pos+=1
    return text[m.start():pos]
base=Path('drivers/phy/qualcomm')
files={base/'phy-qcom-ufs.c':'ufs_qcom_phy_generic_probe',base/'phy-qcom-ufs-qmp-v4-niobe.c':'ufs_qcom_phy_qmp_v4_phy_calibrate'}
body='\n'.join(function((a.source/n).read_text(),f) for n,f in files.items())
mutations={'lost_error':('return err ? ERR_PTR(err) : generic_phy;','return generic_phy;'),'lost_errno':('ERR_PTR(err)','ERR_PTR(-EIO)'), 'ignored_submode':('submode != UFS_QCOM_PHY_SUBMODE_G4','false && submode != UFS_QCOM_PHY_SUBMODE_G4')}
report={'status':'PASS','source_sha256':{str(n):hashlib.sha256((a.source/n).read_bytes()).hexdigest() for n in files},'controls':{},'limits':'Selected production functions with mocked hardware/framework services; no actual storage, PHY timing or kernel concurrency validation.'}
with tempfile.TemporaryDirectory(prefix='ufs-niobe-') as tmp:
    for label in ['production',*mutations]:
        code=body
        if label in mutations:
            old,new=mutations[label];assert old in code;code=code.replace(old,new,1)
        src=Path(tmp)/(label+'.c');exe=src.with_suffix('')
        src.write_text((H/'tests/failures.c').read_text().replace('/* PRODUCTION_FUNCTIONS */',code))
        subprocess.run([a.cc,'-std=gnu11','-Wall','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-g',str(src),'-o',str(exe)],check=True,capture_output=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True)
        if label=='production':assert r.returncode==0,r.stderr
        else:
            assert r.returncode!=0,label
            report['controls'][label]={'compiled':True,'rejected':True}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print('PASS production failure paths; three compiled regressions rejected')
