#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef uint32_t u32;
typedef uint64_t u64, dma_addr_t, resource_size_t, phys_addr_t;
#define PAGE_SIZE 4096ULL
#define PAGE_SHIFT 12
#define SZ_4K 4096ULL
#define SZ_8K 8192ULL
#define SZ_4M 4194304ULL
#define PAGE_ALIGNED(x) (!((x) & (PAGE_SIZE-1)))
#define DMA_BIT_MASK(x) ((1ULL<<(x))-1)
#define BIT(x) (1ULL<<(x))
#define MPSS_RMTS_CLIENT_ID 1
#define VMID_HLOS 3
#define VMID_MSS_MSA 15
#define VMID_NAV 26
#define PERM_READ 1
#define PERM_WRITE 2
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define IORESOURCE_MEM 0
#define UIO_MEM_PHYS 1
#define check_add_overflow(a,b,c) __builtin_add_overflow(a,b,c)
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_err_probe(dev,ret,...) (ret)
#define MAX_UIO_MAPS 5
struct device_node {bool guarded,nav;int client_error;u32 client;};
struct device {struct device_node *of_node;};
struct resource {resource_size_t start,end;const char *name;};
struct platform_device {struct device dev;struct resource *resource;void *data;};
struct vm_area_struct {unsigned long vm_start,vm_end,vm_pgoff,vm_page_prot;};
struct uio_mem {phys_addr_t addr;resource_size_t size;int memtype;};
struct uio_info {void *priv;struct uio_mem mem[MAX_UIO_MAPS];const char *name,*version;int (*mmap)(struct uio_info*,struct vm_area_struct*);};
struct qcom_scm_vmperm {int vmid,perm;};
struct scatterlist {unsigned int length;phys_addr_t phys;};
struct sg_table {unsigned int orig_nents;struct scatterlist *sgl;};

