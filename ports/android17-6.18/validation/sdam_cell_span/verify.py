#!/usr/bin/env python3
"""Verify the actual native/current NVMEM parsers and effective cell reads."""
from pathlib import Path
import hashlib, json, re, struct, subprocess, sys, tempfile

HERE = Path(__file__).resolve().parent
RECIPE = HERE.parent.parent
WORK = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
ARCHIVE = WORK / 'artifacts/20261101T232730636087Z'
NODE = '/soc/qcom,spmi@c42d000/qcom,pmk8550@0/sdam@7000/sm1510_present@5d'
sys.path.insert(0, str(RECIPE))
from build_devicetrees import read_fdt

def function(path, name):
    text = path.read_text()
    start = re.search(r'^static (?:int|void) ' + name + r'\(', text, re.M).start()
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

report = {'status': 'PASS', 'boards': {}, 'functions': {}}
with tempfile.TemporaryDirectory(prefix='onyx-sdam-span-') as tmp:
    tmp = Path(tmp)
    dtc = WORK / 'prebuilts/kernel-build-tools/linux_musl-x86/bin/dtc'
    fdtoverlay = Path('/usr/bin/fdtoverlay')
    overlay = tmp / 'span.dtbo'
    subprocess.run([str(dtc), '-@', '-I', 'dts', '-O', 'dtb', '-o', str(overlay), str(RECIPE / 'devicetrees/onyx-6.18-sdam-cell-span.dts')], check=True, capture_output=True)
    for board in ['tuna.dtb', 'tuna7.dtb']:
        old_path = ARCHIVE / 'devicetrees/checked' / board
        new_path = tmp / board
        subprocess.run([str(fdtoverlay), '-i', str(old_path), '-o', str(new_path), str(overlay)], check=True)
        old, new = read_fdt(old_path), read_fdt(new_path)
        expected = {n: dict(p) for n, p in old.items()}
        assert old[NODE]['reg'] == struct.pack('>II', 0x5d, 1)
        assert old[NODE]['bits'] == struct.pack('>II', 5, 5)
        expected[NODE]['reg'] = struct.pack('>II', 0x5d, 2)
        assert expected == new, board
        invalid = []
        tested = 0
        for name, props in new.items():
            if 'bits' not in props or 'reg' not in props:
                continue
            reg, bits = props['reg'], props['bits']
            if len(reg) == len(bits) == 8:
                offset, width = struct.unpack('>II', bits)
                length = struct.unpack('>II', reg)[1]
                tested += 1
                if offset >= 8 * length or width < 1 or offset + width > 8 * length:
                    invalid.append(name)
        assert not invalid, invalid
        report['boards'][board] = {'only_changed_property': NODE + '/reg', 'reg': [0x5d, 2], 'bits_preserved': [5, 5], 'bitfield_cells_audited': tested}

    adapter = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint32_t __be32;
#define GFP_KERNEL 0
#define BITS_PER_BYTE 8
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define IS_ALIGNED(n,a) (!((n)&((a)-1)))
#define GENMASK(h,l) ((uint32_t)((~0U << (l)) & (~0U >> (31-(h)))))
struct device { int id; };
struct device_node { __be32 reg[2],bits[2];int reg_len,bits_len;struct device_node *child,*next; };
struct nvmem_cell_info;
struct nvmem_device;
struct nvmem_layout { void (*fixup_cell_info)(struct nvmem_device *,struct nvmem_layout *,struct nvmem_cell_info *); };
struct nvmem_device { struct device dev;int stride,word_size;struct nvmem_layout *layout;void (*fixup_dt_cell_info)(struct nvmem_device *,struct nvmem_cell_info *); };
struct nvmem_cell_info { unsigned offset,bytes,bit_offset,nbits;size_t raw_len;char *name;void *read_post_process,*priv;struct device_node *np; };
struct nvmem_cell_entry { struct nvmem_device *nvmem;int offset,bytes;size_t raw_len;char *name;void *read_post_process,*priv;int bit_offset,nbits;struct device_node *np; };
static unsigned warnings,cell_count;
static struct nvmem_cell_entry cells[4];
#define dev_err(dev,...) ((void)(dev),warnings++)
#define for_each_child_of_node(np,child) for ((child)=(np)->child;(child);(child)=(child)->next)
static u32 be32_to_cpup(const __be32 *value) { return __builtin_bswap32(*value); }
static const __be32 *of_get_property(struct device_node *np,const char *name,int *len) {
 if(!strcmp(name,"reg")){*len=np->reg_len;return np->reg_len?np->reg:NULL;}
 assert(!strcmp(name,"bits"));*len=np->bits_len;return np->bits_len?np->bits:NULL;
}
static void *allocations[32];static unsigned allocation_count;
static char *kasprintf(int gfp,const char *format,struct device_node *np) { (void)gfp;(void)format;(void)np;assert(allocation_count<32);char *p=strdup("cell");allocations[allocation_count++]=p;return p; }
static void host_free(void *p) { for(unsigned i=0;i<allocation_count;i++)if(allocations[i]==p)allocations[i]=NULL;free(p); }
static struct device_node *of_node_get(struct device_node *np) { return np; }
static void of_node_put(struct device_node *np) { (void)np; }
#define kfree host_free
'''
    chunks = [adapter]
    native = WORK / 'baseline/kernel/xiaomi/sm8735/drivers/nvmem/core.c'
    current = WORK / 'common/drivers/nvmem/core.c'
    for variant, path in [('old', native), ('new', current)]:
        mapper_name = 'nvmem_cell_info_to_nvmem_cell_entry_nodup'
        shift_name = 'nvmem_shift_read_buffer_in_place'
        parser_name = 'nvmem_add_cells_from_dt'
        mapper, shift, parser = [function(path, n) for n in [mapper_name, shift_name, parser_name]]
        for name, text in [(mapper_name, mapper), (shift_name, shift), (parser_name, parser)]:
            report['functions'][variant + ':' + name] = hashlib.sha256(text.encode()).hexdigest()
        chunks.append(mapper.replace(mapper_name, variant + '_mapper'))
        chunks.append(shift.replace(shift_name, variant + '_shift'))
        chunks.append('static int ' + variant + '_add(struct nvmem_device *n,const struct nvmem_cell_info *info) { assert(cell_count<4);int ret=' + variant + '_mapper(n,info,&cells[cell_count]);if(!ret){cells[cell_count].name=NULL;cell_count++;}return ret;}')
        chunks.append(parser.replace(parser_name, variant + '_parse').replace('nvmem_add_one_cell', variant + '_add'))
    chunks.append(r'''
