#include "slim_mock.h"
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{int unused;};struct idr{void*slot[256];unsigned cursor;};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
/* PUBLIC HEADER */
/* PRIVATE HEADER */
/* NGD CONSTANTS */

#undef SLIM_INFO
#undef SLIM_WARN
#undef SLIM_ERR
#define SLIM_INFO(...) ((void)0)
#define SLIM_WARN(...) ((void)0)
#define SLIM_ERR(...) ((void)0)
#define dev_err(...) ((void)0)
#define ALIGN(v,n) (((v)+(n)-1)&~((n)-1))
#define HZ 250
#define DECLARE_COMPLETION_ONSTACK(n) struct completion n={0}
#define memcpy_toio(d,s,n) memcpy(d,s,n)
#define DMA_MEM_TO_DEV 1
#define DMA_PREP_INTERRUPT 1
struct dma_chan{int unused;};typedef int dma_cookie_t;
struct dma_async_tx_descriptor{void(*callback)(void*);void*callback_param;};
struct qcom_slim_ngd_ctrl;
struct qcom_slim_ngd_dma_desc{struct dma_async_tx_descriptor*desc;struct qcom_slim_ngd_ctrl*ctrl;struct completion*comp;dma_cookie_t cookie;dma_addr_t phys;void*base;};
struct qcom_slim_ngd_ctrl{struct slim_controller ctrl;struct device*dev;struct dma_chan*dma_tx_channel;struct qcom_slim_ngd_dma_desc txdesc[QCOM_SLIM_NGD_DESC_NUM];struct workqueue_struct*mwq;struct work_struct m_work;struct mutex tx_lock;spinlock_t tx_buf_lock;bool stopping;struct{bool is_r_mem;}r_mem;void*tx_base;dma_addr_t tx_phys_base;int tx_tail,tx_head;};
static int hw_error,prep_error,submit_error,tx_timeout,reply_timeout,idr_error,response_error;
static int posts,received,last_length;static u8 last_rx_tid;static u8 sent[40];static struct slim_msg_txn*pending;static struct dma_async_tx_descriptor dma;
static int check_hw_state(struct qcom_slim_ngd_ctrl*c,struct slim_msg_txn*t){return hw_error;}
static bool mutex_trylock(struct mutex*m){if(m->held)return false;mutex_lock(m);return true;}
static void pm_runtime_mark_last_busy(struct device*d){}
int slim_alloc_txn_tid(struct slim_controller*c,struct slim_msg_txn*t){if(idr_error)return -ENOSPC;assert(!pending);t->tid=9;pending=t;return 0;}
void slim_free_txn_tid(struct slim_controller*c,struct slim_msg_txn*t){if(pending==t)pending=NULL;t->tid=0;}
void slim_msg_response(struct slim_controller*c,u8*b,u8 tid,u8 len){received++;last_length=len;last_rx_tid=tid;}
static struct dma_async_tx_descriptor*dmaengine_prep_slave_single(struct dma_chan*c,dma_addr_t p,int n,int direction,int flags){assert(c);if(prep_error)return NULL;assert(n>0&&n<=40);memcpy(sent,(void*)(uintptr_t)p,n);return &dma;}
static int dmaengine_submit(struct dma_async_tx_descriptor*d){posts++;return submit_error?-EIO:1;}
#define dma_submit_error(cookie) ((cookie)<0?(cookie):0)
static void dma_async_issue_pending(struct dma_chan*c){if(!tx_timeout)dma.callback(dma.callback_param);if(pending&&!reply_timeout){pending->result=response_error;complete(pending->comp);pending=NULL;}}
#define wait_for_completion_timeout(c,t) ((c)->done?1:0)
/* FUNCTIONS */
static void reset(struct qcom_slim_ngd_ctrl*c){assert(!pending);memset(c->tx_base,0x77,1200);memset(c->txdesc,0,sizeof(c->txdesc));c->tx_head=c->tx_tail=0;hw_error=prep_error=submit_error=tx_timeout=reply_timeout=idr_error=response_error=0;posts=received=0;}
int main(void){struct device dev={0};struct dma_chan channel={0};struct workqueue_struct wq={.alive=1};struct qcom_slim_ngd_ctrl c={.ctrl={.dev=&dev},.dev=&dev,.dma_tx_channel=&channel,.mwq=&wq};dev.data=&c;c.tx_base=malloc(1200);c.tx_phys_base=(uintptr_t)c.tx_base;u8 data[40]={7,8};struct slim_val_inf msg={.wbuf=data,.num_bytes=2};struct slim_msg_txn txn={.mt=0,.mc=SLIM_MSG_MC_CONNECT_SOURCE,.dt=0,.la=5,.msg=&msg,.rl=6};int scenarios=0;
for(int scenario=0;scenario<9;scenario++){reset(&c);int expected=0;switch(scenario){case 1:hw_error=-EHOSTDOWN;expected=-EHOSTDOWN;break;case 2:prep_error=1;expected=-ENOMEM;break;case 3:submit_error=1;expected=-EIO;break;case 4:tx_timeout=1;expected=-ETIMEDOUT;break;case 5:reply_timeout=1;expected=-ETIMEDOUT;break;case 6:idr_error=1;expected=-ENOSPC;break;case 7:response_error=-EMSGSIZE;expected=-EMSGSIZE;break;case 8:c.stopping=true;expected=-ESHUTDOWN;break;}assert(qcom_slim_ngd_xfer_msg(&c.ctrl,&txn)==expected);assert(txn.mt==0&&txn.mc==SLIM_MSG_MC_CONNECT_SOURCE&&txn.rl==6&&txn.msg==&msg&&msg.wbuf==data&&!pending&&!c.tx_lock.held);for(int i=0;i<30;i++)assert(!c.txdesc[i].comp);if(scenario==0){assert(sent[0]==(7|(2<<5))&&sent[1]==SLIM_USR_MC_CONNECT_SRC&&sent[2]==255&&sent[3]==5&&sent[4]==7&&sent[5]==8&&sent[6]==9&&sent[7]==0);}if(tx_timeout){dma.callback(dma.callback_param);assert(c.tx_head==1);}if(prep_error||submit_error)assert(c.tx_head==c.tx_tail);c.stopping=false;scenarios++;}
reset(&c);txn.mc=SLIM_MSG_MC_CHANGE_VALUE;txn.rl=8;msg.num_bytes=2;assert(!qcom_slim_ngd_xfer_msg(&c.ctrl,&txn));assert(sent[0]==7&&sent[1]==SLIM_MSG_MC_CHANGE_VALUE&&sent[5]==7&&sent[6]==8);for(int len=0;len<=40;len++){reset(&c);msg.num_bytes=len;txn.rl=len+6;int ret=qcom_slim_ngd_xfer_msg(&c.ctrl,&txn);assert(ret==(len<=26?0:-EMSGSIZE));scenarios++;}
reset(&c);txn.mc=SLIM_MSG_MC_CONNECT_SOURCE;msg.num_bytes=0;assert(qcom_slim_ngd_xfer_msg(&c.ctrl,&txn)==-EINVAL&&!posts);msg.num_bytes=2;txn.dt=1;assert(qcom_slim_ngd_xfer_msg(&c.ctrl,&txn)==-EPROTONOSUPPORT);txn.dt=0;
for(int len=0;len<32;len++){reset(&c);u8*frame=calloc(len?len:1,1);frame[0]=len|(SLIM_MSG_MT_SRC_REFERRED_USER<<5);if(len>1)frame[1]=SLIM_USR_MC_GENERIC_ACK;if(len>3)frame[3]=23;qcom_slim_ngd_rx(&c,frame);assert(received==(len>=4));if(received)assert(last_rx_tid==23&&last_length==len-4);free(frame);scenarios++;}
for(int len=2;len<32;len++){reset(&c);u8 frame[40]={len|(6<<5),SLIM_USR_MC_ADDR_REPLY,0,6};qcom_slim_ngd_rx(&c,frame);assert(received==(len==11));}
reset(&c);struct slim_device sdev={.ctrl=&c.ctrl};struct slim_framer framer={.superfreq=4000};c.ctrl.a_framer=&framer;struct slim_stream_runtime rt={.dev=&sdev,.rate=48000,.ratem=12};int exp;assert(qcom_slim_calc_coef(&rt,&exp)==1&&exp==2);
txn.mc=SLIM_MSG_MC_BEGIN_RECONFIGURATION;txn.msg=NULL;assert(!qcom_slim_ngd_xfer_msg(&c.ctrl,&txn));free(c.tx_base);printf("{\"transfer_and_rx_cases\":%d,\"late_dma_callback_safe\":true,\"failed_post_ring_rollback\":true,\"caller_transaction_unchanged\":true,\"frame_padding_zero\":true,\"address_reply_length_checked\":true}\n",scenarios);}
