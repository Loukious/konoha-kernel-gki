from pathlib import Path
import re, subprocess, tempfile, json, hashlib, argparse
H=Path(__file__).resolve().parent
def extract(text,name):
    m=re.search(r'^static (?:int|void) '+name+r'\([^;]*?\)\n\{',text,re.M);assert m,name
    return text[m.start():text.index('\n}',m.end())+2]
ap=argparse.ArgumentParser();ap.add_argument('--source-root',type=Path,default=H/'candidate');ap.add_argument('--output',type=Path,default=H/'stream-tests.json');a=ap.parse_args()
p=a.source_root/'drivers/input/misc/si_haptic/haptic_misc.c';s=p.read_text();names=['haptic_clean_buf','stream_work_proc','sih_vma_open','sih_vma_close','memo_file_mmap']
code=(H/'stream.c').read_text().replace('/* SOURCE */','\n'.join(extract(s,n) for n in names[:4])+'\n'+s[s.index('static const struct vm_operations_struct sih_vm_ops'):s.index('static int memo_file_mmap')]+extract(s,names[-1]))
with tempfile.TemporaryDirectory(prefix='onyx-sih-stream-') as tmp:
    pth=Path(tmp)/'test.c';pth.write_text(code);binary=pth.with_suffix('')
    r=subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Werror=implicit-function-declaration',str(pth),'-o',str(binary)],capture_output=True,text=True);assert not r.returncode,r.stderr
    r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20);assert not r.returncode,r.stderr
    report={'status':'PASS','tests':json.loads(r.stdout),'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'sanitizers':['AddressSanitizer','UndefinedBehaviorSanitizer'],'limits':'Actual production stream and mmap functions with mocked register/time/page services; validates calls and accounting, not real kernel page tables or live haptics.'}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