static void reset(void) { for(unsigned i=0;i<allocation_count;i++)free(allocations[i]);allocation_count=0;warnings=cell_count=0;memset(cells,0,sizeof(cells)); }
int main(void) {
 struct nvmem_device dev={.stride=1,.word_size=1};
 struct device_node sm={.reg={__builtin_bswap32(0x5d),__builtin_bswap32(1)},.bits={__builtin_bswap32(5),__builtin_bswap32(5)},.reg_len=8,.bits_len=8};
 struct device_node ocp={.reg={__builtin_bswap32(0x76),__builtin_bswap32(6)},.reg_len=8};
 struct device_node parent={.child=&sm};sm.next=&ocp;
 reset();assert(!old_parse(&dev,&parent));assert(cell_count==2 && !warnings);
 struct nvmem_cell_entry old_sm=cells[0],old_ocp=cells[1];
 assert(old_sm.offset==0x5d && old_sm.bytes==2 && old_sm.raw_len==2 && old_sm.bit_offset==5 && old_sm.nbits==5);
 reset();assert(new_parse(&dev,&parent)==-EINVAL);assert(warnings==1 && !cell_count);
 sm.reg[1]=__builtin_bswap32(2);
 reset();assert(!new_parse(&dev,&parent));assert(cell_count==2 && !warnings);
 struct nvmem_cell_entry new_sm=cells[0],new_ocp=cells[1];
 assert(new_sm.offset==old_sm.offset && new_sm.bytes==old_sm.bytes && new_sm.raw_len==old_sm.raw_len && new_sm.bit_offset==old_sm.bit_offset && new_sm.nbits==old_sm.nbits);
 assert(new_ocp.offset==old_ocp.offset && new_ocp.bytes==old_ocp.bytes && new_ocp.raw_len==old_ocp.raw_len);
 for(unsigned value=0;value<65536;value++) {
  u8 before[2]={value&255,value>>8},after[2]={value&255,value>>8};
  old_shift(&old_sm,before);new_shift(&new_sm,after);
  assert(!memcmp(before,after,2));assert(after[0]==((value>>5)&31) && !after[1]);
 }
 const unsigned invalid[][3]={{1,5,5},{2,16,1},{2,5,0},{2,5,12}};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
  sm.reg[1]=__builtin_bswap32(invalid[i][0]);sm.bits[0]=__builtin_bswap32(invalid[i][1]);sm.bits[1]=__builtin_bswap32(invalid[i][2]);
  reset();assert(new_parse(&dev,&parent)==-EINVAL && warnings==1 && !cell_count);
 }
 reset();
 puts("PASS: exact native/current NVMEM parsers, preserved offset/bitfield/two-byte reads and sibling cell; 65536 identical read results; four invalid-span controls rejected");return 0;
}
''')
    source = tmp / 'test.c'
    source.write_text('\n'.join(chunks) + '\n')
    compiler = WORK / 'prebuilts/clang/host/linux-x86/clang-r596125/bin/clang'
    binary = tmp / 'test'
    subprocess.run([str(compiler), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    report['output'] = result.stdout.strip()
report['upstream_error_path_observation'] = 'Pinned current parser allocates cell diagnostic name before validating bits and leaves it allocated on invalid-bit error. Host test cleanup frees these allocations after negative controls; the valid corrected DT never enters this error path.'
report['limits'] = 'Actual source parsers, cell geometry and bit extraction with host OF/device/allocation adapters. Native physical read geometry preserved. Live SDAM provider binding and BCL probe outcome are separate checks.'
(HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(report['output'])
