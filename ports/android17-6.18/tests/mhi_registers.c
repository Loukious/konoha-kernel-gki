#include "mock.h"
#define MHI_REG_SIZE 4096
#define MISC_OFFSET 0x24
#define MISC_CAP_MASK U32_MAX
#define CAP_CAPID_MASK 0xff000000
#define CAP_NEXT_CAP_MASK 0x00fff000
struct device{int x;};struct mhi_ring_element{u64 ptr;u32 dword[2];};struct mhi_controller{void *regs;size_t reg_len;u32 timeout_ms;struct device *cntrl_dev;int (*read_reg)(struct mhi_controller *,void *,u32 *);void (*write_reg)(struct mhi_controller *,void *,u32);};struct mhi_ring{u64 alloc_size,iommu_base,len;void *pre_aligned,*base;dma_addr_t dma_handle;};
static unsigned accesses;static dma_addr_t next_dma;
static int rd(struct mhi_controller *c,void *p,u32 *v){memcpy(v,p,4);accesses++;return 0;}
static void wr(struct mhi_controller *c,void *p,u32 v){memcpy(p,&v,4);accesses++;}
static u64 div_u64(u64 n,u32 d){return n/d;}
static void fsleep(u32 n){}
static void *dma_alloc_coherent(struct device *d,size_t n,dma_addr_t *h,int f){*h=next_dma;return alloc_mem(n);}
static void dma_free_coherent(struct device *d,size_t n,void *p,dma_addr_t h){free_mem(p);}
/* FUNCTIONS */
int main(void){u32 regs[1024]={123};struct mhi_controller c={regs,sizeof(regs),1,NULL,rd,wr};unsigned cases=0;for(size_t span=0;span<=64;span++)for(u32 off=0;off<=68;off++){c.reg_len=span;u32 v=0;unsigned old=accesses;bool valid=!(off%4)&&off+4<=span;int r=mhi_read_reg(&c,regs,off,&v);assert((!r)==valid);mhi_write_reg(&c,regs,off,42);assert(accesses==old+(valid?2:0));cases++;}c.reg_len=sizeof(regs);assert(!mhi_reg_range_valid(&c,(void *)((uintptr_t)regs-4),0,4));assert(!mhi_reg_range_valid(&c,regs,U32_MAX,4));assert(!mhi_reg_range_valid(&c,(u8 *)regs+4092,0,8));assert(mhi_reg_range_valid(&c,(u8 *)regs+4092,0,4));u32 v;assert(mhi_read_reg_field(&c,regs,0,0,&v)==-EINVAL);assert(mhi_write_reg_field(&c,regs,0,0,1)==-EINVAL);assert(mhi_poll_reg_field(&c,regs,0,1,1,0)==-EINVAL);regs[0]=0x80000000;assert(!mhi_write_reg_field(&c,regs,0,0xf0,0xff));assert(regs[0]==0x800000f0);assert(!mhi_read_reg_field(&c,regs,0,0xf0,&v)&&v==15);
for(u64 b=0;b<32;b++)for(u64 l=0;l<64;l++)for(u64 a=0;a<100;a++){struct mhi_ring ring={.iommu_base=b,.len=l};bool valid=a>=b&&a-b<l&&!((a-b)%16);assert(is_valid_ring_ptr(&ring,a)==valid);cases++;}
struct mhi_ring top={.iommu_base=U64_MAX-31,.len=64};assert(!is_valid_ring_ptr(&top,0));assert(is_valid_ring_ptr(&top,U64_MAX-15));
for(u64 len=1;len<258;len++)for(next_dma=1;next_dma<20;next_dma++){struct mhi_ring ring={0};assert(!mhi_alloc_aligned_ring(&c,&ring,len));assert(!(ring.iommu_base%len)&&ring.iommu_base>=next_dma);assert((u8 *)ring.base>=(u8 *)ring.pre_aligned&&(u8 *)ring.base+len<=(u8 *)ring.pre_aligned+ring.alloc_size);free_mem(ring.pre_aligned);cases++;}
struct mhi_ring ring={0};assert(mhi_alloc_aligned_ring(&c,&ring,0)==-EINVAL);assert(mhi_alloc_aligned_ring(&c,&ring,U64_MAX)==-EINVAL);next_dma=U64_MAX;assert(mhi_alloc_aligned_ring(&c,&ring,16)==-EOVERFLOW);assert(!allocations);
regs[MISC_OFFSET/4]=100;regs[25]=(1U<<24)|(100U<<12);u32 offset;unsigned old=accesses;assert(mhi_get_capability_offset(&c,2,&offset)==-ENXIO);assert(accesses-old<=2049);regs[25]=(2U<<24);assert(!mhi_get_capability_offset(&c,2,&offset)&&offset==100);regs[MISC_OFFSET/4]=U32_MAX;assert(mhi_get_capability_offset(&c,2,&offset)==-ENXIO);regs[MISC_OFFSET/4]=101;assert(mhi_get_capability_offset(&c,2,&offset)==-ENXIO);regs[MISC_OFFSET/4]=0;assert(mhi_get_capability_offset(&c,2,&offset)==-ENXIO);
printf("{\"register_ring_and_alignment_cases\":%u,\"overflow_and_zero_masks_rejected\":true}\n",cases);}
