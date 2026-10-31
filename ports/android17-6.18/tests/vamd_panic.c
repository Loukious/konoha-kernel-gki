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
#define NOTIFY_DONE 0
#define smp_load_acquire(p) (*(p))
#define list_for_each_entry_rcu(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define raw_smp_processor_id() simulated_cpu
#define raw_spin_lock_irqsave(p,f) ((void)(p),assert(!locked),locked=true,(f)=0)
#define raw_spin_unlock_irqrestore(p,f) ((void)(p),(void)(f),assert(locked),locked=false)
typedef uint64_t phys_addr_t;
typedef uint64_t dma_addr_t;
typedef uint32_t u32;
typedef uint64_t u64;
struct list_head {struct list_head *next,*prev;};
struct kobject {int refs;};
struct notifier_block;
struct atomic_notifier_head {struct notifier_block *head;};
struct notifier_block {int (*notifier_call)(struct notifier_block*,unsigned long,void*);int priority;};
/* STRUCTURES */
static int simulated_cpu,va_md_region_lock;
static bool locked,nofault_failure;
static struct md_region advertised;
static int registration_error;
static int msm_minidump_add_region(const struct md_region *r) {advertised=*r;return registration_error;}
static long copy_from_kernel_nofault(void *dst,const void *src,size_t size) {if(nofault_failure){memset(dst,0xab,size/2);return -EFAULT;}memcpy(dst,src,size);return 0;}
static long strscpy(char *dst,const char *src,size_t n) {size_t len=strlen(src);if(len>=n){memcpy(dst,src,n-1);dst[n-1]=0;return -E2BIG;}memcpy(dst,src,len+1);return len;}
static unsigned rcu_readers,callback_calls;
static bool base_enabled=true;
static void rcu_read_lock(void) {rcu_readers++;}
static void rcu_read_unlock(void) {assert(rcu_readers);rcu_readers--;}
static bool msm_minidump_enabled(void) {return base_enabled;}
static int atomic_notifier_call_chain(struct atomic_notifier_head *h,unsigned long e,void *ptr) {assert(rcu_readers>=2);return h->head?h->head->notifier_call(h->head,e,ptr):0;}
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

static struct va_md_s_data client;
static struct notifier_block nb;
static int notify(struct notifier_block *n,unsigned long e,void *ptr) {
 (void)n;(void)e;(void)ptr;callback_calls++;
 assert(va_md_data.in_oops_handler&&va_md_data.collecting);
 assert(qcom_va_md_panic_handler(NULL,0,NULL)==NOTIFY_DONE);
 struct va_md_entry region={.vaddr=(unsigned long)source_bytes[0],.size=7};strcpy((char*)region.owner,"direct");assert(!qcom_va_md_add_region(&region));
 return 0;
}
static void initialize(bool enabled) {
 setup(4096);va_md_data.va_md_init=true;va_md_data.collecting=false;
 struct list_head *head=&va_md_data.va_md_list;head->next=head->prev=&client.va_md_s_list;client.va_md_s_list.next=client.va_md_s_list.prev=head;client.enable=enabled;nb.notifier_call=notify;client.va_md_s_notif_list.head=&nb;
}
static void run_pair(void) {rcu_read_lock();qcom_va_md_panic_handler(NULL,0,NULL);assert(!va_md_data.collecting);qcom_va_md_elf_panic_handler(NULL,0,NULL);rcu_read_unlock();assert(!va_md_data.in_oops_handler&&!rcu_readers);}
int main(void) {
 for(int i=0;i<128;i++)source_bytes[0][i]=i;
 initialize(false);run_pair();assert(!callback_calls&&!va_md_data.num_sections&&!va_md_data.va_md_minidump_reg);client.enable=true;run_pair();assert(callback_calls==1&&va_md_data.num_sections==1);check_elf(1,7,false);unsigned old_calls=callback_calls;run_pair();assert(callback_calls==old_calls&&((Elf64_Ehdr*)va_md_data.elf.ehdr)->e_shnum==3);cleanup();
 initialize(true);registration_error=-EIO;run_pair();assert(!va_md_data.va_md_minidump_reg&&va_md_data.num_sections==1);registration_error=0;run_pair();assert(va_md_data.va_md_minidump_reg&&va_md_data.num_sections==1);check_elf(1,7,false);cleanup();
 initialize(true);base_enabled=false;run_pair();assert(!va_md_data.in_oops_handler&&!va_md_data.num_sections);base_enabled=true;
 rcu_read_lock();qcom_va_md_panic_handler(NULL,0,NULL);assert(va_md_data.in_oops_handler&&!va_md_data.collecting);simulated_cpu=1;qcom_va_md_elf_panic_handler(NULL,0,NULL);assert(va_md_data.in_oops_handler);struct va_md_entry region={.vaddr=(unsigned long)source_bytes[1],.size=7};assert(qcom_va_md_add_region(&region)==-EINVAL);simulated_cpu=0;qcom_va_md_elf_panic_handler(NULL,0,NULL);rcu_read_unlock();assert(!va_md_data.in_oops_handler);check_elf(1,7,false);cleanup();
 printf("{\"panic_phase_cases\":7,\"recursive_capture_rejected\":true,\"failed_registration_retry\":true,\"collection_window_enforced\":true,\"secondary_cpu_rejected\":true,\"advertised_dump_not_rewritten\":true}\n");
}
