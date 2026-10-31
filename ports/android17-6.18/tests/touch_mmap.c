#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL<<PAGE_SHIFT)
#define VM_EXEC 1
#define VM_MAYEXEC 2
#define VM_DONTEXPAND 4
#define VM_DONTDUMP 8
typedef uintptr_t dma_addr_t;
typedef struct { int touch_id,mmap_area; } private_data_t;
typedef struct { unsigned long frame_data_size,raw_data_size;unsigned frame_data_buf_size,raw_data_buf_size;dma_addr_t frame_data_mmap_phy_base,raw_data_mmap_phy_base; } xiaomi_touch_data_t;
struct file { void *private_data; };
struct vm_area_struct { unsigned long vm_start,vm_end,vm_pgoff,vm_flags,vm_page_prot; };
static struct { dma_addr_t input_event_time_line_phy_base; } state={0x400000};
static typeof(state) *xiaomi_touch=&state;
static xiaomi_touch_data_t panel={.frame_data_size=PAGE_SIZE*8,.raw_data_size=PAGE_SIZE*2,.frame_data_buf_size=5,.raw_data_buf_size=5,.frame_data_mmap_phy_base=0x100000,.raw_data_mmap_phy_base=0x200000};
static xiaomi_touch_data_t *get_xiaomi_touch_data(int id){return id==0?&panel:NULL;}
static dma_addr_t get_report_point_info_phy_addr(void){return 0x500000;}
#define vm_flags_clear(v,b) ((v)->vm_flags&=~(b))
#define vm_flags_set(v,b) ((v)->vm_flags|=(b))
static unsigned long mapped_pfn,mapped_length,mapped_prot;static int mappings;
static int remap_pfn_range(struct vm_area_struct *v,unsigned long start,unsigned long pfn,unsigned long length,unsigned long prot){assert(start==v->vm_start);mappings++;mapped_pfn=pfn;mapped_length=length;mapped_prot=prot;return 0;}
/* FUNCTIONS */
int main(void){unsigned cases=0;private_data_t p={.touch_id=0};struct file f={&p};
for(int area=0;area<=5;area++){p.mmap_area=area;unsigned long pages=area==1?40:area==2?10:area==3?45:area==4?1:0;for(unsigned long offset=0;offset<=pages+2;offset++){for(unsigned long n=1;n<=pages+2;n++){struct vm_area_struct v={.vm_start=PAGE_SIZE,.vm_end=PAGE_SIZE+n*PAGE_SIZE,.vm_pgoff=offset,.vm_flags=VM_MAYEXEC,.vm_page_prot=0x21};int old=mappings,r=xiaomi_touch_dev_mmap_locked(&f,&v);bool valid=pages&&offset<pages&&n<=pages-offset;assert((r==0)==valid);assert(mappings==old+valid);if(valid){unsigned long base=area==1?0x100000:area==2?0x200000:area==3?0x400000:0x500000;assert(mapped_pfn==(base>>PAGE_SHIFT)+offset);assert(mapped_length==n*PAGE_SIZE&&mapped_prot==0x21);assert(!(v.vm_flags&VM_MAYEXEC));}cases++;}}
struct vm_area_struct v={.vm_start=PAGE_SIZE,.vm_end=2*PAGE_SIZE,.vm_pgoff=ULONG_MAX};assert(xiaomi_touch_dev_mmap_locked(&f,&v)<0);cases++;
v.vm_pgoff=ULONG_MAX>>PAGE_SHIFT;assert(xiaomi_touch_dev_mmap_locked(&f,&v)<0);cases++;
v.vm_pgoff=0;v.vm_flags=VM_EXEC;assert(xiaomi_touch_dev_mmap_locked(&f,&v)==-EPERM);cases++;
p.touch_id=-1;v.vm_flags=0;assert((xiaomi_touch_dev_mmap_locked(&f,&v)==0)==(area==3));p.touch_id=0;cases++;}
printf("{\"mapping_boundary_cases\":%u,\"overflow_offsets_rejected\":true,\"executable_mapping_rejected\":true,\"caller_protection_preserved\":true}\n",cases);}
