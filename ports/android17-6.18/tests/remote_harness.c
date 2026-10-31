/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
#define U32_MAX UINT32_MAX
#define __packed __attribute__((packed))
#define GFP_KERNEL 0
#define BIT(n) (1UL << (n))
#define IS_ALIGNED(x,a) (!((x) & ((a)-1)))
#define READ_ONCE(x) (x)
static u32 readl(const void *p) { u32 v; memcpy(&v,p,sizeof(v)); return v; }
#define mb() ((void)0)
#define SMP2P_MAX_ENTRY 16
#define SMP2P_MAX_ENTRY_NAME 16
#define SMP2P_INFO(...) ((void)0)
#define GLINK_SMEM_INFO(...) ((void)0)
#define FIFO_FULL_RESERVE 8
#define TX_BLOCKED_CMD_RESERVE 8
#define SMEM_GLINK_NATIVE_XPRT_FIFO_1 479
#define pr_err(...) ((void)0)
#define WARN_ON_ONCE(x) (x)
#define min_t(t,a,b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define le32_to_cpu(x) (x)
#define cpu_to_le32(x) (x)
#define memcpy_fromio memcpy
#define IS_ERR(x) ((uintptr_t)(x) > (uintptr_t)-4096)
#define PTR_ERR(x) ((long)(x))
#define QMI_ENCDEC_DECODE_N_BYTES(dst,src,sz) do { memcpy(dst,src,sz); dst=(u8 *)dst+sz; src=(const u8 *)src+sz; } while(0)
struct device { int dummy; };
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(name) struct list_head name = {&(name), &(name)}
#define INIT_LIST_HEAD(p) do { (p)->next=(p); (p)->prev=(p); } while (0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(pos,head,m) \
 for(struct list_head *_n=(head)->next; _n!=(head) && ((pos)=container_of(_n,__typeof__(*(pos)),m),1); _n=_n->next)
#define list_for_each_entry_safe(pos,tmp,head,m) \
 for(struct list_head *_n=(head)->next,*_next=NULL; _n!=(head) && (_next=_n->next,(pos)=container_of(_n,__typeof__(*(pos)),m),(tmp)=NULL,1); _n=_next)
