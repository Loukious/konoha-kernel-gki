#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define __iomem
#define IORESOURCE_MEM 0x200
#define ERR_PTR(code) ((void *)(intptr_t)(code))
#define PTR_ERR(ptr) ((intptr_t)(ptr))
#define resource_size(res) ((res)->end - (res)->start + 1)

struct resource { uintptr_t start, end; unsigned flags; };
struct device_node { bool duplicate; };
struct device { struct device_node *of_node; };
static bool boot_trace;
static int translation_error, shared_calls, exclusive_calls, selected_index;
static bool no_real_name, fail_mapping;
static uintptr_t mapped_start, mapped_size;
static struct resource real;

static bool of_property_read_bool(struct device_node *node, const char *name)
{
    assert(!strcmp(name, "qcom,duplicate-funnel"));
    return node && node->duplicate;
}
static int of_property_match_string(struct device_node *node,
                                    const char *property, const char *value)
{
    assert(node && !strcmp(property, "reg-names"));
    if (!strcmp(value, "funnel-base-dummy")) return 0;
    assert(!strcmp(value, "funnel-base-real"));
    return no_real_name ? -ENODATA : 1;
}
static int of_address_to_resource(struct device_node *node, int index,
                                  struct resource *res)
{
    assert(node);
    selected_index = index;
    *res = real;
    if (index == 0) res->start = 0x10940000;
    return translation_error;
}
static __attribute__((unused)) void *devm_ioremap(struct device *dev, uintptr_t start, uintptr_t size)
{
    assert(dev);
    shared_calls++;
    mapped_start = start;
    mapped_size = size;
    return fail_mapping ? NULL : (void *)0x12340;
}
static __attribute__((unused)) void *devm_ioremap_resource(struct device *dev, struct resource *res)
{
    assert(dev && res);
    exclusive_calls++;
    mapped_start = res->start;
    mapped_size = resource_size(res);
    return fail_mapping ? ERR_PTR(-EBUSY) : (void *)0x12340;
}
#define dev_info(dev, fmt, ...) ((void)(dev))
#include "helper.inc"

static void reset(void)
{
    translation_error = shared_calls = exclusive_calls = 0;
    no_real_name = fail_mapping = false;
    mapped_start = mapped_size = 0;
    selected_index = -1;
    real = (struct resource){0x10983000, 0x10983fff, IORESOURCE_MEM};
}
int main(void)
{
    struct device_node node = {false};
    struct device dev = {&node};
    struct resource dummy = {0x10940000, 0x10940fff, IORESOURCE_MEM};
    reset();
    assert(funnel_ioremap(&dev, &dummy) == (void *)0x12340);
    assert(exclusive_calls == 1 && shared_calls == 0 && mapped_start == dummy.start);
    reset(); fail_mapping = true;
    assert(PTR_ERR(funnel_ioremap(&dev, &dummy)) == -EBUSY);
    node.duplicate = true;
    reset();
    assert(funnel_ioremap(&dev, &dummy) == (void *)0x12340);
    assert(selected_index == 1 && shared_calls == 1 && exclusive_calls == 0);
    assert(mapped_start == real.start && mapped_size == 0x1000);
    reset(); no_real_name = true;
    assert(PTR_ERR(funnel_ioremap(&dev, &dummy)) == -ENODATA);
    assert(shared_calls == 0 && exclusive_calls == 0);
    reset(); translation_error = -EPROBE_DEFER;
    assert(PTR_ERR(funnel_ioremap(&dev, &dummy)) == -EPROBE_DEFER);
    assert(shared_calls == 0 && exclusive_calls == 0);
    reset(); real.flags = 0;
    assert(PTR_ERR(funnel_ioremap(&dev, &dummy)) == -EINVAL);
    reset(); real.end = real.start - 1;
    assert(PTR_ERR(funnel_ioremap(&dev, &dummy)) == -EINVAL);
    reset(); fail_mapping = true;
    assert(PTR_ERR(funnel_ioremap(&dev, &dummy)) == -ENOMEM);
    puts("PASS: ordinary/shared mappings, missing name, translation, range and mapping errors");
    return 0;
}
