"""Audit and package the microphone/Si vibrator candidate against installed 6.18."""
from pathlib import Path
import argparse, hashlib, json, struct, subprocess, sys
H=Path(__file__).resolve().parent
RECIPE=H.parent.parent
sys.path.insert(0,str(RECIPE))
from build_devicetrees import read_fdt
ap=argparse.ArgumentParser();ap.add_argument('artifact',type=Path);a=ap.parse_args()
W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
OLD=W/'artifacts/20261101T232730636087Z'
ROM=Path('/home/loukious/Android/custom-rom')
PKG=W/'packaging'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'status':'PASS','installed':str(OLD),'candidate':str(a.artifact)}
oldmods={p.name:sha(p) for p in (OLD/'platform').glob('*.ko')}
newmods={p.name:sha(p) for p in (a.artifact/'platform').glob('*.ko')}
assert set(newmods)-set(oldmods)=={'si_haptic.ko'}
assert not set(oldmods)-set(newmods)
changes=[n for n in oldmods if oldmods[n]!=newmods[n]]
assert changes==['lpass_cdc_tx_macro_dlkm.ko'],changes
for p in (a.artifact/'gki').iterdir():
    if p.is_file():assert sha(p)==sha(OLD/'gki'/p.name),p.name
report['changed_platform_modules']=changes
report['added_platform_modules']=['si_haptic.ko']
report['gki_unchanged']=True
build=json.loads((a.artifact/'report.json').read_text())
report['platform_modules']=len(build['module_verification'])
report['crc_checks']=sum(m['imports_checked'] for m in build['module_verification'])
si=next(m for m in build['module_verification'] if m['module']=='si_haptic.ko')
assert any('silicon,sih_haptic_688X' in s for s in si['aliases'])
assert 'miev' in si['depends']
report['silicon_driver']=si
for root in [H/'candidate',RECIPE/'validation/microphone_unmute/candidate']:
    for p in root.rglob('*'):
        if p.is_file():
            rel=p.relative_to(root).as_posix()
            assert sha(W/'onyx'/rel)==sha(p),rel
            assert build['sha256']['inputs/staged-files.json'] == sha(a.artifact/'inputs/staged-files.json')
            staged=json.loads((a.artifact/'inputs/staged-files.json').read_text())
            assert staged[rel]==sha(p),rel
report['composed_source_matches_tested_snapshots']=True
inventory=json.loads((PKG/'module-inventory-20261102-kernel-warnings.json').read_text())
inventory['previous_basis']=inventory['basis'];inventory['basis']=str(a.artifact)
node='/soc/qcom,spmi@c42d000/qcom,pmk8550@0/sdam@7000/sm1510_present@5d'
for board in inventory['boards']:
    old=read_fdt(OLD/'devicetrees/checked'/board)
    new=read_fdt(a.artifact/'devicetrees/checked'/board)
    expected={n:dict(p) for n,p in old.items()}
    expected[node]['reg']=struct.pack('>II',93,2)
    assert expected==new
    inventory['boards'][board]['sha256']=sha(a.artifact/'devicetrees/checked'/board)
inventory['restored_native_driver']=inventory['remaining_modules'].pop('si_haptic')
inventory['restored_native_driver']['evidence']='Native6.6 binds5-006b; real driver compiled with OF aliases, preserved DT compatible/reg/config and firmware'
report['only_board_property_change']=node+'/reg'
report['native_dtbo_preserved']=sha(a.artifact/'devicetrees/dtbo.img')==sha(OLD/'devicetrees/dtbo.img')
assert report['native_dtbo_preserved']
inv=PKG/'module-inventory-20261102-microphone-vibrator.json'
inv.write_text(json.dumps(inventory,indent=2)+'\n')
(H/'build-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
def run(name,*args):
    subprocess.run([sys.executable,str(RECIPE/name),*[str(x) for x in args]],check=True)
first=PKG/'first-stage-20261102-microphone-vibrator'
bundle=PKG/'experimental-bundle-20261102-microphone-vibrator'
images=PKG/'experimental-images-20261102-microphone-vibrator'
serial=PKG/'experimental-images-20261102-microphone-vibrator-serial'
run('assemble_first_stage.py',a.artifact,ROM/'out/target/product/onyx','--inventory',inv,'--output',first)
run('assemble_module_bundle.py',a.artifact,ROM/'out/target/product/onyx','--first-stage',first,'--output',bundle)
run('build_experimental_images.py',a.artifact,bundle,PKG/'baseline-20261101T011044Z',ROM,'--output',images,'--partition-sizes',W/'device-backups/5493f3c6-20261101T160034Z/target-dlkm-sizes.json','--compact-system-erofs','--diagnostic-boot','--boot-checkpoints','--funnel-checkpoints','--gpi-checkpoints')
run('build_serial_module_diagnostic.py',images,PKG/'serial-module-loaders-20261101-v2',ROM,'--output',serial)
load=(bundle/'partitions/vendor_dlkm/lib/modules/modules.load')
if not load.exists():
    load=next(bundle.rglob('modules.load'))
assert any('si_haptic' in p.read_text() for p in bundle.rglob('modules.load'))
report['packaging_report']=str(serial/'report.json')
report['packaged_modules']=json.loads((bundle/'report.json').read_text())['modules']
report['status']='PASS: offline source, module, DT, image and packaging checks; not installed'
(H/'build-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
