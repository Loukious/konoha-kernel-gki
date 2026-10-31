#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define __packed __attribute__((packed))
#define BIT_ULL(n) (1ULL << (n))
#define U32_MAX UINT32_MAX
#define PAGE_MASK (~4095UL)
#define PAGE_ALIGN(x) (((x) + 4095UL) & PAGE_MASK)
#define ALIGN(x,a) (((x) + (a) - 1) & ~((__typeof__(x))(a) - 1))
#define IS_ALIGNED(x,a) (!((x) & ((a)-1)))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define READ_ONCE(x) (x)
#define min_t(t,a,b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define WARN_ON_ONCE(x) (x)
#define le32_to_cpu(x) (x)
#define memcpy_fromio memcpy
#define IPCLITE_OS_LOG(...) ((void)0)
#define dev_err(...) ((void)0)
#define trace_kgsl_hfi_receive(...) ((void)0)
#define trace_kgsl_hfi_send(...) ((void)0)
#define rmb() ((void)0)
#define HFI_QUEUE_SIZE 4096
#define HFI_QUEUE_HDR_MAX 17
#define SZ_4 4
#define SZ_16 16
#define HFI_QUEUE_STATUS_DISABLED 0
#define HFI_MSG_ACK 1
#define MSG_HDR_GET_ID(hdr) ((hdr)&0xff)
#define MSG_HDR_GET_SIZE(hdr) (((hdr)>>8)&0xff)
#define MSG_HDR_GET_TYPE(hdr) (((hdr)>>16)&0xf)
#define MSG_HDR_GET_SEQNUM(hdr) (((hdr)>>20)&0xfff)
#define HFI_QUEUE_OFFSET(i) (ALIGN(sizeof(struct hfi_queue_table), SZ_16) + (i)*HFI_QUEUE_SIZE)
#define HOST_QUEUE_START_ADDR(mem,i) ((mem)->hostptr + HFI_QUEUE_OFFSET(i))
#define GMU_PDEV_DEV(x) NULL
#define KGSL_DEVICE(x) NULL
#define FIFO_FULL_RESERVE 8
#define IPCMEM_INIT_COMPLETED 1
struct kgsl_device { int unused; };
struct kgsl_memdesc { void *hostptr; u64 size; u32 gmuaddr; };
struct gen8_hfi { struct kgsl_memdesc *hfi_mem; };
struct gmu_vma_entry { u32 start, size; };
struct gen8_gmu_device {
 struct gen8_hfi hfi;
 int global_entries, num_vmas;
 struct kgsl_memdesc gmu_globals[2];
 struct gmu_vma_entry vma[2];
};
struct adreno_device { struct gen8_gmu_device *gmu; };
static struct gen8_gmu_device *to_gen8_gmu(struct adreno_device *a) { return a->gmu; }
struct ipclite_fifo { u32 *head, *tail; char *fifo; u32 length; };
struct ipclite_info { struct { struct { void *virt_base; } mem; } ipcmem; };
static struct ipclite_info *ipclite;
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define DMA_FENCE_FLAG_SEQNO64_BIT 0
#define test_bit(n,p) (!!(*(p)&(1UL<<(n))))
#define lower_32_bits(v) ((u32)(v))
struct dma_fence { u64 seqno; unsigned long flags; };
struct kgsl_timeline { u64 value; };
struct kgsl_timeline_fence { struct dma_fence base; struct kgsl_timeline *timeline; };
/* STAGED_TYPES */
static void hfi_update_read_idx(struct hfi_queue_header *h, u32 n) { h->read_index=n; }
static void hfi_update_write_idx(volatile void *p, u32 n) { memcpy((void *)p,&n,sizeof(n)); }
/* STAGED_DEFINITIONS */
static u64 seed=0x13dc004412345678ULL;
static u32 random32(void) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed; }
static unsigned long math_cases, fifo_cases, hfi_cases, firmware_cases;
static void test_math(void) {
 u32 values[]={0,1,2,3,4,8,16,4095,4096,4097,INT_MAX,0xffffe000U,0xfffff000U,0xfffffffeU,UINT32_MAX};
 for(unsigned a=0;a<ARRAY_SIZE(values);a++)
 for(unsigned b=0;b<ARRAY_SIZE(values);b++)
 for(unsigned c=0;c<ARRAY_SIZE(values);c++)
 for(unsigned d=0;d<ARRAY_SIZE(values);d++) {
  u32 out=0x1234;u32 pad=values[(a+b+c+d)%ARRAY_SIZE(values)];
  u64 total=(u64)values[a]+(u64)values[b]*values[c]+values[d]+pad;
  int r=hw_fence_checked_region_size(values[a],values[b],values[c],values[d],pad,&out);
  assert((r==0)==(total<=0xfffff000ULL));
  if(!r)assert(out==((total+4095)&~4095ULL));else assert(out==0x1234);
  math_cases++;
 }
 for(unsigned i=0;i<500000;i++) {
  u32 limit=random32();int pages=(int)random32();
  u32 r=kgsl_reclaim_remaining(limit,pages);
  u64 expected=pages>=0&&(u32)pages<limit ? (u64)limit-pages:0;
  assert(r==expected);math_cases++;
 }
}
static void test_fifo(void) {
 for(u32 length=8;length<=128;length+=8) {
  char *mem=malloc(length);u32 head,tail;struct ipclite_fifo f={&head,&tail,mem,length};
  for(head=0;head<=length+8;head++)for(tail=0;tail<=length+8;tail++) {
   size_t rx=ipcmem_rx_avail(&f),tx=ipcmem_tx_avail(&f);
   if(head>=length||tail>=length){assert(!rx&&!tx);} else {
    assert(rx==(head+length-tail)%length);
    size_t n=length-(head+length-tail)%length;assert(tx==(n<8?0:n-8));
   }
   fifo_cases++;
  }
  for(head=0;head<length;head++)for(u32 n=0;n<=length;n++) {
   char src[128],got[128],expected[128];
   for(u32 i=0;i<length;i++)src[i]=random32();
   memset(mem,0x52,length);memset(expected,0x52,length);
   for(u32 i=0;i<n;i++)expected[(head+i)%length]=src[i];
   unsigned next=ipcmem_tx_write_one(&f,head,src,n);
   assert(next==(head+n)%length);assert(!memcmp(mem,expected,length));
   tail=head;memset(got,0xcc,sizeof(got));ipcmem_rx_peak(&f,got,n);
   assert(!memcmp(got,src,n));fifo_cases++;
  }
  free(mem);
 }
}
static bool firmware_oracle(const u8 *data,size_t n) {
 if(!n)return false;
 size_t off=0;
 while(off<n) {
  if(n-off<16)return false;
  u32 addr,len;memcpy(&addr,data+off,4);memcpy(&len,data+off+4,4);
  off+=16;if((u64)addr+len>0x100000000ULL||len>n-off)return false;off+=len;
 }
 return true;
}
static void test_firmware(void) {
 u8 data[512];
 for(unsigned run=0;run<200000;run++) {
  memset(data,0,sizeof(data));size_t off=0;
  while(off+16<sizeof(data)) {
   struct gmu_block_header b={random32(),random32()%48,random32()%4,random32()};
   if(b.size>sizeof(data)-off-16)b.size=0;
   memcpy(data+off,&b,sizeof(b));off+=sizeof(b)+b.size;
  }
  size_t n=(run%2)?off:random32()%sizeof(data);
  if(run%3==0){u32 invalid=random32();memcpy(data+4,&invalid,4);}
  assert(gen8_gmu_fw_valid(data,n)==firmware_oracle(data,n));firmware_cases++;
 }
 struct gen8_gmu_device g={.num_vmas=2,.global_entries=2,
  .vma={{0x1000,0x2000},{0xffff0000,0xffff}},
  .gmu_globals={{.gmuaddr=0x1000,.size=0x2000},{.gmuaddr=0xffff0000,.size=0xffff}}};
 for(unsigned i=0;i<200000;i++) {
  u32 addr=random32(),len=random32();int expected=-ENOENT;
  if(i%3==0){addr=0xffff0000U+(random32()&0xffff);len=random32()&0x1ffff;}
  if(i%5==0){addr=0x1000+(random32()&0x3fff);len=random32()&0x3fff;}
  for(int j=0;j<2;j++)if(addr>=g.vma[j].start&&(u64)addr+len<=(u64)g.vma[j].start+g.vma[j].size){expected=j;break;}
  assert(find_vma_block(&g,addr,len)==expected);
  assert(find_gmu_memdesc(&g,addr,len)==(expected<0?NULL:&g.gmu_globals[expected]));firmware_cases++;
 }
}
static void test_hfi(void) {
 size_t sz=HFI_QUEUE_OFFSET(17);void *mem=calloc(1,sz);assert(mem);
 struct kgsl_memdesc md={.hostptr=mem,.size=sz};
 struct gen8_gmu_device g={.hfi={&md}};struct adreno_device a={&g};
 struct hfi_queue_table *tbl=mem;u32 out[256],msg[256];
 for(u32 rd=0;rd<1050;rd++)for(u32 wr=0;wr<1050;wr++) {
  assert(gen8_hfi_ring_valid(1024,rd,wr)==(rd<1024&&wr<1024&&rd%4==0&&wr%4==0));hfi_cases++;
 }
 for(unsigned i=0;i<100000;i++) {
  unsigned q=i%17;struct hfi_queue_header *hdr=&tbl->qhdr[q];
  *hdr=(struct hfi_queue_header){.status=1,.queue_size=1024};
  u32 start=(random32()%256)*4,n=1+random32()%128;
  hdr->read_index=hdr->write_index=start;
  for(u32 j=0;j<n;j++)msg[j]=random32();msg[0]=(n<<8)|23;
  assert(!gen8_hfi_queue_write(&a,q,msg,n*4));
  assert(hdr->write_index==(start+ALIGN(n,4))%1024);
  assert(gen8_hfi_queue_read(&g,q,out,sizeof(out))==(int)n);
  assert(!memcmp(out,msg,n*4));assert(hdr->read_index==hdr->write_index);hfi_cases++;
  /* Guard invalid indices and truncated or malformed firmware messages. */
  hdr->read_index=UINT32_MAX;assert(gen8_hfi_queue_read(&g,q,out,sizeof(out))==-EPROTO);
  hdr->read_index=0;hdr->queue_size=0;assert(gen8_hfi_queue_write(&a,q,msg,n*4)==-EPROTO);
  *hdr=(struct hfi_queue_header){.status=1,.queue_size=1024,.write_index=4};
  u32 *queue=HOST_QUEUE_START_ADDR(&md,q);queue[0]=(8<<8);assert(gen8_hfi_queue_read(&g,q,out,sizeof(out))==-EPROTO);
  queue[0]=(1<<8)|(HFI_MSG_ACK<<16);assert(gen8_hfi_queue_read(&g,q,out,sizeof(out))==-EPROTO);
  queue[0]=0;assert(gen8_hfi_queue_read(&g,q,out,sizeof(out))==-EPROTO);hfi_cases+=5;
 }
 assert(gen8_hfi_queue_read(&g,UINT32_MAX,out,sizeof(out))==-EINVAL);
 assert(gen8_hfi_queue_write(&a,17,msg,4)==-EINVAL);
 md.size=HFI_QUEUE_OFFSET(1);assert(gen8_hfi_queue_read(&g,1,out,sizeof(out))==-EINVAL);hfi_cases+=3;
 free(mem);
}
static void test_checksum(void) {
 struct ipcmem_toc_header hdr;struct ipclite_info info={.ipcmem.mem.virt_base=&hdr};ipclite=&info;
 for(unsigned i=0;i<10000;i++) {
  u32 *words=(void*)&hdr;for(unsigned j=0;j<sizeof(hdr)/4;j++)words[j]=random32();
  hdr.magic_number=0;hdr.init_done=0;insert_magic_number();hdr.init_done=1;
  u32 sum=0;for(unsigned j=0;j<sizeof(hdr)/4;j++)sum^=words[j];
  assert(sum==UINT32_MAX);firmware_cases++;
 }
}
int main(void) {
 test_math();test_fifo();test_firmware();test_hfi();test_checksum();
 struct kgsl_timeline t;struct kgsl_timeline_fence f={.base.flags=1,.timeline=&t};
 for(unsigned i=0;i<200000;i++) {
  f.base.seqno=((u64)random32()<<32)|random32();t.value=((u64)random32()<<32)|random32();
  if(i%7==0)t.value=f.base.seqno;
  assert(timeline_fence_signaled(&f.base)==(f.base.seqno<=t.value));math_cases++;
 }
 u64 edges[]={0,1,0x7fffffff,0xffffffffULL,0x100000000ULL,0x8000000000000000ULL,UINT64_MAX};
 for(unsigned i=0;i<ARRAY_SIZE(edges);i++)for(unsigned j=0;j<ARRAY_SIZE(edges);j++) {
  f.base.seqno=edges[i];t.value=edges[j];assert(timeline_fence_signaled(&f.base)==(edges[i]<=edges[j]));math_cases++;
 }
 printf("{\"region_reclaim_cases\":%lu,\"ipclite_fifo_cases\":%lu,\"gen8_firmware_cases\":%lu,\"gen8_hfi_cases\":%lu}\n",math_cases,fifo_cases,firmware_cases,hfi_cases);
}
