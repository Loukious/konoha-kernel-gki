#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#define KERNEL_VERSION(a,b,c) (((a) << 16) | ((b) << 8) | (c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,33)
#define CAM_DBG(...) ((void)0)
struct dma_buf { unsigned int dummy; } buffer;
struct iosys_map { union { void *vaddr; void *vaddr_iomem; }; bool is_iomem; };
#define IOSYS_MAP_INIT_VADDR(p) { .vaddr = (p), .is_iomem = false }
static int mode, maps, unmaps;
static char memory[32];

static bool iosys_map_is_null(const struct iosys_map *map)
{
	return map->vaddr == NULL;
}

static int dma_buf_vmap_unlocked(struct dma_buf *buf, struct iosys_map *map)
{
	assert(buf == &buffer);
	if (mode == 3)
		return -ENOMEM;
	map->is_iomem = mode == 1;
	map->vaddr = mode == 2 ? NULL : memory;
	maps++;
	return 0;
}

static void dma_buf_vunmap_unlocked(struct dma_buf *buf, struct iosys_map *map)
{
	assert(buf == &buffer);
	assert(map->is_iomem == (mode == 1));
	assert(map->vaddr == (mode == 2 ? NULL : memory));
	unmaps++;
	assert(unmaps == maps);
}

#include "vmap.inc"

int main(void)
{
	for (mode = 0; mode < 4; mode++) {
		uintptr_t address = UINTPTR_MAX;
		maps = unmaps = 0;
		int rc = cam_compat_util_get_dmabuf_va(&buffer, &address);
		if (mode == 0) {
			assert(rc == 0 && address == (uintptr_t)memory);
			assert(maps == 1 && unmaps == 0);
			cam_compat_util_put_dmabuf_va(&buffer, (void *)address);
		} else {
			assert(rc == (mode == 3 ? -ENOMEM : -EOPNOTSUPP));
			assert(address == 0);
		}
		assert(maps == unmaps);
	}
	puts("{\"cases\":4}");
	return 0;
}
