#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the actual ARM64 loader fixup against ELF files without loading them.

Usage: tools/test_module_exit_relocation.py STOCK.ko COMPATIBLE.ko [MODULE_DIR ...]
Requires a host C compiler. Inputs are read-only; mutations use private copies.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


HARNESS = r'''
#include <elf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef Elf64_Ehdr Elf_Ehdr;
typedef Elf64_Shdr Elf_Shdr;
typedef Elf64_Sym Elf_Sym;
typedef Elf64_Rela Elf_Rela;
typedef Elf64_Addr Elf_Addr;
#define ELF_ST_TYPE ELF64_ST_TYPE
#define MODULE_NAME_LEN 56
#define IS_ALIGNED(x, a) (!((x) & ((a) - 1)))
#ifndef TEST_EXIT_OFFSET
#define TEST_EXIT_OFFSET 0x5a0
#endif

/* Only the named fields used by the production function are needed here. */
struct module {
    char prefix[0x18];
    char name[MODULE_NAME_LEN];
    char before_init[0x188 - 0x18 - MODULE_NAME_LEN];
    int (*init)(void);
    char before_exit[TEST_EXIT_OFFSET - 0x188 - sizeof(void *)];
    void (*exit)(void);
    char tail[0x600 - TEST_EXIT_OFFSET - sizeof(void *)];
};
struct load_info {
    Elf_Ehdr *hdr;
    Elf_Shdr *sechdrs;
    struct module *mod;
    const char *name, *strtab, *secstrings;
    struct { unsigned int mod, sym, str; } index;
};
static unsigned int corrections;
#define pr_info(...) (++corrections)
static void *memchr_inv(const void *p, int value, size_t size)
{
    const unsigned char *bytes = p;
    for (size_t i = 0; i < size; i++)
        if (bytes[i] != value)
            return (void *)(bytes + i);
    return NULL;
}
static bool module_init_layout_section(const char *text)
{
    return strncmp(text, ".init", 5) == 0 || !strcmp(text, ".test.init");
}

/* PRODUCTION_FUNCTION */

static void require(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    require(argc == 4, "expected file, mutation, expected correction count");
    FILE *file = fopen(argv[1], "rb");
    require(file != NULL, "cannot open fixture");
    require(fseek(file, 0, SEEK_END) == 0, "cannot seek fixture");
    long length = ftell(file);
    require(length > (long)sizeof(Elf_Ehdr), "short fixture");
    rewind(file);
    unsigned char *data = malloc(length), *before = malloc(length);
    require(data && before, "allocation failed");
    require(fread(data, 1, length, file) == (size_t)length, "short read");
    fclose(file);

    struct load_info info = { .hdr = (void *)data };
    require(!memcmp(info.hdr->e_ident, ELFMAG, SELFMAG) &&
            info.hdr->e_ident[EI_CLASS] == ELFCLASS64 &&
            info.hdr->e_machine == EM_AARCH64 &&
            info.hdr->e_shentsize == sizeof(Elf_Shdr), "not ARM64 ELF64");
    require(info.hdr->e_shoff <= (size_t)length &&
            info.hdr->e_shnum <= (length - info.hdr->e_shoff) / sizeof(Elf_Shdr),
            "invalid section table");
    info.sechdrs = (void *)(data + info.hdr->e_shoff);
    require(info.hdr->e_shstrndx < info.hdr->e_shnum, "invalid section names");
    for (unsigned int i = 0; i < info.hdr->e_shnum; i++) {
        Elf_Shdr *sec = &info.sechdrs[i];
        if (sec->sh_type != SHT_NOBITS)
            require(sec->sh_offset <= (size_t)length &&
                    sec->sh_size <= length - sec->sh_offset, "invalid section");
        sec->sh_addr = (uintptr_t)(data + sec->sh_offset);
    }
    info.secstrings = (void *)info.sechdrs[info.hdr->e_shstrndx].sh_addr;
    for (unsigned int i = 1; i < info.hdr->e_shnum; i++) {
        Elf_Shdr *sec = &info.sechdrs[i];
        if (!strcmp(info.secstrings + sec->sh_name, ".gnu.linkonce.this_module"))
            info.index.mod = i;
        if (sec->sh_type == SHT_SYMTAB)
            info.index.sym = i;
    }
    require(info.index.mod && info.index.sym, "missing module/symbol sections");
    info.mod = (void *)info.sechdrs[info.index.mod].sh_addr;
    info.name = info.mod->name;
    info.index.str = info.sechdrs[info.index.sym].sh_link;
    require(info.index.str < info.hdr->e_shnum, "invalid string table");
    info.strtab = (void *)info.sechdrs[info.index.str].sh_addr;
    Elf_Sym *syms = (void *)info.sechdrs[info.index.sym].sh_addr;
    Elf_Rela *exit_rel = NULL, *init_rel = NULL;
    Elf_Shdr *relsec = NULL;
    for (unsigned int i = 1; i < info.hdr->e_shnum; i++) {
        Elf_Shdr *sec = &info.sechdrs[i];
        if (sec->sh_type != SHT_RELA || sec->sh_info != info.index.mod)
            continue;
        relsec = sec;
        Elf_Rela *rels = (void *)sec->sh_addr;
        for (size_t j = 0; j < sec->sh_size / sizeof(*rels); j++) {
            const char *name = info.strtab + syms[ELF64_R_SYM(rels[j].r_info)].st_name;
            if (!strcmp(name, "cleanup_module")) exit_rel = &rels[j];
            if (!strcmp(name, "init_module")) init_rel = &rels[j];
        }
    }
    const char *mutation = argv[2];
    if (strcmp(mutation, "none")) {
        require(exit_rel && init_rel && relsec, "mutation requires standard callbacks");
        Elf_Sym *cleanup = &syms[ELF64_R_SYM(exit_rel->r_info)];
        Elf_Shdr *text = &info.sechdrs[cleanup->st_shndx];
        if (!strcmp(mutation, "other-name")) strcpy(info.mod->name, "another_vendor_driver");
        else if (!strcmp(mutation, "other-offset")) exit_rel->r_offset = 0x5d0;
        else if (!strcmp(mutation, "already-compatible")) exit_rel->r_offset = TEST_EXIT_OFFSET;
        else if (!strcmp(mutation, "init-mismatch")) init_rel->r_offset += 8;
        else if (!strcmp(mutation, "no-cleanup")) relsec->sh_size = sizeof(Elf_Rela);
        else if (!strcmp(mutation, "extra-relocation")) relsec->sh_size += sizeof(Elf_Rela);
        else if (!strcmp(mutation, "nonzero-data")) ((char *)info.mod)[0x100] = 1;
        else if (!strcmp(mutation, "bad-symbol")) exit_rel->r_info = ELF64_R_INFO(0xffffff, R_AARCH64_ABS64);
        else if (!strcmp(mutation, "bad-string")) cleanup->st_name = info.sechdrs[info.index.str].sh_size;
        else if (!strcmp(mutation, "not-function")) cleanup->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
        else if (!strcmp(mutation, "not-executable")) text->sh_flags &= ~SHF_EXECINSTR;
        else if (!strcmp(mutation, "freed-init-text")) {
            /* Rewrite a private copy of the section name to an init section. */
            strcpy((char *)info.secstrings + text->sh_name, ".init.text");
        }
        else if (!strcmp(mutation, "custom-init-text"))
            strcpy((char *)info.secstrings + text->sh_name, ".test.init");
        else if (!strcmp(mutation, "out-of-bounds")) exit_rel->r_offset = sizeof(struct module);
        else if (!strcmp(mutation, "unaligned")) exit_rel->r_offset++;
        else if (!strcmp(mutation, "overlap-init")) exit_rel->r_offset = init_rel->r_offset;
        else if (!strcmp(mutation, "overlap-name")) exit_rel->r_offset = offsetof(struct module, name);
        else if (!strcmp(mutation, "addend")) exit_rel->r_addend = 1;
        else if (!strcmp(mutation, "wrong-type")) exit_rel->r_info = ELF64_R_INFO(ELF64_R_SYM(exit_rel->r_info), R_AARCH64_ABS32);
        else if (!strcmp(mutation, "wrong-link")) relsec->sh_link = 0;
        else if (!strcmp(mutation, "wrong-size")) info.sechdrs[info.index.mod].sh_size += 64;
        else require(false, "unknown mutation");
    }
    memcpy(before, data, length);
    fixup_module_exit_relocation(&info);
    int expected = atoi(argv[3]);
    require(expected < 0 || corrections == (unsigned int)expected, "unexpected correction count");
    if (corrections) {
        require(exit_rel && corrections == 1 && exit_rel->r_offset == TEST_EXIT_OFFSET,
                "incorrect correction");
        /* After undoing the sole allowed write, every byte must match. */
        size_t offset = (unsigned char *)&exit_rel->r_offset - data;
        memcpy(&exit_rel->r_offset, before + offset, sizeof(exit_rel->r_offset));
    }
    require(!memcmp(before, data, length), "unexpected mutation outside exit relocation");
    printf("%u\n", corrections);
    free(before);
    free(data);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path)
    parser.add_argument("compatible", type=Path)
    parser.add_argument("directories", type=Path, nargs="*")
    args = parser.parse_args()
    source = (Path(__file__).resolve().parent.parent / "kernel/module/main.c").read_text()
    start = source.index("static void fixup_module_exit_relocation(")
    function = source[start:source.index("\n#else", start)]
    harness = HARNESS.replace("/* PRODUCTION_FUNCTION */", function)
    negative = [
        "already-compatible", "init-mismatch", "no-cleanup", "extra-relocation",
        "nonzero-data", "bad-symbol", "bad-string", "not-function", "not-executable",
        "freed-init-text", "custom-init-text", "out-of-bounds", "unaligned", "overlap-init", "overlap-name",
        "addend", "wrong-type", "wrong-link", "wrong-size",
    ]
    with tempfile.TemporaryDirectory(prefix="module-exit-test-") as tmp:
        tmp = Path(tmp)
        c_file = tmp / "test.c"
        c_file.write_text(harness)
        count = 0
        for target in (0x5a0, 0x590):
            binary = tmp / f"test-{target:x}"
            subprocess.run([
                os.environ.get("HOSTCC", "cc"), "-std=gnu11", "-O1", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                f"-DTEST_EXIT_OFFSET={target}", str(c_file), "-o", str(binary),
            ], check=True)

            def run(path, mutation, expected):
                nonlocal count
                result = subprocess.run(
                    [str(binary), str(path.resolve()), mutation, str(expected)],
                    text=True, capture_output=True,
                )
                if result.returncode:
                    raise RuntimeError(f"{path}: {mutation}, target {target:#x}\n{result.stderr}")
                count += 1
                return int(result.stdout.strip())

            for mutation in ("none", "other-name", "other-offset"):
                run(args.stock, mutation, 1)
            for mutation in negative:
                run(args.stock, mutation, 0)
            run(args.compatible, "none", int(target != 0x5a0))
            if target == 0x5a0:
                files = sorted({p for d in args.directories for p in d.rglob("*.ko")})
                fixed = sum(run(p, "none", -1) for p in files)
                print(f"Corpus: {len(files)} modules, {fixed} recognized corrections, "
                      f"{len(files) - fixed} left byte-identical")
        print(f"PASS: {count} cases, two target layouts, ASan/UBSan enabled")


if __name__ == "__main__":
    main()
