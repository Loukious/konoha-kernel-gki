#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <elf.h>
#define MAX_OWNER_STRING 32
#define MAX_NAME_LENGTH 12
#define VA_MD_VADDR_MARKER -1
#define VA_MD_CB_MARKER -2
#define ELF_CLASS ELFCLASS64
#define ELF_DATA ELFDATA2LSB
#define ELF_OSABI ELFOSABI_NONE
#define ELF_ARCH EM_AARCH64
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define check_add_overflow(a,b,p) __builtin_add_overflow((a),(b),(p))
#define pr_err(...) ((void)0)
#define raw_smp_processor_id() simulated_cpu
#define raw_spin_lock_irqsave(p,f) ((void)(p),assert(!locked),locked=true,(f)=0)
#define raw_spin_unlock_irqrestore(p,f) ((void)(p),(void)(f),assert(locked),locked=false)
typedef uint64_t phys_addr_t;
typedef uint64_t dma_addr_t;
typedef uint32_t u32;
typedef uint64_t u64;
struct list_head {struct list_head *next,*prev;};
struct kobject {int refs;};
struct atomic_notifier_head {int unused;};
struct notifier_block {int (*notifier_call)(struct notifier_block*,unsigned long,void*);int priority;};
/* STRUCTURES */
static int simulated_cpu,va_md_region_lock;
static bool locked,nofault_failure;
static struct md_region advertised;
static int registration_error;
static int msm_minidump_add_region(const struct md_region *r) {advertised=*r;return registration_error;}
static long copy_from_kernel_nofault(void *dst,const void *src,size_t size) {if(nofault_failure){memset(dst,0xab,size/2);return -EFAULT;}memcpy(dst,src,size);return 0;}
static long strscpy(char *dst,const char *src,size_t n) {size_t len=strlen(src);if(len>=n){memcpy(dst,src,n-1);dst[n-1]=0;return -E2BIG;}memcpy(dst,src,len+1);return len;}
/* FUNCTIONS */
static unsigned char source_bytes[6][128];
static void fill(void *dst,unsigned long n) {memset(dst,0x6d,n);}
static void setup(size_t capacity) {
 memset(&va_md_data,0,sizeof(va_md_data));
 unsigned char *mem=calloc(1,capacity+32);assert(mem);memset(mem+capacity,0x5a,32);
 va_md_data.elf_mem=(unsigned long)mem;va_md_data.total_mem_size=capacity;va_md_data.mem_phys_addr=0x12300000;va_md_data.collecting=true;va_md_data.capture_cpu=0;simulated_cpu=0;
}
static void cleanup(void) {unsigned char *mem=(void*)va_md_data.elf_mem;for(int i=0;i<32;i++)assert(mem[va_md_data.total_mem_size+i]==0x5a);free(mem);}
static void check_elf(unsigned expected,unsigned data_size,bool fault) {
 Elf64_Ehdr *hdr=(void*)va_md_data.elf.ehdr;
 assert(!memcmp(hdr->e_ident,ELFMAG,SELFMAG)&&hdr->e_machine==EM_AARCH64&&hdr->e_type==ET_CORE);
 assert(hdr->e_shnum==expected+2&&hdr->e_phnum==expected&&hdr->e_shstrndx==1);
 Elf64_Shdr *sh=(void*)((char*)hdr+hdr->e_shoff);Elf64_Phdr *ph=(void*)((char*)hdr+hdr->e_phoff);
 assert(sh[0].sh_type==SHT_NULL&&sh[1].sh_type==SHT_STRTAB);
 const char *names=(char*)hdr+sh[1].sh_offset;assert(!strcmp(names+sh[1].sh_name,"STR_TBL"));
 for(unsigned i=0;i<expected;i++) {
  assert(ph[i].p_offset==sh[i+2].sh_offset&&ph[i].p_filesz==data_size&&ph[i].p_type==PT_LOAD);
  assert(ph[i].p_offset+data_size<=va_md_data.elf_size);
  assert(!strcmp(names+sh[i+2].sh_name,i%2?"callback":"direct"));
  unsigned char *bytes=(void*)((char*)hdr+ph[i].p_offset);
  for(unsigned j=0;j<data_size;j++)assert(bytes[j]==(i%2?0x6d:fault?0:source_bytes[i][j]));
 }
 qcom_va_md_minidump_registration();assert(va_md_data.va_md_minidump_reg);
 assert(advertised.size==va_md_data.elf_size&&advertised.virt_addr==(unsigned long)hdr&&advertised.phys_addr==0x12300000+(unsigned long)hdr-va_md_data.elf_mem);
}
int main(void) {
 unsigned long overlap_cases=0,geometry_cases=0;
 struct va_md_entry e={.vaddr=1,.size=1};strcpy((char*)e.owner,"name");
 setup(4096);va_md_data.collecting=false;assert(qcom_va_md_add_region(&e)==-EINVAL);va_md_data.collecting=true;
 assert(qcom_va_md_add_region(NULL)==-EINVAL);e.cb=fill;assert(qcom_va_md_add_region(&e)==-EINVAL);e.vaddr=0;e.cb=NULL;assert(qcom_va_md_add_region(&e)==-EINVAL);e.vaddr=1;e.size=0;assert(qcom_va_md_add_region(&e)==-EINVAL);e.vaddr=ULONG_MAX;e.size=2;assert(qcom_va_md_add_region(&e)==-EINVAL);e.size=1;simulated_cpu=1;assert(qcom_va_md_add_region(&e)==-EINVAL);simulated_cpu=0;
 va_md_data.num_sections=SHN_LORESERVE-3;assert(qcom_va_md_add_region(&e)==-ENOSPC);va_md_data.num_sections=0;cleanup();
 for(unsigned a=0;a<16;a++)for(unsigned b=0;b<16;b++)for(unsigned la=1;la<=8;la++)for(unsigned lb=1;lb<=8;lb++) {
  setup(4096);struct va_md_entry x={.vaddr=0x1000+a,.size=la},y={.vaddr=0x1000+b,.size=lb};
  memset(x.owner,'x',sizeof(x.owner));memset(y.owner,'y',sizeof(y.owner));struct va_md_entry original=x;
  assert(!qcom_va_md_add_region(&x));assert(!memcmp(&x,&original,sizeof(x)));
  bool overlap=a<b+lb&&b<a+la;assert(qcom_va_md_add_region(&y)==(overlap?-EINVAL:0));assert(va_md_data.num_sections==(overlap?1:2));cleanup();overlap_cases++;
 }
 for(int i=0;i<6;i++)for(int j=0;j<128;j++)source_bytes[i][j]=(unsigned char)(i*17+j);
 for(unsigned count=1;count<=6;count++)for(unsigned size=1;size<=64;size++)for(unsigned extra=0;extra<64;extra++) {
  size_t nodes=count*sizeof(struct va_md_tree_node),capacity=nodes+180+extra*8;
  setup(capacity);
  for(unsigned i=0;i<count;i++){struct va_md_entry x={.size=size};if(i%2){x.cb=fill;strcpy((char*)x.owner,"callback");}else{x.vaddr=(unsigned long)source_bytes[i];strcpy((char*)x.owner,"direct");}assert(!qcom_va_md_add_region(&x));}
  size_t total=nodes+sizeof(Elf64_Ehdr)+2*sizeof(Elf64_Shdr)+sizeof("STR_TBL")+1;unsigned expected=0;
  for(unsigned i=0;i<count;i++){size_t next=total+sizeof(Elf64_Shdr)+sizeof(Elf64_Phdr)+size+strlen(i%2?"callback":"direct")+1;if(ALIGN(next,4)>capacity)break;total=next;expected++;}
  int ret=qcom_va_md_calc_elf_size();assert(ret==(expected?0:-ENOSPC));
  if(expected){assert(va_md_data.elf.phdr_cnt==expected&&va_md_data.elf.shdr_cnt==expected+2&&va_md_data.elf_size==ALIGN(total,4)-nodes);memset((void*)va_md_data.elf.ehdr,0,va_md_data.elf_size);qcom_va_add_elf_hdr();qcom_va_add_hdrs();check_elf(expected,size,false);}
  cleanup();geometry_cases++;
 }
 setup(4096);e=(struct va_md_entry){.vaddr=(unsigned long)source_bytes[0],.size=31};strcpy((char*)e.owner,"direct");assert(!qcom_va_md_add_region(&e));assert(!qcom_va_md_calc_elf_size());memset((void*)va_md_data.elf.ehdr,0,va_md_data.elf_size);nofault_failure=true;qcom_va_add_elf_hdr();qcom_va_add_hdrs();check_elf(1,31,true);cleanup();nofault_failure=false;
 setup(512);e=(struct va_md_entry){.cb=fill,.size=UINT_MAX};memset(e.owner,'x',sizeof(e.owner));assert(!qcom_va_md_add_region(&e));assert(qcom_va_md_calc_elf_size()==-ENOSPC);registration_error=-ENOSPC;qcom_va_md_minidump_registration();assert(!va_md_data.va_md_minidump_reg);cleanup();
 printf("{\"range_overlap_cases\":%lu,\"decoded_elf_geometry_cases\":%lu,\"null_and_overflow_rejected\":true,\"faulting_source_zeroed\":true,\"input_owner_unchanged\":true}\n",overlap_cases,geometry_cases);
}
