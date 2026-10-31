/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
typedef uint64_t phys_addr_t;
typedef uint64_t __le64;
#define U32_MAX UINT32_MAX
#define U64_MAX UINT64_MAX
#define GFP_KERNEL 0
#define BIT(n) (1ULL<<(n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((long)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ALIGNED(v,a) (!((v)&((a)-1)))
#define check_add_overflow __builtin_add_overflow
#define min(a,b) ((a)<(b)?(a):(b))
#define dev_err(...) ((void)0)
#define pr_warn(...) ((void)0)
#define mutex_lock(p) ((void)0)
#define mutex_unlock(p) ((void)0)
#define memcpy_fromio memcpy
#define memcpy_toio memcpy
#define __iomem
struct device_node { unsigned index; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct rproc { void *priv; };
struct rproc_subdev { int unused; };
struct completion { bool ready; };
struct work_struct { bool initialized,pending; };
struct qmp { int unused; };
struct icc_path { int unused; };
struct qcom_smem_state { int unused; };
#define IRQF_TRIGGER_RISING 1
#define IRQF_ONESHOT 2
#define IRQF_NO_AUTOEN 4
static unsigned operation,fail_operation,irq_mask,qmp_refs,cleanup_count,stop_bit;
static unsigned cleanup_log[8];
static bool work_initialized;
static struct qmp fake_qmp;
static struct qcom_smem_state fake_state;
static struct icc_path fake_path;
static bool fail_step(void) { return ++operation==fail_operation; }
#define INIT_WORK(work,fn) do { (void)(fn); (work)->initialized=true; (work)->pending=true; work_initialized=true; } while(0)
static void init_completion(struct completion *c) { c->ready=false; }
static int platform_get_irq_byname(struct platform_device *p,const char *name) {
 (void)p; if(fail_step())return -EIO;
 const char *names[]={"wdog","fatal","ready","handover","stop-ack"};
 for(unsigned i=0;i<5;i++)if(!strcmp(name,names[i]))return i+1;
 abort();
}
static int devm_request_threaded_irq(struct device *dev,int irq,void *top,void *thread,int flags,const char *name,void *data) {
 (void)dev;(void)top;(void)thread;(void)name;(void)data;
 assert(work_initialized && irq>=1&&irq<=5);
 if(irq==4)assert(flags&IRQF_NO_AUTOEN);
 if(fail_step())return -EIO;
 assert(!(irq_mask&BIT(irq-1)));irq_mask|=BIT(irq-1);return 0;
}
static void devm_free_irq(struct device *dev,int irq,void *data) {
 (void)dev;(void)data;assert(irq_mask&BIT(irq-1));irq_mask&=~BIT(irq-1);cleanup_log[cleanup_count++]=irq;
}
static void cancel_work_sync(struct work_struct *w) { assert(w->initialized&&!irq_mask);w->pending=false; }
static void *devm_qcom_smem_state_get(struct device *dev,const char *name,unsigned *bit) {
 (void)dev;(void)name;if(fail_step())return ERR_PTR(-EIO);*bit=stop_bit;return &fake_state;
}
static const char *devm_kstrdup_const(struct device *dev,const char *s,int flag) { (void)dev;(void)flag;return fail_step()?NULL:s; }
static struct qmp *qmp_get(struct device *dev) { (void)dev;if(fail_step())return ERR_PTR(-EIO);qmp_refs++;return &fake_qmp; }
static void qmp_put(struct qmp *q) { assert(q==&fake_qmp&&qmp_refs--==1); }
static int dev_err_probe(struct device *d,int error,const char *fmt) { (void)d;(void)fmt;return error; }
static struct icc_path *devm_of_icc_get(struct device *dev,const char *name) { (void)dev;(void)name;return fail_step()?ERR_PTR(-EIO):&fake_path; }
static int q6v5_wdog_interrupt(void){return 0;}
static int q6v5_fatal_interrupt(void){return 0;}
static int q6v5_ready_interrupt(void){return 0;}
static int q6v5_handover_interrupt(void){return 0;}
static int q6v5_stop_interrupt(void){return 0;}
static void qcom_q6v5_crash_handler_work(void){}
static const char *smem_data;
static size_t smem_length;
static void *qcom_smem_get(unsigned h,unsigned id,size_t *n) { (void)h;(void)id;*n=smem_length;return (void *)smem_data; }
static char *kstrndup(const char *s,size_t n,int flag) { (void)flag;size_t len=strnlen(s,n);char *p=malloc(len+1);assert(p);memcpy(p,s,len);p[len]=0;return p; }

struct regulator { bool enabled; int voltage,load; };
struct reg_info { struct regulator *reg; u32 uV,uA; };
struct qcom_adsp {
 struct device *dev; int reg_cnt,enabled_regs; struct reg_info *regs;
 u64 mem_phys;size_t mem_size;void *mem_region;
 int region_assign_idx,region_assign_count,region_assign_vmid;
 bool region_assign_shared;unsigned assigned_regions;
 phys_addr_t region_assign_phys[3];size_t region_assign_size[3];u64 region_assign_perms[3];
};
static unsigned reg_operation,reg_fail,reg_disables;
static bool reg_should_fail(void){return ++reg_operation==reg_fail;}
static int regulator_set_voltage(struct regulator *r,int min,int max) { (void)max;if(min&&reg_should_fail())return -EIO;r->voltage=min;return 0; }
static int regulator_set_load(struct regulator *r,int load) { if(load&&reg_should_fail())return -EIO;r->load=load;return 0; }
static int regulator_enable(struct regulator *r) { if(reg_should_fail())return -EIO;assert(!r->enabled);r->enabled=true;return 0; }
static void regulator_disable(struct regulator *r) { assert(r->enabled);r->enabled=false;reg_disables++; }
struct rproc_dump_segment { dma_addr_t da;size_t size;int64_t offset; };
struct resource { u64 start;size_t size; };
#define resource_size(r) ((r)->size)
#define QCOM_SCM_VMID_HLOS 3
#define QCOM_SCM_PERM_RW 3
struct qcom_scm_vmperm { int vmid;int perm; };
static struct device_node nodes[3];
static unsigned assignment_operation,assignment_fail,node_refs,assigned_mask;
static bool assignment_fails(void) { return ++assignment_operation==assignment_fail; }
static struct device_node *of_parse_phandle(struct device_node *np,const char *name,int index) {
 (void)np;(void)name;assert(index>=1&&index<=3);if(assignment_fails())return NULL;
 nodes[index-1].index=index-1;node_refs++;return &nodes[index-1];
}
static int of_address_to_resource(struct device_node *np,int index,struct resource *r) {
 (void)index;if(assignment_fails())return -EIO;r->start=0x10000000+np->index*4096;r->size=4096;return 0;
}
static void of_node_put(struct device_node *np) { assert(np&&node_refs);node_refs--; }
static int qcom_scm_assign_mem(u64 address,size_t size,u64 *source,struct qcom_scm_vmperm *dest,unsigned count) {
 assert(size==4096&&address>=0x10000000&&address<0x10003000);
 unsigned i=(address-0x10000000)/4096;
 if(assignment_fails())return -EIO;
 bool unassign=count==1&&dest[0].vmid==QCOM_SCM_VMID_HLOS;
 if(unassign){assert(assigned_mask&BIT(i));assigned_mask&=~BIT(i);*source=BIT(QCOM_SCM_VMID_HLOS);}
 else {assert(!(assigned_mask&BIT(i)));assigned_mask|=BIT(i);*source=BIT(7);}
 return 0;
}
#define PIL_RELOC_NAME_LEN 8
#define PIL_RELOC_ENTRY_SIZE 20
static struct {void *base;size_t num_entries;} _reloc;
static int qcom_pil_info_init(const char *name) {(void)name;return 0;}
static void writel(u32 value,void *p){memcpy(p,&value,sizeof(value));}

struct firmware {size_t size;const u8 *data;};
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(h) struct list_head h={&(h),&(h)}
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry_safe(pos,tmp,head,m) \
 for(struct list_head *_n=(head)->next,*_next=NULL; _n!=(head) && (_next=_n->next,(pos)=container_of(_n,__typeof__(*(pos)),m),(tmp)=NULL,1); _n=_next)
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;}
struct qcom_dump_segment {struct list_head node;u64 da;size_t size;};
#define QCOM_MDT_TYPE_MASK (7<<24)
#define QCOM_MDT_TYPE_HASH (2<<24)
static unsigned allocations,alloc_operation,alloc_failure;
static void *kzalloc(size_t n,int flag){(void)flag;if(++alloc_operation==alloc_failure)return NULL;void *p=calloc(1,n);assert(p);allocations++;return p;}
static void kfree(void *p){if(p){assert(allocations);allocations--;free(p);}}
static unsigned elf_dump_calls,elf_segment_count;
static int qcom_elf_dump(struct list_head *h,struct device *dev,unsigned char class){
 (void)dev;assert(class==ELFCLASS32);elf_dump_calls++;elf_segment_count=0;
 for(struct list_head *n=h->next;n!=h;n=n->next)elf_segment_count++;
 return -EXDEV;
}
/* STAGED_DEFINITIONS */
static unsigned long test_ranges(void){
 unsigned long cases=0;const u64 bases[]={0,4096,1ULL<<32,1ULL<<40,UINT64_MAX-128};
 for(unsigned b=0;b<ARRAY_SIZE(bases);b++)for(int delta=-8;delta<144;delta++)for(size_t size=0;size<144;size++){
  u64 address=bases[b]+delta;size_t offset=SIZE_MAX;__int128 d=(__int128)address-bases[b];
  bool good=d>=0&&d<=128&&size<=128-d;bool result=adsp_range_offset(address,size,bases[b],128,&offset);
  assert(result==good);assert(offset==(good?(size_t)d:SIZE_MAX));cases++;
 }
 size_t offset=0;assert(!adsp_range_offset(1,SIZE_MAX,0,128,&offset));cases++;
 unsigned char memory[128];struct qcom_adsp adsp={.mem_phys=1ULL<<40,.mem_size=128,.mem_region=memory};struct rproc r={.priv=&adsp};
 for(int off=-1;off<=129;off++)for(size_t size=0;size<=129;size++){
  bool mmio=false;void *v=adsp_da_to_va(&r,adsp.mem_phys+off,size,&mmio);bool good=off>=0&&off<=128&&size<=128-off;
  assert((v!=NULL)==good&&mmio==good);if(good)assert(v==memory+off);cases++;
 }
 const u64 extreme[]={0,1,UINT64_MAX-128,UINT64_MAX};
 for(unsigned i=0;i<4;i++)for(int64_t so=-1;so<=128;so++)for(size_t off=0;off<=129;off++)for(size_t size=0;size<=128;size+=16){
  struct rproc_dump_segment seg={.da=extreme[i],.size=128,.offset=so};u64 address=0;
  __int128 total=(__int128)seg.da+so+off;bool good=so>=0&&off<=128&&size<=128-off&&total<=UINT64_MAX&&total+size<=UINT64_MAX;
  assert(adsp_dump_address(&seg,off,size,&address)==good);if(good)assert(address==(u64)total);cases++;
 }
 return cases;
}
static unsigned long test_regulators(void){
 unsigned long cases=0;
 for(unsigned count=0;count<=8;count++)for(unsigned fail=0;fail<=3*count;fail++){
  struct regulator reg[8]={};struct reg_info info[8];for(unsigned i=0;i<8;i++)info[i]=(struct reg_info){.reg=&reg[i],.uV=100+i,.uA=50+i};
  struct qcom_adsp a={.reg_cnt=count,.regs=info};reg_operation=reg_disables=0;reg_fail=fail;
  int ret=enable_regulators(&a);assert((ret==0)==(fail==0));if(!ret){unsigned before=reg_operation;assert(!enable_regulators(&a));assert(before==reg_operation);}
  disable_regulators(&a);disable_regulators(&a);assert(a.enabled_regs==0);
  for(unsigned i=0;i<8;i++)assert(!reg[i].enabled&&!reg[i].load&&!reg[i].voltage);cases++;
 }
 return cases;
}
static unsigned long test_q6(void){
 unsigned long cases=0;
 for(unsigned bit=0;bit<=32;bit++)for(unsigned fail=0;fail<=15;fail++){
  struct qcom_q6v5 q={};struct platform_device p={};struct rproc r={};
  operation=irq_mask=qmp_refs=cleanup_count=0;fail_operation=fail;stop_bit=bit;work_initialized=false;
  int ret=qcom_q6v5_init(&q,&p,&r,123,456,2,"adsp",NULL);
  if(ret)assert(!irq_mask&&!qmp_refs&&!q.requested_irqs&&!q.crash_handler.pending);
  else{assert(irq_mask==31&&qmp_refs==1&&bit<32);q6v5_release_resources(&q);}
  assert(!irq_mask&&!qmp_refs&&!q.requested_irqs&&!q.crash_handler.pending);
  for(unsigned i=1;i<cleanup_count;i++)assert(cleanup_log[i-1]>cleanup_log[i]);cases++;
 }
 for(size_t n=0;n<128;n++){
  char *input=malloc(n?n:1);memset(input,'x',n);smem_data=input;smem_length=n;
  char *copy=q6v5_copy_crash_message(0,1);assert((copy!=NULL)==(n>0));if(copy){assert(strlen(copy)==n&&!memcmp(copy,input,n));free(copy);}free(input);cases++;
 }
 smem_data=ERR_PTR(-EIO);smem_length=123;assert(!q6v5_copy_crash_message(0,1));cases++;
 return cases;
}
static unsigned long test_assignments(void){
 unsigned long cases=0;struct device_node parent={};struct device dev={.of_node=&parent};
 for(unsigned count=0;count<=3;count++)for(unsigned shared=0;shared<2;shared++)for(unsigned fail=0;fail<=3*count;fail++){
  struct qcom_adsp a={.dev=&dev,.region_assign_idx=1,.region_assign_count=count,.region_assign_vmid=7,.region_assign_shared=shared};
  assignment_operation=node_refs=assigned_mask=0;assignment_fail=fail;
  int ret=adsp_assign_memory_region(&a);assert((ret==0)==(fail==0));assert(!node_refs);
  assert(assigned_mask==((1U<<a.assigned_regions)-1));
  assignment_fail=0;adsp_unassign_memory_region(&a);
  if(shared){assert(assigned_mask==((1U<<a.assigned_regions)-1));assert(!adsp_assign_memory_region(&a));assert(a.assigned_regions==count);}
  else assert(!assigned_mask&&!a.assigned_regions);
  /* Also reclaim the simulated shared mapping once testing its retention is complete. */
  a.region_assign_shared=false;adsp_unassign_memory_region(&a);assert(!assigned_mask&&!a.assigned_regions);cases++;
 }
 return cases;
}
static unsigned long test_pil(void){
 unsigned long cases=0;
 for(unsigned slots=0;slots<=8;slots++)for(size_t n=1;n<=128;n++){
  unsigned char *bytes=malloc(slots*20+32);memset(bytes,0xa5,slots*20+32);memset(bytes+16,0,slots*20);
  _reloc.base=bytes+16;_reloc.num_entries=slots;char name[129];memset(name,'n',n);name[n]=0;
  int ret=qcom_pil_info_store(name,0x1234567887654321ULL,0xabcdef);assert((ret==0)==(slots>0));
  for(unsigned j=0;j<16;j++){assert(bytes[j]==0xa5);assert(bytes[16+slots*20+j]==0xa5);}
  if(slots){assert(!memcmp(bytes+16,name,n<8?n:8));u64 base;u32 size;memcpy(&base,bytes+24,8);memcpy(&size,bytes+32,4);assert(base==0x1234567887654321ULL&&size==0xabcdef);assert(!qcom_pil_info_store(name,7,9));}
  free(bytes);cases++;
 }
 assert(qcom_pil_info_store(NULL,0,0)==-EINVAL);assert(qcom_pil_info_store("",0,0)==-EINVAL);assert(qcom_pil_info_store("x",0,1ULL<<32)==-EINVAL);return cases+3;
}
static unsigned long test_elf(void){
 unsigned long cases=0;unsigned char *bytes=calloc(1,2048);Elf32_Ehdr *e=(void *)bytes;
 memcpy(e->e_ident,ELFMAG,SELFMAG);e->e_ident[EI_CLASS]=ELFCLASS32;e->e_ident[EI_DATA]=ELFDATA2LSB;e->e_phentsize=sizeof(Elf32_Phdr);
 for(size_t size=0;size<=256;size++)for(unsigned off=0;off<=260;off++)for(unsigned num=0;num<=16;num++){
  e->e_phoff=off;e->e_phnum=num;struct firmware fw={.data=bytes,.size=size};
  bool good=size>=sizeof(*e)&&off>=sizeof(*e)&&off<=size&&!(off&3)&&num*sizeof(Elf32_Phdr)<=size-off;
  assert(qcom_ramdump_elf32_valid(&fw)==good);cases++;
 }
 assert(!qcom_ramdump_elf32_valid(NULL));struct firmware empty={.size=64};assert(!qcom_ramdump_elf32_valid(&empty));cases+=2;
 for(unsigned count=0;count<16;count++)for(unsigned fail=0;fail<=count;fail++){
  e->e_phoff=sizeof(*e);e->e_phnum=count;Elf32_Phdr *ph=(void *)(bytes+sizeof(*e));
  for(unsigned i=0;i<count;i++)ph[i]=(Elf32_Phdr){.p_type=PT_LOAD,.p_paddr=0x1000+i*4096,.p_memsz=4096};
  struct firmware fw={.data=bytes,.size=sizeof(*e)+count*sizeof(*ph)};alloc_operation=allocations=elf_dump_calls=0;alloc_failure=fail;
  int ret=qcom_fw_elf_dump(&fw,NULL);assert(ret==(fail?-ENOMEM:-EXDEV));assert(!allocations);
  if(!fail)assert(elf_dump_calls==1&&elf_segment_count==count);else assert(!elf_dump_calls);cases++;
 }
 free(bytes);return cases;
}
int main(void){
 unsigned long ranges=test_ranges(),regulators=test_regulators(),q6=test_q6(),assign=test_assignments(),pil=test_pil(),elf=test_elf();
 printf("{\"address_range_cases\":%lu,\"regulator_rollback_cases\":%lu,\"q6_irq_and_crash_message_cases\":%lu,\"memory_assignment_cases\":%lu,\"pil_record_cases\":%lu,\"elf_table_and_cleanup_cases\":%lu}\n",ranges,regulators,q6,assign,pil,elf);return 0;
}
