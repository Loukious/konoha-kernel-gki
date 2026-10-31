#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint8_t u8;
#define SZ_4K 4096
#define SZ_1M (1024*1024)
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define SUPPORTS_SYNX_FENCE 0
#define MSM_VIDC_BUF_INTERFACE_QUEUE 1
#define d_vpr_h(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define strscpy(d,s,n) snprintf(d,n,"%s",s)
enum msm_vidc_device_region { MSM_VIDC_AON, MSM_VIDC_PROTOCOL_FENCE_CLIENT_VPU, MSM_VIDC_QTIMER };
enum msm_vidc_buffer_region { MSM_VIDC_NON_SECURE };
struct msm_vidc_mem { u32 type, region, size; bool secure, map_kernel; void *kvaddr; uint64_t device_addr, phys_addr; u32 refcount; };
/* CONSTANTS */
/* LAYOUT */
struct msm_vidc_core {
    struct msm_vidc_mem_addr iface_q_table, sfr, aon_reg, fence_reg, qtimer_reg, mmap_buf;
    struct msm_vidc_iface_q_info iface_queues[VIDC_IFACEQ_NUMQ];
    struct { int value; } capabilities[1];
    struct { struct msm_vidc_mem queue; } synx_fence_data;
    bool synx_queue_mapped;
};
struct device_region_info { uint64_t phy_addr; u32 size, dev_addr; };
static struct device_region_info regions[3]={{.phy_addr=0x1000,.size=4096,.dev_addr=0x1000},{.phy_addr=0x2000,.size=4096,.dev_addr=0x2000},{.phy_addr=0x3000,.size=4096,.dev_addr=0x3000}};
static int mask, fail, step, allocations, mappings, synx_maps;
static struct device_region_info *venus_hfi_get_device_region_info(struct msm_vidc_core *c,int region) { return mask&(1<<region) ? &regions[region] : NULL; }
static int memory_alloc_map(struct msm_vidc_core *c,struct msm_vidc_mem *m) { if (++step==fail) return -EIO; assert(m->size && !m->kvaddr); m->kvaddr=calloc(1,m->size); assert(m->kvaddr); m->device_addr=0x100000+step*0x10000; allocations++; return 0; }
static int memory_unmap_free(struct msm_vidc_core *c,struct msm_vidc_mem *m) { assert(m->kvaddr && allocations>0); free(m->kvaddr); m->kvaddr=NULL; m->device_addr=0; allocations--; return 0; }
static int iommu_map(struct msm_vidc_core *c,struct msm_vidc_mem *m) { if (++step==fail) return -EIO; assert(m->size && m->device_addr); mappings++; return 0; }
static int iommu_unmap(struct msm_vidc_core *c,struct msm_vidc_mem *m) { assert(m->size && mappings>0); mappings--; m->size=0; m->device_addr=0; m->phys_addr=0; return 0; }
static int mem_dma_map_page(struct msm_vidc_core *c,struct msm_vidc_mem *m) { if (++step==fail) return -EIO; m->refcount++; synx_maps++; return 0; }
static int mem_dma_unmap_page(struct msm_vidc_core *c,struct msm_vidc_mem *m) { assert(m->refcount>2 && synx_maps>0); m->refcount--; synx_maps--; return 0; }
#define call_mem_op(c,op,...) op(__VA_ARGS__)
/* SOURCE */
static void clean(struct msm_vidc_core *c)
{
    assert(!allocations && !mappings && !synx_maps && !c->synx_queue_mapped && c->synx_fence_data.queue.refcount==2);
    assert(!c->iface_q_table.align_virtual_addr && !c->sfr.align_virtual_addr && !c->mmap_buf.align_virtual_addr);
    for (int i=0;i<VIDC_IFACEQ_NUMQ;i++) assert(!c->iface_queues[i].q_hdr && !c->iface_queues[i].q_array.align_virtual_addr);
}
static void ready(struct msm_vidc_core *c)
{
    assert(allocations==3 && mappings==__builtin_popcount(mask));
    assert(synx_maps==c->capabilities[0].value && c->synx_fence_data.queue.refcount==2+synx_maps);
    struct hfi_queue_table_header *table=(void *)c->iface_q_table.align_virtual_addr;
    assert(table->qtbl_num_q==3 && table->device_addr==c);
    for (int i=0;i<VIDC_IFACEQ_NUMQ;i++) {
        struct hfi_queue_header *q=c->iface_queues[i].q_hdr;
        assert(q && q->qhdr_type==(VIDC_IFACEQ_DFLT_QHDR|i) && q->qhdr_read_idx==0 && q->qhdr_write_idx==0);
        assert(q->qhdr_rx_req==(i==VIDC_IFACEQ_DBGQ_IDX ? 0 : 1));
    }
}
int main(void)
{
    int cases=0;
    for (mask=0;mask<8;mask++) for (int synx=0;synx<2;synx++) {
        struct msm_vidc_core c={.capabilities[0].value=synx,.synx_fence_data.queue.refcount=2};
        int steps=3+__builtin_popcount(mask)+synx;
        for (int f=1;f<=steps;f++) {
            step=0; fail=f;
            assert(venus_hfi_queue_init(&c)==-EIO); clean(&c);
            step=0; fail=0;
            assert(!venus_hfi_queue_init(&c)); ready(&c);
            venus_hfi_queue_deinit(&c); clean(&c); venus_hfi_queue_deinit(&c); clean(&c); cases++;
        }
        fail=0; step=0;
        assert(!venus_hfi_queue_init(&c)); ready(&c);
        for (int i=0;i<VIDC_IFACEQ_NUMQ;i++) ((struct hfi_queue_header *)c.iface_queues[i].q_hdr)->qhdr_read_idx=12;
        int previous=step; assert(!venus_hfi_queue_init(&c) && step==previous); ready(&c);
        venus_hfi_queue_deinit(&c); clean(&c); cases++;
    }
    printf("%d queue rollback, retry, reset and borrowed-reference cases passed\n",cases);
    return 0;
}
