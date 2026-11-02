#!/usr/bin/env python3
"""Validate the SRAM recipe with the real 6.18 OF translation functions."""
import argparse
import copy
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import struct
import subprocess

H = Path(__file__).resolve().parent
C = H.parents[1]
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
A = W / 'artifacts/20261102T024104137567Z'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate', type=Path, help='Full build artifact to verify against the corrected trees')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('dt_recipe', C / 'build_devicetrees.py')
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)


def cells(value):
    return list(struct.unpack('>' + 'I' * (len(value) // 4), value))


def function(source, name):
    match = re.search(r'^[^\n;{}]*\b' + name + r'\([^;{]+\n\{', source, re.M)
    assert match, name
    start = match.start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


node = '/soc/mmio-sram@17D09400'
child = node + '/scmi-shmem@0'
sources = {'tuna': A / 'devicetrees/checked/tuna.dtb',
           'tuna7': A / 'devicetrees/checked/tuna7.dtb',
           'live': W / 'device-backups/5493f3c6-20261101T160034Z/logging-investigation/recovery-runtime-20261102T014842Z/native-618-recovery/live.dtb'}
dtc = W / 'prebuilts/kernel-build-tools/linux_musl-x86/bin/dtc'
overlay_source = C / 'devicetrees/onyx-6.18-scmi-sram-layout.dts'
overlay = H / 'recipe-overlay.dtbo'
compile_overlay = subprocess.run([str(dtc), '-I', 'dts', '-O', 'dtb', '-@', '-o', str(overlay),
                                  str(overlay_source)], capture_output=True, text=True)
assert compile_overlay.returncode == 0 and not compile_overlay.stderr, compile_overlay.stderr
snapshots = []
comparisons = []
for name, path in sources.items():
    before = dt.read_fdt(path)
    assert cells(before['/soc']['#address-cells']) == [1]
    assert cells(before['/soc']['#size-cells']) == [1]
    result = H / (name + '-corrected.dtb')
    subprocess.run([str(A / 'devicetrees/tools/fdtoverlay'), '-i', str(path), '-o', str(result), str(overlay)], check=True)
    after = dt.read_fdt(result)
    assert before.keys() == after.keys()
    changes = [(p, k) for p in before for k in set(before[p]) | set(after[p])
               if before[p].get(k) != after[p].get(k)]
    assert set(changes) == {(node, 'reg'), (node, 'ranges'), (child, 'reg')}, changes
    # All firmware references, board IDs, node phandles and other devices retain exact bytes.
    assert before['/soc/qcom,scmi']['shmem'] == after['/soc/qcom,scmi']['shmem']
    for variant, tree in [('before', before), ('corrected', after)]:
        snapshots.append((name + '-' + variant, tree, int(variant == 'corrected')))
    for p, k in [(node, 'reg'), (node, 'ranges'), (child, 'reg')]:
        broken = copy.deepcopy(after)
        broken[p][k] = before[p][k]
        snapshots.append((name + '-revert-' + p.split('/')[-1] + '-' + k, broken, 2))
    if args.candidate and name != 'live':
        built = dt.read_fdt(args.candidate / 'devicetrees/checked' / (name + '.dtb'))
        assert built == after, ('built recipe differs from verified overlay', name)
    comparisons.append({'tree': name, 'changes': changes, 'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                        'corrected_sha256': hashlib.sha256(result.read_bytes()).hexdigest()})

adapter = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __be32;
#define cpu_to_be32(x) htonl((uint32_t)(x))
#define be32_to_cpu(x) ntohl(x)
#define OF_BAD_ADDR UINT64_MAX
#define OF_MAX_ADDR_CELLS 4
#define OF_CHECK_ADDR_COUNT(na) ((na)>0&&(na)<=4)
#define OF_CHECK_COUNTS(na,ns) (OF_CHECK_ADDR_COUNT(na)&&(ns)>0)
#define IORESOURCE_MEM 1
#define IORESOURCE_IO 2
#define IORESOURCE_MEM_NONPOSTED 4
#define PCI_BASE_ADDRESS_0 0x10
#define LOGIC_PIO_CPU_MMIO 1
#define __free(x)
#define no_free_ptr(p) (p)
#define VISIBLE_IF_KUNIT
#define unreachable() __builtin_unreachable()
#define overflows_type(x,y) false
#define wrapping_sub(type,a,b) ((type)((a)-(b)))
#define check_add_overflow(a,b,p) __builtin_add_overflow(a,b,p)
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define of_dump_addr(...) ((void)0)
struct device_node {
 struct device_node *parent;
 int address_cells,size_cells,fwnode;
 const char *full_name;
 __be32 reg[8],ranges[8];unsigned reg_length,range_length;
 bool has_ranges;
};
struct resource {u64 start,end;unsigned flags;const char *name;};
struct logic_pio_hwaddr {int flags;};
struct of_bus {
 const char *name,*addresses;
 void (*count_cells)(struct device_node *,int *,int *);
 u64 (*map)(__be32 *,const __be32 *,int,int,int,int);
 int (*translate)(__be32 *,u64,int);
 unsigned flag_cells;
 unsigned (*get_flags)(const __be32 *);
};
static inline u64 of_read_number(const __be32 *p,int n) {u64 v=0;while(n--)v=(v<<32)|ntohl(*p++);return v;}
static inline int of_n_addr_cells(struct device_node *n) {return n->parent->address_cells;}
static inline int of_n_size_cells(struct device_node *n) {return n->parent->size_cells;}
static inline struct device_node *of_node_get(struct device_node *n) {return n;}
static inline void of_node_put(struct device_node *n) {(void)n;}
static inline struct device_node *of_get_parent(const struct device_node *n) {return n->parent;}
static inline const void *of_get_property(const struct device_node *n,const char *name,void *length) {
 if(strcmp(name,"reg")==0){if(length)*(unsigned *)length=n->reg_length;return n->reg_length?n->reg:NULL;}
 if(strcmp(name,"ranges")==0){if(length)*(unsigned *)length=n->range_length;return n->has_ranges?n->ranges:NULL;}
 return NULL;
}
static inline int of_empty_ranges_quirk(const struct device_node *n) {(void)n;return 0;}
static inline struct logic_pio_hwaddr *find_io_range_by_fwnode(int *f) {(void)f;return NULL;}
static inline int of_property_read_string_index(struct device_node *n,const char *p,int i,const char **s) {(void)n;(void)p;(void)i;(void)s;return -EINVAL;}
static inline bool of_mmio_is_nonposted(struct device_node *n) {(void)n;return false;}
static inline u64 of_translate_ioport(struct device_node *n,const __be32 *a,u64 s) {(void)n;(void)a;(void)s;return OF_BAD_ADDR;}
'''
source = (W / 'common/drivers/of/address.c').read_text()
functions = ['of_bus_default_count_cells', 'of_bus_default_map', 'of_bus_default_translate', 'of_bus_default_get_flags']
code = adapter + '\n'.join(function(source, name) for name in functions)
code += r'''
static const struct of_bus bus={.name="default",.addresses="reg",.count_cells=of_bus_default_count_cells,
 .map=of_bus_default_map,.translate=of_bus_default_translate,.get_flags=of_bus_default_get_flags};
static inline const struct of_bus *of_match_bus(struct device_node *n) {(void)n;return &bus;}
'''
functions += ['of_translate_one', '__of_translate_address', 'of_translate_address', '__of_get_address',
              '__of_address_resource_bounds', '__of_address_to_resource', 'of_address_to_resource']
code += '\n' + '\n'.join(function(source, name) for name in functions[4:])
code += r'''
static void values(__be32 *out,const u32 *in,unsigned count) {for(unsigned i=0;i<count;i++)out[i]=htonl(in[i]);}
static void check_case(const char *name,int root_na,int root_ns,const u32 *reg,unsigned rn,const u32 *ranges,unsigned nn,const u32 *creg,unsigned cn,int corrected) {
 struct device_node root={.address_cells=root_na,.size_cells=root_ns,.full_name="/"};
 struct device_node soc={.parent=&root,.address_cells=1,.size_cells=1,.full_name="/soc",.has_ranges=true};
 struct device_node sram={.parent=&soc,.address_cells=2,.size_cells=2,.full_name="sram",.has_ranges=true,.reg_length=rn*4,.range_length=nn*4};
 struct device_node shmem={.parent=&sram,.full_name="shmem",.reg_length=cn*4};
 values(sram.reg,reg,rn);values(sram.ranges,ranges,nn);values(shmem.reg,creg,cn);
 struct resource parent={0},child={0};
 int pr=of_address_to_resource(&sram,0,&parent),cr=of_address_to_resource(&shmem,0,&child);
 if(corrected==2){
  assert(pr!=0||cr!=0||parent.start!=0x17d09400||parent.end!=0x17d097ff||child.start!=0x17d09400||child.end!=0x17d097ff);
  printf("PASS: rejected partial repair %s\n",name);return;
 }
 assert(pr==0&&cr==0);
 if(corrected){assert(parent.start==0x17d09400&&parent.end==0x17d097ff);}
 else {assert(parent.start==0&&parent.end==0x17d093ff);}
 // Both before and after map firmware's SCMI buffer to the intended physical bytes.
 assert(child.start==0x17d09400&&child.end==0x17d097ff);
 printf("PASS: %s parent 0x%llx..0x%llx; SCMI 0x%llx..0x%llx\n",name,(unsigned long long)parent.start,(unsigned long long)parent.end,(unsigned long long)child.start,(unsigned long long)child.end);
 // The corrected 0x400 window rejects an address just outside the declared region.
 if(corrected){shmem.reg[1]=htonl(0x400);assert(of_address_to_resource(&shmem,0,&child)==-EINVAL);}
}
int main(void) {
'''
for name, tree, corrected in snapshots:
    na, ns = cells(tree['/']['#address-cells'])[0], cells(tree['/']['#size-cells'])[0]
    reg, ranges, creg = cells(tree[node]['reg']), cells(tree[node]['ranges']), cells(tree[child]['reg'])
    c_array = lambda v: '(const u32[]){' + ','.join(hex(x) for x in v) + '}'
    code += f'check_case("{name}",{na},{ns},{c_array(reg)},{len(reg)},{c_array(ranges)},{len(ranges)},{c_array(creg)},{len(creg)},{corrected});\n'
code += 'return 0;\n}\n'
p = H / 'translation-test.c'
p.write_text(code)
exe = H / 'translation-test'
subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', str(p), '-o', str(exe)], check=True)
run = subprocess.run([str(exe)], capture_output=True, text=True)
assert run.returncode == 0, run.stderr
report = {'status': 'PASS: recipe source and OF translations; hardware pending',
          'overlay_source': str(overlay_source),
          'overlay_sha256': hashlib.sha256(overlay_source.read_bytes()).hexdigest(),
          'built_artifact': str(args.candidate) if args.candidate else None,
          'cases': len(snapshots), 'partial_repair_negative_controls': 9,
          'trees': comparisons, 'production_functions': functions,
          'core_source_sha256': hashlib.sha256(source.encode()).hexdigest(), 'stdout': run.stdout,
          'scope': 'Actual 6.18 OF generic-bus resource translation against properties from both board DTBs and live root-recovery DT; only three properties change. Firmware address/length and all phandle bytes preserved.',
          'limits': 'No hardware SRAM binding or firmware traffic verified with the correction. Non-MMIO OF branches adapted out in host fixture.'}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(run.stdout, end='')
