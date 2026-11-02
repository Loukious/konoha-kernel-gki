"""Repack matched recovery with a source-built init-mode correction only."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from build_logging_recovery import entries,record
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('backup',type=Path);p.add_argument('binary_report',type=Path);p.add_argument('--rom-root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(exist_ok=False,parents=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((a.backup/'backup-manifest.json').read_text());source=Path(manifest['images']['recovery']['path']);assert sha(source)==manifest['images']['recovery']['sha256']
build=json.loads(a.binary_report.read_text());assert build['status'].startswith('PASS');binary=Path(build['candidate_init']);assert sha(binary)==build['candidate_init_sha256']
tools=a.rom_root/'out/host/linux-x86/bin';commands=[]
def run(tool,args,label):
 args=[str(tools/tool),*[str(x) for x in args]];commands.append(args);r=subprocess.run(args,capture_output=True);(a.output/(label+'.log')).write_bytes(r.stdout+r.stderr);assert r.returncode==0,(label,r.stderr.decode(errors='replace'));return r.stdout
unpack=a.output/'input';raw_args=run('unpack_bootimg',['--boot_img',source,'--out',unpack,'--format','mkbootimg','-0'],'unpack-original').decode().split('\0');args=[s for s in raw_args if s!=''];assert (unpack/'kernel').stat().st_size==0
# Retain empty mkbootimg arguments: -0 has an empty final terminator only.
args=raw_args[:-1] if raw_args[-1]=='' else raw_args
original=entries(run('lz4',['-d','-c',unpack/'ramdisk'],'decompress-original'));target='system/bin/init';fields,old,raw=original[target];assert hashlib.sha256(old).hexdigest()==build['shipping_init_sha256']
blob=b''.join(record(n,f,binary.read_bytes()) if n==target else raw for n,(f,payload,raw) in original.items());blob+=record('TRAILER!!!',[0]*13,b'');blob+=b'\0'*(-len(blob)%512)
cpio=a.output/'ramdisk.cpio';cpio.write_bytes(blob);compressed=a.output/'ramdisk.lz4';run('lz4',['-l','-12',cpio,compressed],'compress-candidate')
args[args.index('--ramdisk')+1]=str(compressed);image=a.output/'recovery.img';run('mkbootimg',args+['--output',image],'mkbootimg')
info=run('avbtool',['info_image','--image',source],'original-avb').decode();algorithm=re.search(r'Algorithm:\s*(\S+)',info).group(1);assert algorithm=='SHA256_RSA2048';salt=re.search(r'Salt:\s*(\S+)',info).group(1);rollback=re.search(r'Rollback Index:\s*(\d+)',info).group(1)
footer=['add_hash_footer','--image',image,'--partition_name','recovery','--partition_size',source.stat().st_size,'--algorithm',algorithm,'--key',a.rom_root/'external/avb/test/data/testkey_rsa2048.pem','--rollback_index',rollback,'--salt',salt]
for key,value in re.findall(r"Prop: (\S+) -> '([^']*)'",info):footer+=['--prop',key+':'+value]
run('avbtool',footer,'candidate-footer');run('avbtool',['verify_image','--image',image],'verify-avb');after_info=run('avbtool',['info_image','--image',image],'candidate-avb').decode()
assert re.search(r'Public key \(sha1\):\s*(\S+)',info).group(1)==re.search(r'Public key \(sha1\):\s*(\S+)',after_info).group(1)
check=a.output/'checked';checked_args=run('unpack_bootimg',['--boot_img',image,'--out',check,'--format','mkbootimg','-0'],'unpack-candidate').decode().split('\0')
# All header arguments except extracted paths remain identical.
for before,after in zip(raw_args,checked_args):
 if str(unpack) in before:assert before.replace(str(unpack),str(check))==after
 else:assert before==after,(before,after)
assert len(raw_args)==len(checked_args)
after=entries(run('lz4',['-d','-c',check/'ramdisk'],'decompress-check'));assert after.keys()==original.keys();assert after[target][1]==binary.read_bytes()
for n,(f,payload,raw) in original.items():
 if n!=target:assert after[n][2]==raw,n
assert (check/'kernel').read_bytes()==(unpack/'kernel').read_bytes();assert image.stat().st_size==source.stat().st_size
report={'status':'PASS: offline source-built recovery init fix; not installed','source_image':str(source),'source_sha256':sha(source),'image':str(image),'sha256':sha(image),'size':image.stat().st_size,'source_build_report':str(a.binary_report),'source_build_report_sha256':sha(a.binary_report),'candidate_init_sha256':sha(binary),'only_changed_cpio_record':target,'preserved_raw_records':len(original)-1,'cpio_paths_unchanged':True,'headers_and_key_unchanged':True,'kernel':'empty as original; uses current6.18bootkernel','commands':commands,'limits':'Recovery mode boot, ADB/fastbootd, module loading and kernel logs require a controlled hardware test; five normalboot images remain unchanged.'}
(a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['status','image','sha256','only_changed_cpio_record','preserved_raw_records']},indent=2))
