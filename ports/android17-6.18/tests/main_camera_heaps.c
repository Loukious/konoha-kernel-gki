#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define CAM_DBG(...) ((void)0)
#define CAM_ERR(...) ((void)0)
#define CAM_INFO(...) ((void)0)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
struct dma_heap { int refs; } heaps[10];
static unsigned int missing;
static bool error_pointer;
static struct {
	struct dma_heap *system_heap, *system_movable_heap, *system_uncached_heap;
	struct dma_heap *camera_heap, *camera_uncached_heap, *secure_display_heap;
	struct dma_heap *ubwc_p_heap, *ubwc_p_movable_heap;
	struct dma_heap *cam_device_heap, *cam_session_heap;
	bool force_cache_allocs;
} tbl;

static struct dma_heap *dma_heap_find(const char *name)
{
	const char *names[] = { "qcom,system", "qcom,system-movable",
		"qcom,system-uncached", "qcom,camera", "qcom,camera-uncached",
		"qcom,display", "qcom,ubwcp", "qcom,ubwcp-movable",
		"qcom,cam-svm-device", "qcom,cam-svm-session" };
	for (unsigned int i = 0; i < 10; ++i) {
		if (strcmp(name, names[i]))
			continue;
		if (missing & (1U << i))
			return error_pointer ? (void *)(intptr_t)-EIO : NULL;
		assert(heaps[i].refs == 0);
		heaps[i].refs++;
		return &heaps[i];
	}
	assert(0);
	return NULL;
}

static void dma_heap_put(struct dma_heap *heap)
{
	assert(heap >= heaps && heap < heaps + 10);
	assert(heap->refs == 1);
	heap->refs--;
}

#include "heaps.inc"

int main(void)
{
	unsigned int cases = 0;
	for (unsigned int err = 0; err < 2; err++) {
		for (unsigned int cached = 0; cached < 2; cached++) {
#ifdef CONFIG_ARCH_QTI_VM
			const unsigned int count = 4;
#else
			const unsigned int count = 256;
#endif
			for (unsigned int mask = 0; mask < count; mask++) {
				memset(&tbl, 0, sizeof(tbl));
				memset(heaps, 0, sizeof(heaps));
				error_pointer = err;
				tbl.force_cache_allocs = cached;
#ifdef CONFIG_ARCH_QTI_VM
				missing = mask << 8;
				bool required_missing = mask != 0;
#else
				missing = mask;
				bool required_missing = (mask & ((1U << 0) | (1U << 5))) ||
					(!cached && (mask & (1U << 2)));
#endif
				int rc = cam_mem_mgr_get_dma_heaps();
				assert(rc == (required_missing ? (err ? -EIO : -ENODEV) : 0));
				if (required_missing) {
					for (unsigned int i = 0; i < 10; ++i)
						assert(heaps[i].refs == 0);
				}
				cam_mem_mgr_put_dma_heaps();
				cam_mem_mgr_put_dma_heaps();
				for (unsigned int i = 0; i < 10; ++i)
					assert(heaps[i].refs == 0);
				cases++;
			}
		}
	}
	printf("{\"cases\":%u}\n", cases);
	return 0;
}
