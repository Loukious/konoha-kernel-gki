#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent

def function(source, name):
    match=re.search(r'(?m)^(?:static )?(?:enum hrtimer_restart\s+|struct \w+ \*|[\w*]+\s+)'+re.escape(name)+r'\([^;]+?\)\s*\{',source)
    assert match,name
    pos=match.end();depth=1
    while depth:
        depth+=(source[pos]=='{')-(source[pos]=='}')
        pos+=1
    return source[match.start():pos]

def run(test, source):
    with tempfile.TemporaryDirectory(prefix='onyx-haptic-test-') as tmp:
        d=Path(tmp);(d/'test.c').write_text(source)
        built=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
        if built.returncode:raise RuntimeError(test+': '+built.stderr)
        result=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
        if result.returncode:raise RuntimeError(test+': '+result.stdout+result.stderr)
        return json.loads(result.stdout)

def reject(test, source):
    with tempfile.TemporaryDirectory(prefix='onyx-haptic-negative-') as tmp:
        d=Path(tmp);(d/'test.c').write_text(source)
        built=subprocess.run(['cc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],capture_output=True,text=True)
        if built.returncode:raise RuntimeError(test+': '+built.stderr)
        result=subprocess.run([str(d/'test')],capture_output=True,text=True,timeout=30)
        assert result.returncode and ('Assertion' in result.stderr or 'AddressSanitizer' in result.stderr),test
        return {'rejected':True,'compiled':True,'detector':'AddressSanitizer' if 'AddressSanitizer' in result.stderr else 'assertion'}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--package',default='onyx');ap.add_argument('--output',type=Path);args=ap.parse_args()
    base=args.workspace/args.package/'drivers/input/misc/aw86927_haptic';sources={p.name:p.read_text() for p in base.glob('*') if p.is_file()}
    source=sources['aw8697.c'];a=source.index('struct aw8697_misc_context {');b=source.index('static const unsigned char aw8697_reg_access',a)
    test=(HERE/'tests/haptic_misc.c').read_text().replace('/* STRUCTURES */',source[a:b]).replace('/* OPERATIONS */','\n\n'.join(function(source,'aw8697_file_'+n) for n in ['open','release','unlocked_ioctl','read','write'])).replace('/* REGISTRATION */','\n\n'.join(function(source,'aw8697_misc_'+n) for n in ['register','unregister']))
    report={'misc':run('misc',test)}
    negative={'removed_misc_still_attached':reject('misc disconnect',test.replace('context->aw8697 = NULL;', '/* missing disconnect */'))}
    awinic=re.search(r'struct awinic \{.*?\n\};',sources['aw_haptic.h'],re.S).group(0)
    pieces=[]
    for chip in ['aw8697','aw86927']:
        source=sources[chip+'.c']
        for name in [chip+'_ram_work_routine',chip+'_ram_update',chip+'_ram_init',chip+'_stop_work_routine','qti_hap_stop_timer','qti_hap_disable_timer',chip+'_vibrator_init',chip+'_shutdown']:
            pieces.append(function(source,name).replace('qti_hap_stop_timer',chip+'_stop_timer_callback').replace('qti_hap_disable_timer',chip+'_disable_timer_callback'))
    pieces.extend(function(sources['aw_haptic.c'],name) for name in ['awinic_cleanup','awinic_i2c_probe','awinic_i2c_remove'])
    test=(HERE/'tests/haptic_lifecycle.c').read_text().replace('/* AWINIC */',awinic).replace('/* SOURCE */','\n\n'.join(pieces)).replace('struct aw8697','struct mock_aw').replace('struct aw86927','struct mock_aw')
    report['lifecycle']=run('lifecycle',test)
    negative['work_published_uninitialized']=reject('early publication',test.replace('INIT_WORK(&aw8697->set_gain_work, aw8697_haptics_set_gain_work_routine);','/* missing initialization */'))
    negative['timer_performs_i2c']=reject('timer I2C',test.replace('WRITE_ONCE(aw8697->play.length_us, 0);','WRITE_ONCE(aw8697->play.length_us, 0);\n\taw8697_haptic_play_go(aw8697, false);'))
    test=(HERE/'tests/haptic_firmware.c').read_text().replace('/* SOURCE */','\n\n'.join(function(sources[chip+'.c'],chip+'_ram_loaded') for chip in ['aw8697','aw86927'])).replace('struct aw8697 *','struct mock_aw *').replace('struct aw86927 *','struct mock_aw *')
    report['firmware']=run('firmware',test)
    negative['short_ram_header_not_rejected']=reject('RAM bounds',test.replace('cont->size < 5 ||',''))
    test=(HERE/'tests/haptic_i2c.c').read_text().replace('/* SOURCE */','\n\n'.join(function(sources[chip+'.c'],chip+'_'+name) for chip in ['aw8697','aw86927'] for name in ['i2c_writes','i2c_write_bits','alloc_rtp'])).replace('struct aw8697 *','struct mock_aw *').replace('struct aw86927 *','struct mock_aw *')
    report['i2c']=run('i2c',test)
    negative['i2c_allocation_unchecked']=reject('I2C allocation',test.replace('\tif (!data)\n\t\treturn -ENOMEM;',''))
    ring=sources['ringbuffer.c'];ring=ring[ring.index('#define BUFFER_SIZE'):]
    code=function(ring,'read_rb');ring=ring.replace(code,code.replace('return ret;', 'return size;'))
    test=(HERE/'tests/haptic_ring.c').read_text().replace('/* SOURCE */',ring)
    negative['ring_reports_uncopied_bytes']=reject('ring short read',test)
    report['negative_controls']=negative
    for chip in ['aw8697','aw86927']:
        source=sources[chip+'.c']
        assert 'request_firmware_nowait(' not in source
        for callback in ['qti_hap_stop_timer','qti_hap_disable_timer',chip+'_vibrator_timer_func',chip+'_haptic_audio_timer_func']:
            code=function(source,callback)
            assert not re.search(r'\b\w*(?:i2c|play_go|haptic_stop)\w*\(',code),callback
    report['source_invariants']={'no_detached_firmware_requests':True,'timer_callbacks_do_not_call_i2c':True}
    report['sanitizers']=['AddressSanitizer','UndefinedBehaviorSanitizer']
    report['source_sha256']={name:hashlib.sha256((base/name).read_bytes()).hexdigest() for name in sorted(sources)}
    report['harness_sha256']={name:hashlib.sha256((HERE/'tests'/name).read_bytes()).hexdigest() for name in ['haptic_misc.c','haptic_lifecycle.c','haptic_firmware.c','haptic_i2c.c','haptic_ring.c']}
    report['limits']='Extracted production operations with pthread locks, mocked krefs, I2C and user-copy faults. Does not validate live VFS, IRQ, firmware, electrical behavior or phone operation.'
    encoded=json.dumps(report,indent=2)+'\n';print(encoded,end='')
    if args.output:args.output.write_text(encoded)

if __name__=='__main__':main()
