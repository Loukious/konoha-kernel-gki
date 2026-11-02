#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

typedef uint32_t __be32;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t phys_addr_t;
#define __free(x)
#define OF_BAD_ADDR UINT64_MAX
struct device_node { struct device_node *parent; int address_cells, size_cells; };
static bool of_property_present(struct device_node *n, const char *name)
{
    return !strcmp(name, "#address-cells") ? n->address_cells >= 0 : n->size_cells >= 0;
}
static int of_bus_n_addr_cells(struct device_node *n)
{
    for (; n; n=n->parent) if (n->address_cells >= 0) return n->address_cells;
    return 2;
}
static int of_bus_n_size_cells(struct device_node *n)
{
    for (; n; n=n->parent) if (n->size_cells >= 0) return n->size_cells;
    return 1;
}
static struct device_node *__of_get_dma_parent(struct device_node *n) { return n->parent; }
static u64 of_read_number(const __be32 *p, int n)
{
    u64 value = 0;
    for (int i=0; i<n; i++) value = (value << 32) | __builtin_bswap32(p[i]);
    return value;
}
static u64 of_translate_dma_address(struct device_node *n, const __be32 *p);
#include "helper.inc"
static u64 of_translate_dma_address(struct device_node *n, const __be32 *p)
{
    /* This fixture contains only default buses and no DMA ranges. Match the
     * upstream translator's bus-selection failure and identity DMA path. */
    for (struct device_node *bus = n->parent; bus; bus = bus->parent)
        if (!of_bus_default_match(bus)) return OF_BAD_ADDR;
    return of_read_number(p, of_bus_n_addr_cells(n->parent));
}
#include "fixtures.inc"
int main(void)
{
    struct device_node root = {.parent=NULL, .address_cells=2, .size_cells=2};
    struct device_node soc = {.parent=&root, .address_cells=1, .size_cells=1};
    struct device_node cvp = {.parent=&soc, .address_cells=-1, .size_cells=-1};
    struct device_node client = {.parent=&cvp, .address_cells=-1, .size_cells=-1};
    phys_addr_t start = UINT64_MAX;
    size_t length = SIZE_MAX;
    assert(of_translate_dma_region(&client, fixtures[0].cells, &start, &length) == NULL);
    assert(start == UINT64_MAX && length == SIZE_MAX);
    cvp.address_cells = fixture_address_cells;
    cvp.size_cells = fixture_size_cells;
    for (unsigned i=0; i<sizeof(fixtures)/sizeof(fixtures[0]); i++) {
        const __be32 *next = of_translate_dma_region(&client, fixtures[i].cells, &start, &length);
        assert(next == fixtures[i].cells + 2);
        assert(start == fixtures[i].start && length == fixtures[i].length && length);
    }
    puts("PASS: native parent rejected by Linux 6.18; all eight adapted DMA reservations preserved");
}