static int mask_error, allocation_error, table_error, fragmented, assignment_error, reclaim_error, register_error;
static bool scm_available=true;
static int allocations, frees, assign_calls, published, dma_maps, phys_maps;
static dma_addr_t handle=0x0ffffd9000ULL;
static phys_addr_t physical=0xf1900000ULL, last_assign;
static size_t last_assign_size, last_free_size, last_dma_map_size;
static dma_addr_t last_free_handle, last_dma_map_handle;
static unsigned long last_dma_map_offset;
static void *last_allocation, *last_freed, *last_dma_map_cpu;
static int sequence[32], nseq;
static void trace(int stage){assert(nseq<32);sequence[nseq++]=stage;}
static int dma_set_mask_and_coherent(struct device *dev,u64 mask){(void)dev;assert(mask==UINT32_MAX);return mask_error;}
static void *dma_alloc_coherent(struct device *dev,size_t n,dma_addr_t *dma,int flags){(void)dev;(void)flags;if(allocation_error)return NULL;allocations++;*dma=handle;last_allocation=malloc(n);assert(last_allocation);trace(1);return last_allocation;}
static int dma_get_sgtable(struct device *dev,struct sg_table *t,void *cpu,dma_addr_t dma,size_t n){(void)dev;assert(cpu==last_allocation&&dma==handle);if(table_error)return table_error;t->orig_nents=fragmented?2:1;t->sgl=malloc(sizeof(*t->sgl));assert(t->sgl);t->sgl->length=n;t->sgl->phys=physical;return 0;}
static void sg_free_table(struct sg_table *t){free(t->sgl);}
static __attribute__((unused)) phys_addr_t sg_phys(struct scatterlist *sg){return sg->phys;}
static void dma_free_coherent(struct device *dev,size_t n,void *cpu,dma_addr_t dma){(void)dev;assert(cpu==last_allocation);assert(last_freed!=cpu);last_free_handle=dma;last_free_size=n;last_freed=cpu;frees++;trace(5);free(cpu);}
static bool qcom_scm_is_available(void){return scm_available;}
static int qcom_scm_assign_mem(phys_addr_t addr,size_t n,u64 *perms,const struct qcom_scm_vmperm *to,unsigned int count){
 assert(addr==physical||addr==physical+SZ_4K);assert(n==SZ_4M);last_assign=addr;last_assign_size=n;assign_calls++;
 if(count==1){assert(to[0].vmid==VMID_HLOS);trace(4);if(reclaim_error)return reclaim_error;*perms=BIT(VMID_HLOS);return 0;}
 assert(count==2||count==3);assert(*perms==BIT(VMID_HLOS));assert(to[0].vmid==VMID_HLOS&&to[1].vmid==VMID_MSS_MSA);if(count==3)assert(to[2].vmid==VMID_NAV);trace(2);if(assignment_error)return assignment_error;*perms=BIT(VMID_HLOS)|BIT(VMID_MSS_MSA);if(count==3)*perms|=BIT(VMID_NAV);return 0;
}
static int dma_mmap_coherent(struct device *dev,struct vm_area_struct *vma,void *cpu,dma_addr_t dma,size_t size){(void)dev;dma_maps++;last_dma_map_cpu=cpu;last_dma_map_handle=dma;last_dma_map_size=size;last_dma_map_offset=vma->vm_pgoff;assert((vma->vm_end-vma->vm_start)+PAGE_SIZE*vma->vm_pgoff<=size);return 0;}
static unsigned long pgprot_writecombine(unsigned long prot){return prot;}
static int remap_pfn_range(struct vm_area_struct *vma,unsigned long start,unsigned long pfn,unsigned long size,unsigned long prot){(void)prot;assert(start==vma->vm_start);assert(pfn==(physical>>PAGE_SHIFT));assert(size==vma->vm_end-vma->vm_start);phys_maps++;return 0;}
static struct resource *platform_get_resource(struct platform_device *p,int type,int index){assert(type==IORESOURCE_MEM&&index==0);return p->resource;}
static resource_size_t resource_size(struct resource *r){return r->end-r->start+1;}
static int of_property_read_u32(struct device_node *n,const char *key,u32 *out){assert(!strcmp(key,"qcom,client-id"));*out=n->client;return n->client_error;}
static bool of_property_read_bool(struct device_node *n,const char *key){if(!strcmp(key,"qcom,guard-memory"))return n->guarded;assert(!strcmp(key,"qcom,vm-nav-path"));return n->nav;}
static void *managed;
static void *devm_kzalloc(struct device *dev,size_t size,int flags){(void)dev;(void)flags;managed=calloc(1,size);return managed;}
static int uio_register_device(struct device *dev,struct uio_info *info){(void)dev;assert(!strcmp(info->name,"rmtfs")&&info->mem[0].size==SZ_4M&&info->mem[0].memtype==UIO_MEM_PHYS);trace(3);if(register_error)return register_error;published++;return 0;}
static void uio_unregister_device(struct uio_info *info){(void)info;assert(published);published--;trace(6);}
static void platform_set_drvdata(struct platform_device *p,void *data){p->data=data;}
static void *platform_get_drvdata(struct platform_device *p){return p->data;}
#include "helper.inc"
static void reset(void){assert(published==0);free(managed);managed=NULL;mask_error=allocation_error=table_error=fragmented=assignment_error=reclaim_error=register_error=0;allocations=frees=assign_calls=dma_maps=phys_maps=0;nseq=0;last_freed=NULL;scm_available=true;}
static int checks;
int main(void){
 struct device_node node={.client=MPSS_RMTS_CLIENT_ID};struct resource resource={.start=0,.end=SZ_4M-1,.name="rmtfs"};struct platform_device p={.dev={.of_node=&node},.resource=&resource};
 for(int guard=0;guard<2;guard++)for(int nav=0;nav<2;nav++){
  reset();node.guarded=guard;node.nav=nav;assert(!msm_sharedmem_probe(&p));struct msm_sharedmem *s=p.data;
  assert(s->info.mem[0].addr==physical+(guard?SZ_4K:0));assert(last_assign==s->info.mem[0].addr);assert(allocations==1&&frees==0);assert(sequence[0]==1&&sequence[1]==2&&sequence[2]==3);
  struct vm_area_struct v={.vm_start=0x1000000,.vm_end=0x1000000+SZ_4M};assert(!sharedmem_mmap(&s->info,&v));assert(v.vm_pgoff==0);assert(last_dma_map_offset==(unsigned long)guard);assert(last_dma_map_handle==handle&&last_dma_map_cpu==last_allocation&&last_dma_map_size==SZ_4M+(guard?SZ_8K:0));
  for(unsigned long bad=1;bad<=MAX_UIO_MAPS+1;bad++){v.vm_pgoff=bad;assert(sharedmem_mmap(&s->info,&v)==-EINVAL);checks++;}
  v.vm_pgoff=0;v.vm_end+=PAGE_SIZE;assert(sharedmem_mmap(&s->info,&v)==-EINVAL);checks++;
  msm_sharedmem_remove(&p);assert(assign_calls==2&&frees==1&&!s->allocation&&!s->assigned);assert(sequence[3]==6&&sequence[4]==4&&sequence[5]==5);assert(last_free_handle==handle&&last_free_size==SZ_4M+(guard?SZ_8K:0));checks++;
 }
 reset();node.guarded=false;node.nav=false;resource.start=physical;resource.end=physical+SZ_4M-1;assert(!msm_sharedmem_probe(&p));struct msm_sharedmem *s=p.data;struct vm_area_struct v={.vm_start=0x1000000,.vm_end=0x1000000+SZ_4M};assert(!sharedmem_mmap(&s->info,&v));assert(allocations==0&&dma_maps==0&&phys_maps==1);msm_sharedmem_remove(&p);assert(frees==0);checks++;
 resource.start=0;resource.end=SZ_4M-1;
 for(int fault=0;fault<7;fault++){
  reset();switch(fault){case 0:mask_error=-EIO;break;case 1:allocation_error=1;break;case 2:table_error=-EIO;break;case 3:fragmented=1;break;case 4:assignment_error=-EIO;break;case 5:register_error=-EIO;break;case 6:scm_available=false;break;}
  assert(msm_sharedmem_probe(&p)<0);assert(!published&&frees==allocations);if(fault==5)assert(assign_calls==2);if(fault==6)assert(!allocations);checks++;
 }
 reset();assert(!msm_sharedmem_probe(&p));s=p.data;reclaim_error=-EIO;msm_sharedmem_remove(&p);assert(s->allocation&&s->assigned&&!frees);reclaim_error=0;sharedmem_free(s);assert(!s->allocation&&frees==1);checks++;
 reset();struct msm_sharedmem fake={.dev=&p.dev};assert(sharedmem_allocate(&fake,0,false)==-EINVAL);assert(sharedmem_allocate(&fake,SZ_4M-1,false)==-EINVAL);assert(sharedmem_allocate(&fake,(resource_size_t)-4096,true)==-EOVERFLOW);assert(!allocations);checks+=3;
 reset();node.client_error=-EINVAL;assert(msm_sharedmem_probe(&p)==-EINVAL);node.client_error=0;resource.end=0;assert(msm_sharedmem_probe(&p)==-EINVAL);resource.end=SZ_4M-1;checks+=2;
 reset();free(managed);printf("PASS: %d lifecycle, mapping, physical-address and failure-path cases\n",checks);return 0;
}