static void list_add_tail(struct list_head *n, struct list_head *h) { n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n; }
static void list_del(struct list_head *n) { n->prev->next=n->next; n->next->prev=n->prev; }
#define spin_lock_irqsave(lock, flags) ((void)0)
#define spin_unlock_irqrestore(lock, flags) ((void)0)
#define kzalloc(n,flag) calloc(1,n)
#define kfree free
#define DMA_FROM_DEVICE 1
static unsigned dma_calls;
static dma_addr_t last_dma;
static size_t last_dma_len;
static void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma, size_t n, int dir) {
 assert(dev && dir==DMA_FROM_DEVICE); dma_calls++; last_dma=dma; last_dma_len=n;
}
struct qcom_glink_smem { unsigned int remote_pid; };
struct qcom_glink_pipe { size_t length; };
struct glink_smem_pipe { struct qcom_glink_pipe native; u32 *tail,*head; void *fifo; struct qcom_glink_smem *smem; };
#define to_smem_pipe(p) container_of(p,struct glink_smem_pipe,native)
static size_t smem_size;
static void *smem_bytes;
static void *qcom_smem_get(unsigned pid,unsigned id,size_t *size) { (void)pid; (void)id; *size=smem_size; return smem_bytes; }
struct smp2p_smem_item;
struct qcom_smp2p { struct smp2p_smem_item *in; u16 valid_entries; struct list_head inbound; unsigned remote_pid; };
struct smp2p_entry { struct list_head node; const char *name; u32 *value; u32 last_value; unsigned long irq_enabled[1],irq_pending[1],irq_rising[1],irq_falling[1]; void *domain; };
#define for_each_set_bit(i,p,n) for(i=0;i<(n);i++) if(*(p) & BIT(i))
static bool test_bit(int bit,unsigned long *p) { return !!(*p & BIT(bit)); }
static void clear_bit(int bit,unsigned long *p) { *p &= ~BIT(bit); }
static int irq_find_mapping(void *domain,int i) { (void)domain; return i; }
static unsigned irq_calls;
static void handle_nested_irq(int irq) { (void)irq; irq_calls++; }
/* STAGED_DEFINITIONS */
static unsigned long test_qmi(void) {
 unsigned long cases=0; unsigned char src[512],dst[512];
 for(unsigned i=0;i<512;i++)src[i]=i;
 for(u32 n=0;n<=32;n++) for(u32 size=0;size<=16;size++) for(u32 avail=0;avail<=512;avail++) {
  memset(dst,0xa5,sizeof(dst)); int ret=qmi_decode_basic_elem(dst,src,n,size,avail);
  bool good=size && (u64)n*size<=avail;
  assert((ret>=0)==good);
  if(good) { assert(ret==(int)(n*size)); assert(!memcmp(dst,src,ret)); for(unsigned j=ret;j<512;j++)assert(dst[j]==0xa5); }
  else for(unsigned j=0;j<512;j++) assert(dst[j]==0xa5);
  cases++;
 }
 const u32 extremes[]={UINT32_MAX,1U<<31,1U<<30};
 for(unsigned i=0;i<3;i++) for(u32 size=2;size<=16;size*=2) {
  assert(qmi_decode_basic_elem(dst,src,extremes[i],size,512)==-EINVAL); cases++;
 }
 return cases;
}
static unsigned long test_memshare(void) {
 unsigned long cases=0; unsigned char bytes[64]; struct device dev={};
 const u32 bases[]={0,0x1000,UINT32_MAX-64,UINT32_MAX};
 for(unsigned i=0;i<4;i++) {
  u64 base=bases[i]; dma_addr_t dma=(1ULL<<40)+base;
  struct qcom_glink_mem_entry *m=qcom_glink_mem_entry_init(&dev,bytes,dma,sizeof(bytes),base); assert(m);
  for(int off=-8;off<=72;off++) for(size_t len=0;len<=72;len++) {
   u64 da=base+off; bool good=da>=base && da-base<=64 && len<=64-(da-base);
   unsigned before=dma_calls; void *p=qcom_glink_prepare_da_for_cpu(da,len);
   assert((p!=NULL)==good);
   if(good) { assert(p==bytes+off && dma_calls==before+1 && last_dma==dma+off && last_dma_len==len); if(len)memset(p,0xa5,len); }
   else assert(dma_calls==before);
   cases++;
  }
  assert(!qcom_glink_prepare_da_for_cpu(base+(1ULL<<32),1));
  assert(!qcom_glink_prepare_da_for_cpu(UINT64_MAX,SIZE_MAX));
  assert(!qcom_glink_prepare_da_for_cpu(base+1,SIZE_MAX)); cases+=3;
  qcom_glink_mem_entry_free(m); assert(qcom_glink_mem_entries.next==&qcom_glink_mem_entries);
 }
 return cases;
}
static unsigned long test_rings(void) {
 unsigned long cases=0; struct qcom_glink_smem smem={}; u32 head=0,tail=0;
 for(size_t size=0;size<=80;size++) assert(glink_smem_size_valid(size)==(size>16 && !(size&7)));
 assert(!glink_smem_size_valid(UINT64_MAX)); assert(!glink_smem_size_valid(1ULL<<32));
 for(size_t size=24;size<=64;size+=8) {
  unsigned char *fifo=malloc(size),*src=malloc(size),*dst=malloc(size); assert(fifo&&src&&dst);
  struct glink_smem_pipe p={.native.length=size,.tail=&tail,.head=&head,.fifo=fifo,.smem=&smem};
  for(head=0;head<=80;head++) for(tail=0;tail<=80;tail++) {
   size_t rx=glink_smem_rx_avail(&p.native),tx=glink_smem_tx_avail(&p.native);
   if(head>=size || tail>=size)assert(!rx&&!tx);
   else { size_t used=(head+size-tail)%size; size_t free=tail<=head?size-head+tail:tail-head;
    assert(rx==used); assert(tx==(free<16?0:free-16)); }
   cases++;
  }
  for(head=0;head<=80;head++) for(size_t count=0;count<=80;count++) {
   memset(fifo,0xa5,size); memset(src,0x3c,size);
   unsigned h=glink_smem_tx_write_one(&p,head,src,count);
   if(head>=size || count>size) { assert(h==head); for(size_t j=0;j<size;j++)assert(fifo[j]==0xa5); }
   else { assert(h==(head+count)%size); for(size_t j=0;j<size;j++)assert(fifo[(head+j)%size]==(j<count?0x3c:0xa5)); }
   cases++;
  }
  for(tail=0;tail<=80;tail++) for(unsigned offset=0;offset<=80;offset++) for(size_t count=0;count<=size;count+=7) {
   for(size_t j=0;j<size;j++)fifo[j]=j;
   memset(dst,0xa5,size); glink_smem_rx_peek(&p.native,dst,offset,count);
   bool good=tail<size && offset<size && count<=size-offset;
   for(size_t j=0;j<size;j++) assert(dst[j]==(good && j<count?(tail+offset+j)%size:0xa5));
   cases++;
  }
  for(u32 t=0;t<=80;t++) for(size_t count=0;count<=80;count++) {
   tail=t; glink_smem_rx_advance(&p.native,count);
   assert(tail==(t<size&&count<=size?(t+count)%size:t)); cases++;
  }
  free(fifo); free(src); free(dst);
 }
 /* A lazily acquired zero/undersized/misaligned RX FIFO is never published. */
 for(smem_size=0;smem_size<32;smem_size++) {
  unsigned char fifo[32]; smem_bytes=fifo; head=tail=0;
  struct glink_smem_pipe p={.head=&head,.tail=&tail,.smem=&smem};
  assert(!glink_smem_rx_avail(&p.native)); assert((p.fifo!=NULL)==glink_smem_size_valid(smem_size)); cases++;
 }
 return cases;
}
static unsigned long test_smp2p(void) {
 unsigned long cases=0;
 for(unsigned n=0;n<=UINT16_MAX;n++) {
  struct smp2p_smem_item in={.valid_entries=n};
  struct qcom_smp2p smp={.in=&in}; INIT_LIST_HEAD(&smp.inbound);
  struct smp2p_entry entry={.name="abcdefghijklmnop",.irq_enabled={0}};
  list_add_tail(&entry.node,&smp.inbound);
  memcpy(in.entries[15].name,"abcdefghijklmnop",16);
  qcom_smp2p_notify_in(&smp);
  if(n>16) assert(!smp.valid_entries && !entry.value);
  else { assert(smp.valid_entries==n); assert((entry.value!=NULL)==(n==16)); }
  cases++;
 }
 assert(!irq_calls);
 return cases;
}
int main(void) {
 unsigned long qmi=test_qmi(),mem=test_memshare(),ring=test_rings(),smp=test_smp2p();
 printf("{\"qmi_decode_cases\":%lu,\"glink_dma_range_cases\":%lu,\"glink_ring_cases\":%lu,\"smp2p_entry_cases\":%lu}\n",qmi,mem,ring,smp);
 return 0;
}
