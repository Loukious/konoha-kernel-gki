#include "rmnet_mock.h"
#define LINUX_VERSION_CODE 0x61200
#define KERNEL_VERSION(a,b,c) ((a)*65536+(b)*256+(c))
#define MAX_SKB_FRAGS 17
#define RMNET_MAP_DEAGGR_SPACING 64
#define RMNET_MAP_DEAGGR_HEADROOM 32
#define RMNET_FLAGS_INGRESS_MAP_CKSUMV4 1
#define RMNET_PRIV_FLAGS_INGRESS_MAP_CKSUMV5 2
#define RMNET_MAP_HEADER_TYPE_COALESCING 1
#define RMNET_MAP_V5_MAX_NLOS 6
struct rmnet_port { unsigned data_format; };
/* MAP TYPES */
/* FUNCTIONS */
static struct sk_buff *frame(unsigned n,bool nonlinear)
{
 struct sk_buff *s=alloc_skb(nonlinear?0:n,0);assert(s);s->len=n;
 if(nonlinear) { struct page *p=kzalloc(sizeof(*p),0);p->data=kzalloc(n?n:1,0);p->size=n;p->refs=1;s->data_len=n;s->shared.nr_frags=1;s->shared.frags[0]=(skb_frag_t){p,0,n}; }
 return s;
}
int main(void)
{
 struct rmnet_port port={0};unsigned cases=0;
 for(unsigned n=0;n<4;n++)for(unsigned nl=0;nl<2;nl++) { struct sk_buff *s=frame(n,nl);assert(!rmnet_map_deaggregate(s,&port));kfree_skb(s);cases++; }
 for(unsigned nl=0;nl<2;nl++)for(unsigned flags=0;flags<3;flags++)for(unsigned payload=1;payload<=128;payload++) {
  port.data_format=flags;unsigned n=4+payload+(flags==1?8:flags==2?4:0);struct sk_buff *s=frame(n,nl);unsigned char *data=rmnet_map_data_ptr(s);struct rmnet_map_header *h=(void*)data;h->pkt_len=htons(payload);s->priority=55;
  if(flags==2)((struct rmnet_map_v5_csum_header*)(data+4))->header_type=2;
  struct sk_buff *out=rmnet_map_deaggregate(s,&port);assert(out && out!=s && out->len==n && out->priority==55 && !s->len);
  assert(!memcmp(rmnet_map_data_ptr(out),data,n));kfree_skb(out);kfree_skb(s);assert(!live);cases++;
 }
 port.data_format=0;struct sk_buff *s=frame(8,false);((struct rmnet_map_header*)s->data)->pkt_len=htons(5);assert(!rmnet_map_deaggregate(s,&port));((struct rmnet_map_header*)s->data)->pkt_len=0;assert(!rmnet_map_deaggregate(s,&port));kfree_skb(s);
 s=frame(8,true);s->shared.frags[0].size=3;assert(!rmnet_map_deaggregate(s,&port));s->shared.frags[0].size=8;s->len=9;assert(!rmnet_map_deaggregate(s,&port));s->len=8;kfree_skb(s);
 port.data_format=2;s=frame(12,false);((struct rmnet_map_header*)s->data)->pkt_len=htons(4);((struct rmnet_map_v5_csum_header*)(s->data+4))->header_type=1;assert(rmnet_map_deaggregate(s,&port)==s && s->len==12);kfree_skb(s);
 assert(!skbs && !live);printf("{\"qmap_frame_cases\":%u,\"linear_and_single_fragment_paths\":true,\"short_and_invalid_layout_rejected\":true,\"native_v4_v5_and_coalescing_preserved\":true}\n",cases+5);
}
