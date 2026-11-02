from pathlib import Path
import re, subprocess, tempfile, json, hashlib, argparse
H=Path(__file__).resolve().parent
def extract(text,name):
    m=re.search(r'^(?:static )?(?:int|void) '+name+r'\([^;]*?\)\n\{',text,re.M);assert m,name
    return text[m.start():text.index('\n}',m.end())+2]
ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,default=H/'candidate');ap.add_argument('--output',type=Path,default=H/'lifecycle-tests.json');ap.add_argument('--negative',action='store_true');a=ap.parse_args()
p=a.source_root/'drivers/input/misc/si_haptic/haptic.c';s=p.read_text()
names=['sih_ram_load','sih_request_ram','vibrator_init','ram_work_init','sih_cleanup','sih_i2c_probe','sih_i2c_remove']
mutations={
 'publish_before_runtime_init':('\tvibrator_init(sih_haptic);','\t/* initialization omitted */'),
 'missing_firmware_ownership':('\tatomic_inc(&sih_haptic->fw_refs);','\t/* reference omitted */'),
 'accept_error_regmap':('IS_ERR_OR_NULL(sih_haptic->regmapp.regmapping)', '!sih_haptic->regmapp.regmapping'),
 'unregister_copied_input':('input_unregister_device(sih_haptic->input_dev);','input_unregister_device(&sih_haptic->soft_frame.vib_dev);'),
}
controls={}
with tempfile.TemporaryDirectory(prefix='onyx-sih-lifecycle-') as tmp:
    for key in mutations if a.negative else ['candidate']:
        source=s
        if a.negative:
            before,after=mutations[key];assert before in source;source=source.replace(before,after)
        code=(H/'lifecycle.c').read_text().replace('/* SOURCE */','\n'.join(extract(source,n) for n in names))
        pth=Path(tmp)/'test.c';pth.write_text(code);binary=pth.with_suffix('')
        r=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Werror=implicit-function-declaration',str(pth),'-o',str(binary)],capture_output=True,text=True)
        assert not r.returncode,r.stderr
        r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
        if a.negative:
            assert r.returncode,key;controls[key]={'rejected':True,'diagnostic':r.stderr[:700]}
        else:
            assert not r.returncode,r.stderr
            controls=json.loads(r.stdout)
    report={'status':'PASS','tests':controls,'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Actual production probe, cleanup, timer initialization and firmware ownership functions with mocked kernel services; no chip protocol or live device validation.'}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
