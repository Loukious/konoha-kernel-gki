#!/usr/bin/env python3
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def main():
 ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);a=ap.parse_args();p=a.workspace/a.package/'drivers/input/misc/aw86927_haptic/ringbuffer.c';source=p.read_text();source=source[source.index('#define BUFFER_SIZE'):];test=(HERE/'tests/haptic_ring.c').read_text().replace('/* SOURCE */',source)
 with tempfile.TemporaryDirectory(prefix='onyx-haptic-ring-') as tmp:
  d=Path(tmp);(d/'test.c').write_text(test);proc=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
  if proc.returncode:raise RuntimeError(proc.stderr)
  run=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
  if run.returncode:raise RuntimeError(run.stdout+run.stderr)
  report=json.loads(run.stdout)
 report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer'];report['source_sha256']=hashlib.sha256(p.read_bytes()).hexdigest();report['limits']='Actual production ring functions with pthread locks/SRCU/condition mocks. No live kernel stream, IRQ, hardware haptic or firmware validation.';out=json.dumps(report,indent=2)+'\n';print(out,end='')
 if a.output:a.output.write_text(out)
if __name__=='__main__':main()
