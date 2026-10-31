/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
typedef uint64_t phys_addr_t;
#define BIT(n) (1UL << (n))
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define GFP_KERNEL 0
#define GPI_DBG_COMMON 0
#define GPII_INFO(...) ((void)0)
#define GPII_CRITIC(...) ((void)0)
#define SPI_LOG_ERR(...) ((void)0)
#define smp_wmb() ((void)0)
#define DMA_MAPPING_ERROR UINT64_MAX
static bool is_power_of_2(u32 n) { return n && !(n & (n - 1)); }
static u32 roundup_pow_of_two(u32 n) { u32 p = 1; while (p < n) p <<= 1; return p; }
struct device { int unused; };
struct clk { int index, enabled; };
static int clk_fail, clk_call, clk_log[16], clk_log_count;
static int clk_prepare_enable(struct clk *clk)
{
    if (++clk_call == clk_fail) return -EIO;
    assert(!clk->enabled++);
    clk_log[clk_log_count++] = clk->index;
    return 0;
}
static void clk_disable_unprepare(struct clk *clk)
{
    assert(clk->enabled-- == 1);
    clk_log[clk_log_count++] = -clk->index;
}
static bool alloc_fail;
static dma_addr_t alloc_dma;
static void *allocation;
static void *dma_alloc_coherent(struct device *dev, size_t bytes, dma_addr_t *dma, int flags)
{
    (void)dev; (void)flags;
    if (alloc_fail) return NULL;
    assert(!allocation);
    allocation = malloc(bytes);
    assert(allocation);
    *dma = alloc_dma;
    return allocation;
}
static void dma_free_coherent(struct device *dev, size_t bytes, void *ptr, dma_addr_t dma)
{
    (void)dev; (void)bytes;
    assert(ptr == allocation && dma == alloc_dma);
    free(ptr); allocation = NULL;
}
struct gpi_dev { struct device *dev; u32 max_gpii, static_gpii_mask; };
#define READ_ONCE(x) (x)
#define SE_PROTOCOL_UART 2
#define MAX_CHANNELS_PER_GPII 2
struct gpii;
struct dma_chan { int unused; };
struct virt_dma_chan { struct dma_chan chan; int index; bool pending; };
struct tasklet_struct { bool pending; };
struct gpii_chan { struct virt_dma_chan vc; struct gpii *gpii; int chid; };
struct gpii {
    struct gpi_dev *gpi_dev;
    struct gpii_chan gpii_chan[2];
    bool configured_irq;
    int irq, protocol;
    struct tasklet_struct ev_task;
};
static struct gpii_chan *to_gpii_chan(struct dma_chan *chan)
{
    return (struct gpii_chan *)chan;
}
static int sync_order[4], sync_count;
static void synchronize_irq(int irq) { assert(irq == 7); sync_order[sync_count++] = 1; }
static void tasklet_kill(struct tasklet_struct *task) { task->pending = false; sync_order[sync_count++] = 2; }
static void vchan_synchronize(struct virt_dma_chan *vc) { vc->pending = false; sync_order[sync_count++] = 3 + vc->index; }

struct list_head { struct list_head *next, *prev; };
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define list_for_each_entry(pos, head, member) \
    for (struct list_head *node = (head)->next; \
         node != (head) && ((pos) = container_of(node, __typeof__(*(pos)), member), 1); \
         node = node->next)
