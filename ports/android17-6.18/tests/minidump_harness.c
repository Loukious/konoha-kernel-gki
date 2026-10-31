/* SPDX-License-Identifier: GPL-2.0-only */
/* Kernel services below are host-test shims, never linked into a module. */
#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t elf_addr_t;
#define elfhdr Elf64_Ehdr
#define elf_shdr Elf64_Shdr
#define elf_phdr Elf64_Phdr
/* Driver uses struct tags, while libc ELF types are anonymous typedefs. */
#define CONFIG_MINIDUMP_MAX_ENTRIES 200
#define MAX_NAME_LENGTH 12
#define MAX_REGION_NAME_LENGTH 16
#define MD_REGION_VALID 1
#define MD_SS_ENABLED 1
#define ELF_CLASS ELFCLASS64
#define ELF_DATA ELFDATA2LSB
#define ELF_OSABI ELFOSABI_NONE
#define ELF_ARCH EM_AARCH64
#define GFP_KERNEL 0
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define IS_ALIGNED(x, a) (!((x) & ((a) - 1)))
#define virt_to_phys(p) ((uintptr_t)(p) - 0x1000)
#define DEBUG_SYMBOL_LOOKUP(name) test_banner
#define spin_lock_irqsave(lock, flags) ((flags) = 0)
#define spin_unlock_irqrestore(lock, flags) ((void)(flags))
#define printk_deferred(...) ((void)0)
#define pr_err(...) ((void)0)
static char test_banner[] = "Linux version 6.18.33 test banner\n";
static void *kzalloc(size_t size, int flags)
{
	(void)flags;
	return calloc(1, size);
}
#define kfree free
static ssize_t strscpy(char *dest, const char *src, size_t size)
{
	size_t n = strnlen(src, size);
	if (!size)
		return -E2BIG;
	if (n == size) {
		memcpy(dest, src, size - 1);
		dest[size - 1] = 0;
		return -E2BIG;
	}
	memcpy(dest, src, n + 1);
	return n;
}
static int gh_rm_minidump_register_range(u64 address, size_t size,
					const char *name, size_t length)
{
	assert(address && size && length == strlen(name));
	return 0;
}

/* DRIVER_HELPERS */

static struct md_region regions[CONFIG_MINIDUMP_MAX_ENTRIES];

static void setup(bool rm)
{
	is_rm_minidump = rm;
	num_regions = 0;
	first_removed_entry = INT_MAX;
	memset(&minidump_elfheader, 0, sizeof(minidump_elfheader));
	minidump_table = calloc(1, sizeof(*minidump_table));
	minidump_table->md_ss_toc = calloc(1, sizeof(*minidump_table->md_ss_toc));
	minidump_table->md_regions = calloc(MAX_NUM_ENTRIES, sizeof(struct md_ss_region));
	minidump_table->md_ss_toc->ss_region_count = 1;
	minidump_table->md_ss_toc->md_ss_enable_status = MD_SS_ENABLED;
	assert(msm_minidump_add_header() == 0);
	md_init_done = true;
	for (unsigned int i = 0; i < CONFIG_MINIDUMP_MAX_ENTRIES; i++) {
		snprintf(regions[i].name, sizeof(regions[i].name), "ENTRY%07u", i);
		regions[i].virt_addr = 0x100000000UL + 0x2000 * i;
		regions[i].phys_addr = 0x80000000UL + 0x2000 * i;
		regions[i].size = 0x1000;
	}
}

static void teardown(void)
{
	free(minidump_elfheader.ehdr);
	free(minidump_table->md_regions);
	free(minidump_table->md_ss_toc);
	free(minidump_table);
}

static void check_elf(unsigned int count)
{
	struct elfhdr *hdr = minidump_elfheader.ehdr;
	unsigned int base = is_rm_minidump ? 3 : 4;
	assert(hdr->e_shnum == count + base);
	assert(hdr->e_phnum == count + 1);
	assert(hdr->e_shnum <= MAX_ELF_HEADERS);
	assert(minidump_elfheader.strtable_idx < MAX_STRTBL_SIZE);
	assert(!strcmp(elf_lookup_string(hdr, elf_section(hdr, base - 1)->sh_name),
		       "linux_banner"));
	for (unsigned int i = 0; i < count; i++) {
		struct elf_shdr *sh = elf_section(hdr, i + base);
		struct elf_phdr *ph = elf_program(hdr, i + 1);
		struct md_region found = md_get_region(elf_lookup_string(hdr, sh->sh_name));
		assert(found.virt_addr == ph->p_vaddr);
		assert(found.phys_addr == ph->p_paddr);
		assert(found.size == sh->sh_size);
		assert(sh->sh_addr == ph->p_vaddr);
		assert(sh->sh_offset == ph->p_offset);
		assert(sh->sh_size == ph->p_filesz);
		if (i)
			assert(ph->p_offset == elf_program(hdr, i)->p_offset +
			       elf_program(hdr, i)->p_filesz);
	}
}

int main(void)
{
	for (unsigned int mode = 0; mode < 2; mode++) {
		for (unsigned int removed = 0; removed < CONFIG_MINIDUMP_MAX_ENTRIES; removed++) {
			setup(mode);
			/* Pre-probe reservations already contribute to num_regions. */
			num_regions = CONFIG_MINIDUMP_MAX_ENTRIES;
			for (unsigned int i = 0; i < num_regions; i++) {
				assert(!validate_region(&regions[i]));
				if (!mode)
					md_add_ss_toc(&regions[i], i);
				md_add_elf_header(&regions[i]);
			}
			check_elf(num_regions);
			assert(md_elf_entry_number(&regions[removed]) == (int)(removed + (mode ? 3 : 4)));
			struct md_region changed = regions[removed];
			changed.virt_addr += 0x10000000;
			changed.phys_addr += 0x10000000;
			if (!mode) {
				assert(md_update_ss_toc((removed + 1) % num_regions, &changed) == -EINVAL);
				assert(!md_update_ss_toc(removed, &changed));
			}
			md_update_elf_header(removed, &changed);
			check_elf(num_regions);
			struct md_region invalid = changed;
			invalid.size += 4;
			assert(msm_minidump_clear_headers(&invalid) == -EINVAL);
			check_elf(num_regions);
			if (mode) {
				assert(!msm_minidump_clear_headers(&changed));
				num_regions--;
			} else {
				assert(md_remove_ss_toc(&invalid) == -EINVAL);
				assert(!md_remove_ss_toc(&changed));
			}
			check_elf(num_regions);
			assert(!md_get_region(changed.name).size);
			if (!mode)
				md_add_ss_toc(&regions[removed], num_regions);
			md_add_elf_header(&regions[removed]);
			num_regions++;
			check_elf(num_regions);
			teardown();
		}
	}
	puts("PASS: 400 SMEM/RM capacity, update, removal and refill scenarios");
	return 0;
}
