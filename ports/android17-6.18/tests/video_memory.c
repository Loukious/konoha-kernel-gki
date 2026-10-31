#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
#define SZ_4K 4096U
#define ALIGN(x,a) (((x)+(a)-1)&~((__typeof__(x))(a)-1))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,33)
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((intptr_t)(p))
#define MSM_VIDC_NON_SECURE 0
#define MSM_VIDC_NON_SECURE_PIXEL 1
#define MSM_VIDC_SECURE_PIXEL 2
#define MSM_VIDC_SECURE_NONPIXEL 3
#define MSM_VIDC_SECURE_BITSTREAM 4
#define MSM_VIDC_BUF_BIN 1
#define VMID_CP_BITSTREAM 1
#define PERM_READ 1
#define PERM_WRITE 2
#define DMA_BIDIRECTIONAL 0
#define DMA_ATTR_SKIP_CPU_SYNC 1
#define DMA_ATTR_DELAYED_UNMAP 2
#define d_vpr_h(...) ((void)0)
#define d_vpr_l(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define trace_msm_vidc_dma_buffer(...) ((void)0)
struct dma_buf { bool secure; void *storage; };
struct dma_heap { bool secure; };
struct device { int value; };
struct dma_buf_attachment { unsigned long dma_map_attrs; };
struct sg_table { uint64_t *sgl; };
struct iosys_map { void *vaddr; };
struct msm_vidc_mem { u32 size, region, type, refcount; bool secure, map_kernel; struct dma_buf *dmabuf; void *kvaddr; struct iosys_map dmabuf_map; uint64_t device_addr; struct dma_buf_attachment *attach; struct sg_table *table; };
struct context_bank_info { u32 region; struct device *dev; };
struct msm_vidc_core { struct context_bank_info cb; };
struct mem_buf_lend_kernel_arg { int nr_acl_entries, *vmids, *perms; };
static int fail, heaps, buffers, cpu, vmaps, attachments, maps;
static size_t allocated_size;
static struct dma_heap heap;
static struct sg_table table;
static uint64_t address=0x800000;
static struct dma_heap *dma_heap_find(const char *name) { if (fail==1) return NULL; heap.secure=strcmp(name,"qcom,system")!=0; heaps++; return &heap; }
static void dma_heap_put(struct dma_heap *h) { assert(h==&heap && heaps>0); heaps--; }
static struct dma_buf *dma_heap_buffer_alloc(struct dma_heap *h,size_t n,int fd,int flags) { allocated_size=n; if(fail==2)return ERR_PTR(-ENOMEM); assert(heaps==1 && n); struct dma_buf *b=calloc(1,sizeof(*b)); b->secure=h->secure; b->storage=calloc(1,64); buffers++; return b; }
static void dma_heap_buffer_free(struct dma_buf *b) { assert(b && buffers>0 && !cpu && !vmaps && !attachments && !maps); buffers--; free(b->storage); free(b); }
static int dma_buf_begin_cpu_access(struct dma_buf *b,int direction) { if(fail==4)return -EIO; cpu++; return 0; }
static int dma_buf_end_cpu_access(struct dma_buf *b,int direction) { assert(cpu>0 && !vmaps); cpu--; return 0; }
static int dma_buf_vmap_unlocked(struct dma_buf *b,struct iosys_map *m) { assert(cpu>0); if(fail==5)return -EIO; vmaps++; m->vaddr=b->storage; return 0; }
static void dma_buf_vunmap_unlocked(struct dma_buf *b,struct iosys_map *m) { assert(vmaps>0 && m->vaddr==b->storage); vmaps--; m->vaddr=NULL; }
static int mem_buf_lend(struct dma_buf *b,struct mem_buf_lend_kernel_arg *a) { assert(a->nr_acl_entries==1); if(fail==3)return -EIO; b->secure=true; return 0; }
static bool mem_buf_dma_buf_exclusive_owner(struct dma_buf *b) { return !b->secure; }
static struct context_bank_info *msm_vidc_get_context_bank_for_device(struct msm_vidc_core *c,struct device *d) { return &c->cb; }
static struct context_bank_info *msm_vidc_get_context_bank_for_region(struct msm_vidc_core *c,u32 r) { return fail==6 ? NULL : &c->cb; }
static struct dma_buf_attachment *dma_buf_attach(struct dma_buf *b,struct device *d) { if(fail==7)return ERR_PTR(-ENOMEM); attachments++; return calloc(1,sizeof(struct dma_buf_attachment)); }
static struct sg_table *dma_buf_map_attachment(struct msm_vidc_core *c,struct dma_buf_attachment *a) { if(fail==8)return ERR_PTR(-EIO); assert(a->dma_map_attrs==(DMA_ATTR_SKIP_CPU_SYNC|DMA_ATTR_DELAYED_UNMAP)); maps++; table.sgl=&address; return &table; }
static void dma_buf_unmap_attachment(struct msm_vidc_core *c,struct dma_buf_attachment *a,struct sg_table *t) { assert(maps>0 && t==&table); maps--; }
static void dma_buf_detach(struct msm_vidc_core *c,struct dma_buf *b,struct dma_buf_attachment *a) { assert(!maps && attachments>0); attachments--; free(a); }
static bool is_sys_cache_present(struct msm_vidc_core *c) { return true; }
#define sg_dma_address(p) (*(p))
#define call_mem_op(c,op,...) op(__VA_ARGS__)
/* SOURCE */
static void clean(struct msm_vidc_mem *m) { assert(!heaps && !buffers && !cpu && !vmaps && !attachments && !maps); assert(!m->dmabuf && !m->kvaddr && !m->refcount && !m->attach && !m->table); }
int main(void)
{
    int cases=0; struct device dev={0}; struct msm_vidc_core core={.cb.dev=&dev};
    for (int secure=0;secure<2;secure++) for (int mapping=0;mapping<2;mapping++) {
        core.cb.region=secure ? MSM_VIDC_SECURE_BITSTREAM : MSM_VIDC_NON_SECURE;
        for (fail=1;fail<=8;fail++) {
            if ((fail==3 && !secure) || ((fail==4 || fail==5) && !mapping)) continue;
            struct msm_vidc_mem m={.size=4096,.secure=secure,.map_kernel=mapping,.region=core.cb.region,.type=MSM_VIDC_BUF_BIN};
            assert(msm_vidc_memory_alloc_map_ext(&core,&m)<0); clean(&m); cases++;
        }
        fail=0;
        struct msm_vidc_mem m={.size=4097,.secure=secure,.map_kernel=mapping,.region=core.cb.region,.type=MSM_VIDC_BUF_BIN};
        assert(!msm_vidc_memory_alloc_map_ext(&core,&m)); assert(!heaps && buffers==1 && attachments==1 && maps==1 && m.refcount==1); assert(allocated_size==8192);
        assert(msm_vidc_memory_alloc_map_ext(&core,&m)==-EINVAL);
        assert(!msm_vidc_memory_unmap_free_ext(&core,&m)); clean(&m); cases++;
    }
    struct msm_vidc_mem m={0}; fail=0; assert(msm_vidc_memory_alloc_map_ext(&core,&m)==-EINVAL); clean(&m); cases++;
    m.size=UINT32_MAX; fail=2; assert(msm_vidc_memory_alloc_map_ext(&core,&m)==-ENOMEM); assert(allocated_size==(UINT64_C(1)<<32)); clean(&m); cases++;
    printf("%d DMA-BUF allocation, CPU-access, mapping and rollback cases passed\n",cases);
    return 0;
}
