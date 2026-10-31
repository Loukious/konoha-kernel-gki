#include "gsi_mock.h"
#define MAXQUEUELEN 128
#define EVT_NONE 0
#define RNDIS_MSG_INIT 2
#define RNDIS_MSG_HALT 3
#define RNDIS_MSG_QUERY 4
#define RNDIS_MSG_SET 5
#define RNDIS_MSG_RESET 6
#define RNDIS_MSG_KEEPALIVE 8
#define RNDIS_OID_GEN_CURRENT_PACKET_FILTER 0x1010e
/* RNDIS_TYPES */
struct event_queue{u8 event[MAXQUEUELEN],head,tail;spinlock_t q_lock;};
struct gsi_data_port{struct event_queue evt_q;struct workqueue_struct*ipa_usb_wq;struct delayed_work usb_ipa_w;};
struct f_gsi{struct gsi_data_port d_port;};
#define d_port_to_gsi(p) container_of(p,struct f_gsi,d_port)
/* FUNCTIONS */
int main(void){unsigned cases=0;struct f_gsi g={0};for(unsigned cycle=0;cycle<100;cycle++){reset_event_queue(&g.d_port);for(int i=1;i<MAXQUEUELEN;i++)post_event(&g.d_port,i);u8 tail=g.d_port.evt_q.tail;for(int i=0;i<300;i++)post_event(&g.d_port,255);assert(g.d_port.evt_q.tail==tail);for(int i=1;i<MAXQUEUELEN;i++){assert(peek_event(&g.d_port)==i);assert(read_event(&g.d_port)==i);}assert(read_event(&g.d_port)==EVT_NONE);cases++;}u8 message[65]={0};assert(!gsi_rndis_message_valid(NULL,0));for(size_t size=0;size<8;size++)assert(!gsi_rndis_message_valid(message,size));for(u32 type=2;type<=8;type++){if(type==7)continue;size_t minimum=(type==2?24:(type==4||type==5)?28:12);put_unaligned_le32(type,message+1);for(u32 len=0;len<=64;len++){put_unaligned_le32(len,message+5);bool valid=gsi_rndis_message_valid(message+1,minimum);assert(valid==(len==minimum));cases++;}}
put_unaligned_le32(RNDIS_MSG_SET,message);put_unaligned_le32(32,message+4);put_unaligned_le32(RNDIS_OID_GEN_CURRENT_PACKET_FILTER,message+12);for(u32 len=0;len<=40;len++)for(u32 off=0;off<=40;off++){put_unaligned_le32(len,message+16);put_unaligned_le32(off,message+20);assert(gsi_rndis_message_valid(message,32)==(len>=4&&off>=20&&off<=24&&len<=24-off));cases++;}put_unaligned_le32(UINT32_MAX,message+20);assert(!gsi_rndis_message_valid(message,32));printf("{\"queue_and_rndis_boundary_cases\":%u,\"queue_overflow_preserves_127_events\":true,\"unaligned_headers_and_wraps\":true}\n",cases);}
