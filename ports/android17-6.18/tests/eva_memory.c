#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef uint32_t u32;typedef uint64_t dma_addr_t;typedef unsigned fmode_t;
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,18,33)
#define U32_MAX UINT32_MAX
#define SZ_4K 4096U
#define ALIGN(x,a) (((x)+(a)-1)&~((typeof(x))(a)-1))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>(uintptr_t)-4096)
#define dprintk(...) ((void)0)
#define DMA_ATTR_SKIP_CPU_SYNC 1U
#define DMA_ATTR_QTI_SMMU_PROXY_MAP 2U
#define SMEM_CAMERA 64U
#define SMEM_NON_PIXEL 16U
#define SMEM_PIXEL 32U
#define VMID_CP_NON_PIXEL 8
#define VMID_CP_PIXEL 9
#define PERM_READ 1
#define PERM_WRITE 2
#define FMODE_PATH 4
struct file_operations {int unused;};struct file {unsigned f_mode;struct file_operations *f_op;int refs;};struct task_struct {int unused;};
struct device {int unused;};struct iommu_domain {int unused;};
struct dma_buf {size_t size;struct file *file;char *name;};
struct dma_heap {int unused;};
struct dma_buf_attachment {unsigned dma_map_attrs;};
struct scatterlist {dma_addr_t dma_address;size_t length;};
struct sg_table {struct scatterlist *sgl;unsigned nents;};
struct context_bank_info {struct device *dev;struct iommu_domain *domain;};
struct msm_cvp_platform_resources {bool iommu;};
struct cvp_dma_mapping_info {struct device *dev;struct iommu_domain *domain;struct sg_table *table;struct dma_buf_attachment *attach;struct dma_buf *buf;void *cb_info;};
struct msm_cvp_smem {struct dma_buf *dma_buf;u32 flags,size,device_addr;void *kvaddr;struct cvp_dma_mapping_info mapping_info;};
struct iosys_map {void *vaddr;bool is_iomem;};struct cvp_dma_buf_vmap {struct iosys_map map;void *vaddr;};
struct mem_buf_lend_kernel_arg {int nr_acl_entries;int *vmids,*perms;};
enum dma_data_direction {DMA_BIDIRECTIONAL,DMA_TO_DEVICE,DMA_FROM_DEVICE};enum smem_cache_ops {SMEM_CACHE_CLEAN,SMEM_CACHE_CLEAN_INVALIDATE,SMEM_CACHE_INVALIDATE};
static struct {const struct file_operations *dmabuf_f_op;} gfa_cv;
static int fail,heap_refs,live_buffers,attachments,mappings,cpu_maps,cpu_access,foreign_refs,lookup_fd;
static bool iomem;static struct device device;static struct context_bank_info cb={.dev=&device};static struct dma_heap heap;static struct dma_buf buffer;static struct file file;static struct dma_buf_attachment attach;static struct scatterlist sg;static struct sg_table table;static char storage[4096];
#define sg_dma_len(s) ((s)->length)
#define sg_dma_address(s) ((s)->dma_address)
static bool is_iommu_present(struct msm_cvp_platform_resources *r){return r->iommu;}
static struct context_bank_info *msm_cvp_smem_get_context_bank(void *r,unsigned f){return fail==1?NULL:&cb;}
static struct dma_buf_attachment *dma_buf_attach(struct dma_buf *b,struct device *d){if(fail==2)return ERR_PTR(-EIO);attachments++;return &attach;}
static struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *a,int dir){if(fail==3)return ERR_PTR(-EIO);mappings++;return &table;}
static void dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *a,struct sg_table *t,int dir){assert(mappings==1);mappings--;}
static void dma_buf_detach(struct dma_buf *b,struct dma_buf_attachment *a){assert(attachments==1&&!mappings);attachments--;}
static struct dma_heap *dma_heap_find(const char *name){if(fail==4)return NULL;heap_refs++;return &heap;}
static void dma_heap_put(struct dma_heap *h){assert(heap_refs==1);heap_refs--;}
static struct dma_buf *dma_heap_buffer_alloc(struct dma_heap *h,size_t size,int a,int b){assert(heap_refs==1);if(fail==5)return ERR_PTR(-ENOMEM);live_buffers++;buffer.size=size;buffer.file=&file;return &buffer;}
static void dma_heap_buffer_free(struct dma_buf *b){assert(live_buffers==1&&!attachments&&!mappings&&!cpu_maps&&!cpu_access);live_buffers--;}
static int mem_buf_lend(struct dma_buf *b,struct mem_buf_lend_kernel_arg *a){return fail==6?-EPERM:0;}
static int dma_buf_begin_cpu_access(struct dma_buf *b,int dir){if(fail==7)return -EIO;cpu_access++;return 0;}
static int dma_buf_end_cpu_access(struct dma_buf *b,int dir){assert(cpu_access==1);cpu_access--;return 0;}
static int dma_buf_vmap_unlocked(struct dma_buf *b,struct iosys_map *m){if(fail==8)return -ENOMEM;cpu_maps++;m->vaddr=storage;m->is_iomem=iomem;return 0;}
static void dma_buf_vunmap_unlocked(struct dma_buf *b,struct iosys_map *m){assert(cpu_maps==1&&m->vaddr==storage);cpu_maps--;}
static void iosys_map_set_vaddr(struct iosys_map *m,void *v){m->vaddr=v;m->is_iomem=false;}
static int dma_buf_begin_cpu_access_partial(struct dma_buf *b,int dir,unsigned long o,unsigned long sz){assert(o<=b->size&&sz<=b->size-o);return 0;}
static int dma_buf_end_cpu_access_partial(struct dma_buf *b,int dir,unsigned long o,unsigned long sz){assert(o<=b->size&&sz<=b->size-o);return 0;}
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
static struct file *fget_task_next(struct task_struct *t,unsigned *fd){if(lookup_fd<0)return NULL;*fd=lookup_fd;foreign_refs++;return &file;}
static void fput(struct file *f){assert(foreign_refs>0);foreign_refs--;}
#include "vmap.inc"
#include "memory.inc"
#include "file.inc"
static void reset(void){assert(!heap_refs&&!live_buffers&&!attachments&&!mappings&&!cpu_maps&&!cpu_access&&!foreign_refs);fail=0;iomem=false;sg=(struct scatterlist){.dma_address=4096,.length=4096};table=(struct sg_table){.sgl=&sg,.nents=1};buffer=(struct dma_buf){.size=4096,.file=&file};file.f_mode=0;}
int main(void){struct msm_cvp_platform_resources r={.iommu=true};struct msm_cvp_smem m;unsigned scenarios=0;
 for(int f=0;f<=8;f++){reset();fail=f;m=(struct msm_cvp_smem){.flags=SMEM_PIXEL};int rc=alloc_dma_mem(4096,4096,1,&r,&m);if(!f){assert(!rc);assert(!free_dma_mem(&m));}else assert(rc<0);reset();scenarios++;}
 for(int f=0;f<7;f++){reset();m=(struct msm_cvp_smem){0};if(f==0)sg.dma_address=0;if(f==1)table.nents=2;if(f==2)sg.length=4095;if(f==3)sg.dma_address=UINT32_MAX-100;if(f==4)sg.dma_address=1ULL<<32;if(f==5)table.sgl=NULL;if(f==6)iomem=true;int rc=alloc_dma_mem(4096,4096,1,&r,&m);if(!f){assert(!rc&&!m.device_addr);assert(!free_dma_mem(&m));}else assert(rc<0);reset();scenarios++;}
 m=(struct msm_cvp_smem){0};assert(alloc_dma_mem(SIZE_MAX,4096,0,&r,&m)==-EINVAL);r.iommu=false;assert(alloc_dma_mem(4096,4096,0,&r,&m)==-ENODEV);r.iommu=true;
 for(unsigned long o=4090;o<4100;o++)for(unsigned long sz=0;sz<10;sz++){int rc=msm_cvp_smem_cache_operations(&buffer,SMEM_CACHE_CLEAN,o,sz);assert((rc==0)==(o<=4096&&sz<=4096-o));scenarios++;}
 struct task_struct task;for(int fd=-1;fd<5;fd++){lookup_fd=fd;struct file *f=msm_cvp_fget(2,&task,FMODE_PATH,1);assert((f!=NULL)==(fd==2));if(f)fput(f);scenarios++;}lookup_fd=2;file.f_mode=FMODE_PATH;assert(!msm_cvp_fget(2,&task,FMODE_PATH,1));assert(!msm_cvp_fget(2,NULL,FMODE_PATH,1));reset();
 printf("{\"dma_mapping_allocation_and_file_scenarios\":%u}\n",scenarios+4);
}
