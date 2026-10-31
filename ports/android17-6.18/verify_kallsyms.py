#!/usr/bin/env python3
"""Cross-check linked 6.18 kallsyms tables against the matching System.map."""

import argparse
import hashlib,json,struct
from pathlib import Path
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("gki", type=Path, help="GKI artifact directory")
parser.add_argument("--output", type=Path, help="Write the validation JSON here")
args = parser.parse_args()
p = args.gki
blob=(p/'vmlinux').read_bytes()
assert blob[:6]==b'\x7fELF\x02\x01'
phoff=struct.unpack_from('<Q',blob,32)[0]
phsize,phnum=struct.unpack_from('<HH',blob,54)
segments=[struct.unpack_from('<IIQQQQQQ',blob,phoff+i*phsize) for i in range(phnum)]
def read(addr,n):
    for typ,flags,offset,vaddr,paddr,size,memsz,align in segments:
        if typ==1 and vaddr<=addr and addr+n<=vaddr+size:
            return blob[offset+addr-vaddr:offset+addr-vaddr+n]
    raise ValueError(hex(addr))
symbols={};expected=set()
for line in (p/'System.map').read_text().splitlines():
    a,t,name=line.split(); a=int(a,16); symbols[name]=a; expected.add((name,a))
def scalar(name,fmt):return struct.unpack(fmt,read(symbols[name],struct.calcsize(fmt)))[0]
n=scalar('kallsyms_num_syms','<I');base=scalar('kallsyms_relative_base','<Q')
offsets=struct.unpack('<'+'I'*n,read(symbols['kallsyms_offsets'],n*4))
index=struct.unpack('<256H',read(symbols['kallsyms_token_index'],512))
tokens=read(symbols['kallsyms_token_table'],symbols['kallsyms_token_index']-symbols['kallsyms_token_table'])
names=read(symbols['kallsyms_names'],symbols['kallsyms_token_table']-symbols['kallsyms_names'])
selected=['_sdata','__bss_stop','__per_cpu_start','__per_cpu_end','__start_ro_after_init','__end_ro_after_init','prb','linux_banner','irq_stack_ptr','tick_cpu_device','modules']
selected_values={};off=0
for i,delta in enumerate(offsets):
    size=names[off];off+=1
    if size&128:
        size=(size&127)|(names[off]<<7);off+=1
    encoded=names[off:off+size];off+=size
    name=''.join(tokens[index[x]:].split(b'\0',1)[0].decode() for x in encoded)[1:]
    addr=base+delta
    assert (name,addr) in expected,(i,name,hex(addr))
    if name in selected:selected_values[name]=hex(addr)
assert set(selected_values)==set(selected),(set(selected)-set(selected_values))
report={'kernel_release':(p/'kernel.release').read_text().strip(),'vmlinux_sha256':hashlib.sha256(blob).hexdigest(),'decoded_symbols_compared_to_system_map':n,'cached_symbols':selected_values,'method':'Decode linked kallsyms names/token table and unsigned 32-bit relative offsets from ELF LOAD segments; compare every entry with System.map.','limits':['Offline linked-image validation; reserved-memory handoff and live crash diagnostics still require device testing.']}
if args.output:
    args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
