"""Audit and package USB presence and charging startup changes."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
R=Path(__file__).resolve().parents[2]
W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
OLD=W/'artifacts/20261102T000429634944Z'
ROM=Path('/home/loukious/Android/custom-rom')
P=W/'packaging'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=argparse.ArgumentParser();a.add_argument('artifact',type=Path);args=a.parse_args();A=args.artifact
old={p.name:sha(p) for p in (OLD/'platform').glob('*.ko')};new={p.name:sha(p) for p in (A/'platform').glob('*.ko')}
assert old.keys()==new.keys()
changed=sorted(n for n in old if old[n]!=new[n])
assert changed==['dwc3-msm.ko','mca_path_control.ko','phy-msm-snps-eusb2.ko'],changed
for p in (A/'gki').iterdir():
 if p.is_file():assert sha(p)==sha(OLD/'gki'/p.name),p.name
for p in (A/'devicetrees/checked').glob('*.dtb'):assert sha(p)==sha(OLD/'devicetrees/checked'/p.name),p.name
assert sha(A/'devicetrees/dtbo.img')==sha(OLD/'devicetrees/dtbo.img')
source=Path(__file__).resolve().parent/'candidate/drivers/power/supply/mca/mca_hardware_monitor/mca_path_control.c'
staged=json.loads((A/'inputs/staged-files.json').read_text());assert sha(source)==staged['drivers/power/supply/mca/mca_hardware_monitor/mca_path_control.c']
inv=json.loads((P/'module-inventory-20261102-microphone-vibrator.json').read_text());inv['previous_basis']=inv['basis'];inv['basis']=str(A)
for board in inv['boards']:assert inv['boards'][board]['sha256']==sha(A/'devicetrees/checked'/board)
I=P/'module-inventory-20261102-path-usb.json';assert not I.exists();I.write_text(json.dumps(inv,indent=2)+'\n')
def run(script,*args):subprocess.run([sys.executable,str(R/script),*[str(p) for p in args]],check=True)
first=P/'first-stage-20261102-path-usb';bundle=P/'experimental-bundle-20261102-path-usb';images=P/'experimental-images-20261102-path-usb';serial=P/'experimental-images-20261102-path-usb-serial'
run('assemble_first_stage.py',A,ROM/'out/target/product/onyx','--inventory',I,'--output',first)
run('assemble_module_bundle.py',A,ROM/'out/target/product/onyx','--first-stage',first,'--output',bundle)
run('build_experimental_images.py',A,bundle,P/'baseline-20261101T011044Z',ROM,'--output',images,'--partition-sizes',W/'device-backups/5493f3c6-20261101T160034Z/target-dlkm-sizes.json','--compact-system-erofs','--diagnostic-boot','--boot-checkpoints','--funnel-checkpoints','--gpi-checkpoints')
run('build_serial_module_diagnostic.py',images,P/'serial-module-loaders-20261101-v2',ROM,'--output',serial)
previous=json.loads((P/'experimental-images-20261102-microphone-vibrator-serial/report.json').read_text());candidate=json.loads((serial/'report.json').read_text())
assert candidate['loader_manifest_sha256']==previous['loader_manifest_sha256'] and candidate['bootconfig']==previous['bootconfig']
for name in ['boot','dtbo']:assert candidate['images'][name]['sha256']==previous['images'][name]['sha256'],name
build=json.loads((A/'report.json').read_text())
report={'status':'OFFLINE PASS; not installed','artifact':str(A),'installed_comparison':str(OLD),'changed_modules':changed,'platform_modules':len(build['module_verification']),'crc_checks':sum(m['imports_checked'] for m in build['module_verification']),'gki_unchanged':True,'device_trees_unchanged':True,'working_microphone_and_vibrator_modules_unchanged':True,'unchanged_images':['boot','dtbo'],'system_dlkm_contents_unchanged':True,'loader_identity_and_bootconfig_unchanged':True,'packaged_modules':json.loads((bundle/'report.json').read_text())['modules'],'images':str(serial),'image_report_sha256':sha(serial/'report.json'),'limits':'Hardware boot, charging-path initialization, USB and existing working hardware must be checked after installation. Native unsupported gate table requests still fail.'}
(Path(__file__).resolve().parent/'build-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
