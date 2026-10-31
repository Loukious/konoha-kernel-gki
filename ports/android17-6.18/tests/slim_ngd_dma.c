#include "slim_mock.h"
#define SLIM_MSGQ_BUF_LEN 40
#define QCOM_SLIM_NGD_DESC_NUM 30
#define DMA_MEM_TO_DEV 1
#define DMA_DEV_TO_MEM 2
#define DMA_PREP_INTERRUPT 1
#define dev_err(...) ((void)0)
#define SLIM_INFO(...) ((void)0)
#define memset_io(p,v,n) memset(p,v,n)
struct dma_chan{int pending;};typedef int dma_cookie_t;
struct dma_async_tx_descriptor{void(*callback)(void*);void*callback_param;};
struct qcom_slim_ngd_ctrl;
struct qcom_slim_ngd_dma_desc{struct dma_async_tx_descriptor*desc;struct qcom_slim_ngd_ctrl*ctrl;struct completion*comp;dma_cookie_t cookie;dma_addr_t phys;void*base;};
struct qcom_slim_ngd_ctrl{struct device*dev;struct dma_chan*dma_tx_channel,*dma_rx_channel;struct qcom_slim_ngd_dma_desc txdesc[30],rx_desc[30];spinlock_t tx_buf_lock;bool dma_stopping;struct{bool is_r_mem;void*r_vsbase;dma_addr_t r_pbase;}r_mem;void*tx_base,*rx_base;dma_addr_t tx_phys_base,rx_phys_base;int tx_tail,tx_head;};
static int prep_step,prep_fail,submit_step,submit_fail,terminations;static struct dma_chan*preparing;static struct dma_async_tx_descriptor desc;
static void qcom_slim_ngd_rx_msgq_cb(void*p){}
static struct dma_chan*dma_request_chan(struct device*d,const char*n){void*p=kzalloc(sizeof(struct dma_chan),0);return p?p:ERR_PTR(-ENOMEM);}
static void dma_release_channel(struct dma_chan*c){assert(!c->pending);kfree(c);}
static void*dma_alloc_coherent(struct device*d,size_t n,dma_addr_t*p,int g){void*b=kzalloc(n,g);*p=(uintptr_t)b;return b;}
static void dma_free_coherent(struct device*d,size_t n,void*p,dma_addr_t a){assert(p&&(uintptr_t)p==a);kfree(p);}
static struct dma_async_tx_descriptor*dmaengine_prep_slave_single(struct dma_chan*c,dma_addr_t p,int n,int dir,int flags){assert(c);preparing=c;if(++prep_step==prep_fail)return NULL;return &desc;}
static int dmaengine_submit(struct dma_async_tx_descriptor*d){if(++submit_step==submit_fail)return -EIO;preparing->pending++;return submit_step;}
#define dma_submit_error(cookie) ((cookie)<0?(cookie):0)
static void dma_async_issue_pending(struct dma_chan*c){}
static int dmaengine_terminate_sync(struct dma_chan*c){c->pending=0;terminations++;return 0;}
static int qcom_slim_ngd_exit_dma(struct qcom_slim_ngd_ctrl*ctrl);
/* FUNCTIONS */
static void clean(struct qcom_slim_ngd_ctrl*c){assert(!live&&!c->rx_base&&!c->tx_base&&!c->dma_rx_channel&&!c->dma_tx_channel);}
int main(void){struct device dev={0};struct qcom_slim_ngd_ctrl c={.dev=&dev};int cases=0;for(int n=1;n<=4;n++){alloc_fault=alloc_step+n;int ret=qcom_slim_ngd_init_dma(&c);assert(ret<0);clean(&c);alloc_fault=0;cases++;}
for(int phase=0;phase<2;phase++)for(int n=1;n<=30;n++){prep_step=submit_step=0;prep_fail=phase?0:n;submit_fail=phase?n:0;assert(qcom_slim_ngd_init_dma(&c)<0);clean(&c);cases++;}
prep_fail=submit_fail=0;prep_step=submit_step=0;assert(!qcom_slim_ngd_init_dma(&c)&&live==4&&c.dma_rx_channel->pending==30&&!c.dma_stopping);int before=terminations;assert(!qcom_slim_ngd_exit_dma(&c));assert(terminations==before+2);clean(&c);assert(!qcom_slim_ngd_exit_dma(&c));clean(&c);
u8 remote[2440];c.r_mem.is_r_mem=true;c.r_mem.r_vsbase=remote;c.r_mem.r_pbase=(uintptr_t)remote;for(int i=0;i<3;i++){prep_step=submit_step=0;assert(!qcom_slim_ngd_init_dma(&c));assert(c.rx_base==remote&&c.tx_base==remote+1200&&c.rx_phys_base==(uintptr_t)remote&&c.tx_phys_base==(uintptr_t)(remote+1200));assert(!qcom_slim_ngd_exit_dma(&c));clean(&c);}printf("{\"allocation_and_descriptor_faults\":%d,\"partial_dma_terminated_before_free\":true,\"repeated_cleanup_safe\":true,\"remote_buffer_offsets_stable\":true}\n",cases);}
