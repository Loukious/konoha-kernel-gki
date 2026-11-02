#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

typedef uint32_t __be32;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t phys_addr_t;
#define unreachable() __builtin_unreachable()
#define __free(x)
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define OF_BAD_ADDR UINT64_MAX
#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT 2
#define OF_ROOT_NODE_SIZE_CELLS_DEFAULT 1
#define EXCLUDED_DEFAULT_CELLS_PLATFORMS 0
#define OF_MAX_ADDR_CELLS 4
#define OF_CHECK_COUNTS(na,ns) ((na)>0 && (na)<=4 && (ns)>0)
#define LOGIC_PIO_CPU_MMIO 1
#define GFP_KERNEL 0
#define IOMMU_RESV_RESERVED 1
#define U64_MAX UINT64_MAX
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define cpu_to_be32(x) __builtin_bswap32((uint32_t)(x))
#define no_free_ptr(x) (x)
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define of_dump_addr(...) ((void)0)
static unsigned int warnings;
#define WARN_ONCE(condition,...) do { static bool warned; if ((condition) && !warned) { ++warnings; warned=true; } } while (0)
struct device_node {
    struct device_node *parent;
    int address_cells, size_cells;
    __be32 pool[8]; unsigned int pool_len;
    int fwnode;
};
struct device { struct device_node *of_node; };
struct logic_pio_hwaddr { unsigned int flags; };
static struct logic_pio_hwaddr *find_io_range_by_fwnode(void *f) { (void)f; return NULL; }
static struct device_node *of_node_get(struct device_node *n) { return n; }
static void of_node_put(struct device_node *n) { (void)n; }
static struct device_node *of_get_parent(const struct device_node *n) { return n->parent; }
static struct device_node *__of_get_dma_parent(const struct device_node *n) { return of_get_parent(n); }
static bool of_property_present(struct device_node *n, const char *name)
{
    if (!strcmp(name,"#address-cells")) return n->address_cells>=0;
    if (!strcmp(name,"#size-cells")) return n->size_cells>=0;
    return false;
}
static int of_property_read_u32(struct device_node *n,const char *name,u32 *v)
{
    int x=!strcmp(name,"#address-cells")?n->address_cells:n->size_cells;
    if(x<0)return -EINVAL;
    *v=(u32)x;return 0;
}
static const void *of_get_property(const struct device_node *n,const char *name,void *len)
{
    if (!strcmp(name,"qcom,iommu-dma-addr-pool") && n->pool_len) {
        memcpy(len,&n->pool_len,sizeof(n->pool_len));return n->pool;
    }
    /* Python validates that every tested ancestor lacks dma-ranges. */
    assert(!strcmp(name,"dma-ranges") || !strcmp(name,"qcom,iommu-dma-addr-pool"));
    return NULL;
}
static struct device_node *of_parse_phandle(struct device_node *n,const char *name,int index)
{ (void)n;(void)name;(void)index;return NULL; }
static u64 of_read_number(const __be32 *p,int n)
{ u64 v=0;for(int i=0;i<n;i++)v=(v<<32)|__builtin_bswap32(p[i]);return v; }
struct of_bus {
    const char *name;
    int (*match)(struct device_node *);
    void (*count_cells)(struct device_node *,int *,int *);
    u64 (*map)(__be32 *,const __be32 *,int,int,int,int);
    int (*translate)(__be32 *,u64,int);
    int flag_cells;
};
#include "of-base.inc"
#include "of-default.inc"
static const struct of_bus of_busses[]={
    {.name="default",.match=of_bus_default_match,.count_cells=of_bus_default_count_cells,
     .map=of_bus_default_map,.translate=of_bus_default_translate,.flag_cells=0}
};
static int of_empty_ranges_quirk(const struct device_node *n) { (void)n;return 0; }
#include "of-translation.inc"

struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(name) struct list_head name={&(name),&(name)}
#define INIT_LIST_HEAD(n) do{(n)->next=(n);(n)->prev=(n);}while(0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_entry(p,t,m) container_of(p,t,m)
#define list_first_entry(h,t,m) list_entry((h)->next,t,m)
#define list_for_each_entry(p,h,m) for((p)=list_entry((h)->next,__typeof__(*(p)),m); &(p)->m!=(h); (p)=list_entry((p)->m.next,__typeof__(*(p)),m))
#define list_for_each_entry_safe(p,n,h,m) for((p)=list_entry((h)->next,__typeof__(*(p)),m),(n)=list_entry((p)->m.next,__typeof__(*(p)),m); &(p)->m!=(h); (p)=(n),(n)=list_entry((n)->m.next,__typeof__(*(n)),m))
static bool list_empty(const struct list_head *h){return h->next==h;}
static void add_between(struct list_head *n,struct list_head *prev,struct list_head *next)
{next->prev=n;n->next=next;n->prev=prev;prev->next=n;}
static void list_add(struct list_head *n,struct list_head *h){add_between(n,h,h->next);}
static void list_add_tail(struct list_head *n,struct list_head *h){add_between(n,h->prev,h);}
static void list_splice(struct list_head *n,struct list_head *h)
{if(!list_empty(n)){struct list_head *first=n->next,*last=n->prev,*next=h->next;first->prev=h;h->next=first;last->next=next;next->prev=last;}}
struct iommu_resv_region {u64 start,length;struct list_head list;};
static struct iommu_resv_region *iommu_alloc_resv_region(u64 start,u64 len,int prot,int type,int flags)
{(void)prot;(void)type;(void)flags;struct iommu_resv_region *r=calloc(1,sizeof(*r));assert(r);r->start=start;r->length=len;INIT_LIST_HEAD(&r->list);return r;}
static void iommu_put_resv_regions(struct device *dev,struct list_head *h)
{(void)dev;while(!list_empty(h)){struct list_head *p=h->next;h->next=p->next;p->next->prev=h;free(list_entry(p,struct iommu_resv_region,list));}}
#define kfree(p) free(p)
struct qcom_iommu_range_prop_cb_data {int (*range_prop_entry_cb_fn)(const __be32 *,int,int,void *);void *arg;};
#include "vendor-pool.inc"
#include "fixtures.inc"
int main(void)
{
    for(unsigned int i=0;i<ARRAY_SIZE(reservations);i++) {
        struct reservation_fixture *f=&reservations[i];
        phys_addr_t start=OF_BAD_ADDR;size_t len=SIZE_MAX;
        assert(of_bus_default_match(f->dev->parent));
        const __be32 *next=of_translate_dma_region(f->dev,f->cells,&start,&len);
        assert(next==f->cells+f->na+f->ns);
        assert(start==f->translated && len==f->length && len);
    }
    for(unsigned int i=0;i<ARRAY_SIZE(pool_fixtures);i++) {
        struct pool_fixture *f=&pool_fixtures[i];struct device dev={.of_node=f->dev};
        LIST_HEAD(pools);LIST_HEAD(reserved);struct iommu_resv_region *r;
        int rc=qcom_iommu_generate_dma_regions(&dev,&pools);
        assert(rc==0);
        assert(!list_empty(&pools));r=list_first_entry(&pools,struct iommu_resv_region,list);
        assert(r->start==f->start && r->length==f->length);
        assert(r->list.next==&pools);
        qcom_iommu_generate_resv_regions(&dev,&reserved);
        assert(!list_empty(&reserved));r=list_first_entry(&reserved,struct iommu_resv_region,list);
        assert(r->start==0 && r->length==f->start);
        r=list_entry(r->list.next,struct iommu_resv_region,list);
        assert(r->start==f->start+f->length && r->length==(u64)(0-r->start));
        assert(r->list.next==&reserved);
        iommu_put_resv_regions(&dev,&pools);iommu_put_resv_regions(&dev,&reserved);
    }
    assert(warnings==expected_warnings);
    printf("PASS: %zu exact DMA translations, %zu native IPA pools and complements, %u OF warning sites\n",ARRAY_SIZE(reservations),ARRAY_SIZE(pool_fixtures),warnings);
}