struct spi_transfer {
    void *rx_buf, *tx_buf;
    dma_addr_t rx_dma, tx_dma;
    size_t len;
    struct list_head transfer_list;
};
struct spi_message { struct list_head transfers; };
struct spi_geni_master { struct device *wrapper_dev, *dev; void *ipc; bool is_le_vm, irq_enabled; int irq; };
enum dma_data_direction { DMA_FROM_DEVICE, DMA_TO_DEVICE };
static int map_call, map_fail, map_active, map_total;
static struct { bool active; enum dma_data_direction dir; size_t size; } maps[64];
static dma_addr_t dma_map_single(struct device *dev, void *buf, size_t len, enum dma_data_direction dir)
{
    (void)dev; assert(buf && len);
    if (++map_call == map_fail) return DMA_MAPPING_ERROR;
    dma_addr_t index = map_total++;
    assert(index < 64 && !maps[index].active);
    maps[index].active = true; maps[index].dir = dir; maps[index].size = len;
    map_active++;
    return index; /* Address zero is a valid successful mapping. */
}
static bool dma_mapping_error(struct device *dev, dma_addr_t dma)
{
    (void)dev; return dma == DMA_MAPPING_ERROR;
}
static void dma_unmap_single(struct device *dev, dma_addr_t dma, size_t len, enum dma_data_direction dir)
{
    (void)dev; assert(dma < 64 && maps[dma].active && maps[dma].dir == dir && maps[dma].size == len);
    maps[dma].active = false; map_active--;
}
static int irq_level, irq_calls;
static void enable_irq(int irq) { assert(irq == 7 && irq_level++ == 0); irq_calls++; }
static void disable_irq(int irq) { assert(irq == 7 && irq_level-- == 1); irq_calls++; }
/* STAGED_DEFINITIONS */
static unsigned long test_ring(void)
{
    unsigned long cases = 0;
    for (u32 e = 0; e <= 65536; e++) {
        for (u32 sz = 0; sz <= 64; sz++) {
            u32 len = UINT32_MAX;
            int ret = gpi_ring_len(e, sz, &len);
            bool good = e >= 2 && sz && !(sz & (sz - 1)) && (u64)e * sz <= 32768;
            assert((ret == 0) == good);
            if (good) assert(len >= (u64)e * sz && is_power_of_2(len) && len <= 32768);
            else assert(len == UINT32_MAX);
            cases++;
        }
    }
    u32 extremes[] = {UINT32_MAX, UINT32_MAX - 1, 1U << 31, 1U << 30};
    for (size_t a = 0; a < 4; a++) for (size_t b = 0; b < 4; b++) {
        u32 len = 0; assert(gpi_ring_len(extremes[a], extremes[b], &len) == -EINVAL); cases++;
    }
    struct device dev = {};
    struct gpi_dev gdev = {.dev = &dev};
    struct gpii g = {.gpi_dev = &gdev};
    struct gpi_ring ring = {};
    for (unsigned int shift = 5; shift <= 15; shift++) {
        u32 len = 1U << shift;
        for (u32 offset = 0; offset < len; offset += 17) {
            alloc_dma = (1ULL << 36) + offset;
            assert(!gpi_alloc_ring(&ring, len / 16, 16, &g));
            assert(ring.len == len && ring.elements == len / 16);
            assert(ring.phys_addr >= alloc_dma && !(ring.phys_addr & (len - 1)));
            assert(ring.phys_addr - alloc_dma < len);
            assert((char *)ring.base + len <= (char *)ring.pre_aligned + ring.alloc_size);
            assert(gpi_alloc_ring(&ring, 2, 16, &g) == -EBUSY);
            memset(ring.base, 0xa5, len);
            gpi_free_ring(&ring, &g); gpi_free_ring(&ring, &g); cases++;
        }
    }
    alloc_fail = true;
    assert(gpi_alloc_ring(&ring, 2, 16, &g) == -ENOMEM && !ring.configured);
    gpi_free_ring(&ring, &g);
    alloc_fail = false;
    return cases;
}
static unsigned long test_mapping(void)
{
    unsigned long cases = 0;
    struct device dev = {};
    struct spi_geni_master mas = {.dev = &dev, .wrapper_dev = &dev};
    char bytes[8][8] = {};
    for (int code = 0; code < 6561; code++) {
        for (int fail = 0; fail <= 16; fail++) {
            struct spi_message msg;
            struct spi_transfer x[8] = {};
            struct list_head *prev = &msg.transfers;
            int bits = code;
            for (int i = 0; i < 8; i++) {
                int mode = bits % 3; bits /= 3;
                x[i].len = i + 1;
                x[i].rx_buf = mode != 0 ? bytes[i] : NULL;
                x[i].tx_buf = mode != 1 ? bytes[i] : NULL;
                prev->next = &x[i].transfer_list; x[i].transfer_list.prev = prev; prev = prev->next;
            }
            prev->next = &msg.transfers; msg.transfers.prev = prev;
            memset(maps, 0, sizeof(maps)); map_call = map_active = map_total = 0; map_fail = fail;
            int ret = spi_geni_map_buf(&mas, &msg);
            if (ret) { assert(ret == -EIO && !map_active && map_call == fail); }
            else { assert(map_active >= 8); }
            spi_geni_unmap_buf(&mas, &msg);
            spi_geni_unmap_buf(&mas, &msg);
            assert(!map_active);
            cases++;
        }
    }
    return cases;
}
static void test_clocks(void)
{
    for (int fail = 0; fail <= 3; fail++) {
        struct clk m = {1, 0}, s = {2, 0}, se = {3, 0};
        clk_fail = fail; clk_call = clk_log_count = 0;
        int ret = geni_se_common_clks_on(&se, &m, &s);
        assert(ret == (fail ? -EIO : 0));
        if (!ret) geni_se_common_clks_off(&se, &m, &s);
        assert(!m.enabled && !s.enabled && !se.enabled);
        int count = fail ? fail - 1 : 3;
        assert(clk_log_count == 2 * count);
        for (int i = 0; i < count; i++) assert(clk_log[i] == -clk_log[2 * count - i - 1]);
    }
}
static void test_irq(void)
{
    for (int guest = 0; guest < 2; guest++) for (int bits = 0; bits < 65536; bits++) {
        struct spi_geni_master mas = {.irq = 7, .is_le_vm = guest};
        irq_level = irq_calls = 0;
        for (int i = 0; i < 16; i++) {
            spi_geni_set_irq(&mas, !!(bits & (1 << i)));
            assert(irq_level == (guest ? 0 : !!(bits & (1 << i))));
        }
        spi_geni_set_irq(&mas, false);
        assert(!irq_level && !mas.irq_enabled);
    }
}
static void test_synchronize(void)
{
    for (int irq = 0; irq < 2; irq++) for (int uart = 0; uart < 2; uart++) for (int channel = 0; channel < 2; channel++) {
        struct gpii g = {.irq = 7, .configured_irq = irq, .protocol = uart ? SE_PROTOCOL_UART : 1, .ev_task.pending = true};
        for (int i = 0; i < 2; i++) {
            g.gpii_chan[i].gpii = &g; g.gpii_chan[i].chid = i;
            g.gpii_chan[i].vc.index = i; g.gpii_chan[i].vc.pending = true;
        }
        sync_count = 0;
        gpi_synchronize(&g.gpii_chan[channel].vc.chan);
        assert(!g.ev_task.pending && !g.gpii_chan[channel].vc.pending);
        assert(g.gpii_chan[1 - channel].vc.pending == !!uart);
        int off = 0;
        if (irq) assert(sync_order[off++] == 1);
        assert(sync_order[off++] == 2);
        if (uart) assert(sync_order[off++] == 3 + channel);
        else { assert(sync_order[off++] == 3); assert(sync_order[off++] == 4); }
        assert(sync_count == off);
    }
}
int main(void)
{
    unsigned long rings = test_ring(), maps_count = test_mapping();
    test_clocks(); test_irq(); test_synchronize();
    printf("{\"ring_cases\":%lu,\"mapping_failure_cases\":%lu,\"clock_sequences\":4,\"irq_sequences\":131072}\n", rings, maps_count);
    return 0;
}
