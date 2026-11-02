#!/usr/bin/env python3
"""Exercise the actual driver mmap function and kernel file replacement helper."""
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
WORK = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


adapter = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define KERNEL_VERSION(a,b,c) ((a)*10000+(b)*100+(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,33)
#define VM_PFNMAP 1UL
#define VM_MIXEDMAP 2UL
#define VM_DONTEXPAND 4UL
#define MSM_BO_WC 1U
#define MSM_BO_UNCACHED 2U
#define MSM_BO_CACHED 4U
#define swap(a,b) do { __typeof__(a) temporary=(a); (a)=(b); (b)=temporary; } while (0)
struct file { int refs; };
struct drm_gem_object { struct file *filp; };
struct msm_gem_object { struct drm_gem_object base; unsigned flags; };
struct vm_area_struct {
    struct file *vm_file;
    unsigned long vm_flags, vm_pgoff, vm_page_prot;
};
static struct msm_gem_object *to_msm_bo(struct drm_gem_object *obj) {
    return (struct msm_gem_object *)obj;
}
static void vm_flags_mod(struct vm_area_struct *vma, unsigned long set, unsigned long clear) {
    vma->vm_flags = (vma->vm_flags & ~clear) | set;
}
static unsigned long vm_get_page_prot(unsigned long flags) { return 0x100UL + flags; }
static unsigned long pgprot_writecombine(unsigned long protection) { return protection + 0x1000UL; }
static unsigned long pgprot_noncached(unsigned long protection) { return protection + 0x2000UL; }
static struct file *get_file(struct file *file) { assert(file->refs > 0); file->refs++; return file; }
static void fput(struct file *file) { if (file) { assert(file->refs > 0); file->refs--; } }
'''

tests = r'''
int main(void) {
    const unsigned flags[] = {MSM_BO_CACHED, MSM_BO_WC, MSM_BO_UNCACHED};
    unsigned cases = 0;
    for (unsigned cache = 0; cache < 3; cache++)
    for (unsigned backing = 0; backing < 2; backing++)
    for (unsigned alias = 0; alias < 2; alias++)
    for (unsigned index = 0; index < 3; index++) {
        struct file drm = {.refs = 2}, shmem = {.refs = 2};
        struct file *target = alias ? &drm : &shmem;
        struct msm_gem_object obj = {.base.filp = backing ? target : NULL, .flags = flags[cache]};
        unsigned long offset = 0x100000UL + index;
        struct vm_area_struct vma = {.vm_file = &drm, .vm_flags = VM_PFNMAP | VM_DONTEXPAND,
                                    .vm_pgoff = offset};
        assert(msm_gem_mmap_obj(&obj.base, &vma) == 0);
        assert(vma.vm_flags == (VM_MIXEDMAP | VM_DONTEXPAND));
        unsigned long protection = vm_get_page_prot(vma.vm_flags);
        if (cache == 1) protection = pgprot_writecombine(protection);
        if (cache == 2) protection = pgprot_noncached(protection);
        assert(vma.vm_page_prot == protection);
        if (cache == 0 && backing) {
            assert(vma.vm_file == target && vma.vm_pgoff == 0);
            assert(drm.refs == (alias ? 2 : 1));
            assert(shmem.refs == (alias ? 2 : 3));
        } else {
            assert(vma.vm_file == &drm && vma.vm_pgoff == offset);
            assert(drm.refs == 2 && shmem.refs == 2);
        }
        fput(vma.vm_file);
        if (cache == 0 && backing && !alias) assert(shmem.refs == 2 && drm.refs == 1);
        else assert(drm.refs == 1 && shmem.refs == 2);
        cases++;
    }
    printf("PASS: %u cached/WC/uncached, private/shmem, file alias and offset cases\n", cases);
    return 0;
}
'''

helper_source = (WORK / 'common/mm/util.c').read_text()
helper = function(helper_source, 'void vma_set_file(')
before = (HERE / 'before.c').read_text()
candidate = (HERE / 'candidate.c').read_text()
variants = {
    'candidate': candidate,
    'original': before,
    'unconditional_offset_reset': candidate.replace(
        '\t\t\tvma->vm_pgoff = 0;\n\t\t}', '\t\t}\n\t\tvma->vm_pgoff = 0;'),
}
results = {}
for name, source in variants.items():
    mmap = function(source, 'int msm_gem_mmap_obj(')
    code = adapter + helper + '\n' + mmap + '\n' + tests
    test = HERE / (name + '-test.c')
    executable = HERE / (name + '-test')
    test.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-g', '-O1', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(test), '-o', str(executable)], check=True)
    run = subprocess.run([str(executable)], capture_output=True, text=True)
    results[name] = {'returncode': run.returncode, 'stdout': run.stdout,
                     'stderr': run.stderr[:3000]}
    assert (run.returncode == 0) == (name == 'candidate'), (name, run.stderr)
report = {
    'status': 'PASS', 'cases': 36,
    'source_functions': ['msm_gem_mmap_obj', 'vma_set_file'],
    'source_sha256': {name: hashlib.sha256(source.encode()).hexdigest()
                      for name, source in [('before', before), ('candidate', candidate),
                                           ('vma_set_file', helper)]},
    'results': results,
    'scope': 'Actual extracted driver/kernel functions with host VMA/file/cache adapters; hardware recovery test pending',
}
(HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(results['candidate']['stdout'].strip())
print('PASS: original NULL-file crash and unconditional offset reset rejected')
