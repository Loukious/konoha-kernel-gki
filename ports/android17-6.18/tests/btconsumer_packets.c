#include "btconsumer_mock.h"
/* CODEC HEADER */
/* PACKET HEADER */
#define MIN_PKT_LEN 9
#define BT 1
#define BTADV 2
#define NONE 3
static struct btfmcodec_data context,*btfmcodec=&context;
static struct btfmcodec_char_device channel;
struct srcu_struct btfmcodec_endpoint_srcu;
static int endpoint_available=1,transport;
int btfmcodec_endpoint_enter(void){return endpoint_available?0:-ENODEV;}
void btfmcodec_endpoint_exit(int idx){}
static btfmcodec_state btfmcodec_get_current_transport(struct btfmcodec_state_machine*s){return s->current_state;}
static btfmcodec_state btfmcodec_get_prev_transport(struct btfmcodec_state_machine*s){return s->prev_state;}
#define wait_event_interruptible(q,c) ((c)?0:-ERESTARTSYS)
/* FUNCTIONS */
static void enqueue_raw(const void*data,size_t len){struct sk_buff*s=alloc_skb(len,0);skb_put_data(s,data,len);skb_queue_tail(&channel.rxq,s);}
int main(void){unsigned cases=0;context.btfmcodec_dev=&channel;channel.btfmcodec=&context;channel.active_clients=2;channel.ready=true;struct file f={.private_data=&context,.f_flags=O_NONBLOCK};channel.workqueue=create_singlethread_workqueue("bearer");u8 packet[16]={0};put_unaligned_le32(BTM_BTFMCODEC_CTRL_LOG_LVL_IND,packet);put_unaligned_le32(1,packet+4);packet[8]=4;
for(unsigned len=0;len<9;len++){enqueue_raw(packet,len);btfmcodec_dev_rxwork_impl(&channel.rx_work);assert(!live);cases++;}
for(unsigned len=0;len<32;len++){put_unaligned_le32(len,packet+4);ssize_t ret=btfmcodec_dev_write(&f,(char*)packet,9,NULL);assert(ret==(len==1?9:-EINVAL));if(ret==9){btfmcodec_dev_rxwork_impl(&channel.rx_work);assert(log_lvl==4);}assert(!live&&!channel.lock.held);cases++;}
put_unaligned_le32(2,packet+4);packet[8]=19;log_lvl=3;enqueue_raw(packet,9);btfmcodec_dev_rxwork_impl(&channel.rx_work);assert(log_lvl==3&&!live);
put_unaligned_le32(1,packet+4);assert(btfmcodec_dev_write(&f,(char*)packet,0,NULL)==0);assert(btfmcodec_dev_write(&f,(char*)packet,8,NULL)==-EMSGSIZE);assert(btfmcodec_dev_write(&f,(char*)packet,4097,NULL)==-EMSGSIZE);user_read_fault=1;assert(btfmcodec_dev_write(&f,(char*)packet,9,NULL)==-EFAULT&&!live);user_read_fault=0;alloc_fault=alloc_step+1;assert(btfmcodec_dev_write(&f,(char*)packet,9,NULL)==-ENOMEM&&!live);alloc_fault=0;mutex_fault=1;assert(btfmcodec_dev_write(&f,(char*)packet,9,NULL)==-ERESTARTSYS);mutex_fault=0;
unsigned opcodes[]={BTM_BTFMCODEC_MASTER_CONFIG_RSP,BTM_BTFMCODEC_CODEC_CONFIG_DMA_RSP,BTM_BTFMCODEC_CTRL_MASTER_SHUTDOWN_RSP,BTM_BTFMCODEC_BEARER_SWITCH_IND,BTM_BTFMCODEC_USECASE_START_RSP};unsigned indexes[]={BTM_PKT_TYPE_MASTER_CONFIG_RSP,BTM_PKT_TYPE_DMA_CONFIG_RSP,BTM_PKT_TYPE_MASTER_SHUTDOWN_RSP,BTM_PKT_TYPE_BEARER_SWITCH_IND,BTM_PKT_TYPE_USECASE_START_RSP};
for(unsigned op=0;op<5;op++)for(unsigned status=0;status<3;status++){unsigned n=op<3?2:1;put_unaligned_le32(opcodes[op],packet);put_unaligned_le32(n,packet+4);packet[8]=packet[9]=status;enqueue_raw(packet,n+8);btfmcodec_dev_rxwork_impl(&channel.rx_work);assert(channel.status[indexes[op]]==(status==0?BTM_RSP_RECV:BTM_FAIL_RESP_RECV));assert(!live);cases++;}
for(unsigned i=0;i<64;i++){put_unaligned_le32(1,packet+4);assert(btfmcodec_dev_write(&f,(char*)packet,9,NULL)==9);}assert(btfmcodec_dev_write(&f,(char*)packet,9,NULL)==-ENOSPC);skb_queue_purge(&channel.rxq);
char message[]="abcdef",out[8]={0};assert(!btfmcodec_dev_enqueue_pkt(&channel,message,6));assert(btfmcodec_dev_read(&f,out,2,NULL)==2&&!memcmp(out,"ab",2));assert(channel.txq.head->len==4);user_write_fault=1;assert(btfmcodec_dev_read(&f,out,4,NULL)==-EFAULT&&channel.txq.head->len==4);user_write_fault=0;assert(btfmcodec_dev_read(&f,out,4,NULL)==4&&!memcmp(out,"cdef",4));assert(btfmcodec_dev_read(&f,out,1,NULL)==-EAGAIN);assert(!live);
channel.closing=true;assert(btfmcodec_dev_write(&f,(char*)packet,9,NULL)==-ENETRESET);assert(btfmcodec_dev_enqueue_pkt(&channel,message,6)==-EINVAL);assert(btfmcodec_dev_read(&f,out,1,NULL)==-ENETRESET);channel.closing=false;
for(unsigned i=0;i<256;i++){int ret=btfmcodec_enqueue_transport(&channel,i);assert(ret==(i>=BT&&i<=NONE?0:-EINVAL));if(!ret)assert(btfmcodec_dequeue_transport(&channel)==(int)i);assert(!live);cases++;}
assert(btfmcodec_dequeue_transport(&channel)==255);destroy_workqueue(channel.workqueue);printf("{\"packet_and_transport_cases\":%u,\"short_headers_rejected\":true,\"partial_reads_retained\":true,\"usercopy_and_allocation_failures_balanced\":true,\"queues_bounded\":true}\n",cases);}
