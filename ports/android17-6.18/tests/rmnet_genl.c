#include "rmnet_mock.h"
#define RMNET_GENL_SUCCESS 0
#define RMNET_GENL_FAILURE -1
#define RMNET_CORE_GENL_CMD_PID_BPS_REQ 1
#define RMNET_CORE_GENL_ATTR_PID_BPS 1
struct genl_info { unsigned snd_seq,snd_portid; };
struct rmnet_core_pid_bps_resp { unsigned char payload[128]; };
static int rmnet_core_genl_family,rmnet_core_userspace_connected,fail_put,fail_attr,fail_unicast;
static size_t requested;
static size_t nla_total_size(size_t n) { return n+4; }
static struct sk_buff *genlmsg_new(size_t n,int flags) { requested=n;return alloc_skb(n+16,flags); }
static void *genlmsg_put(struct sk_buff *s,unsigned port,unsigned seq,void *f,unsigned flags,unsigned cmd) { return fail_put?NULL:s; }
static int nla_put(struct sk_buff *s,unsigned type,size_t n,void *p) { assert(requested==nla_total_size(n));return fail_attr?-EMSGSIZE:0; }
static void genlmsg_end(struct sk_buff *s,void *p) { s->ended=true; }
#define genl_info_net(i) (i)
static int genlmsg_unicast(void *net,struct sk_buff *s,unsigned port) { assert(s->ended);kfree_skb(s);return fail_unicast?-EPIPE:0; }
/* FUNCTIONS */
int main(void)
{
 struct genl_info info={0};struct rmnet_core_pid_bps_resp resp={0};
 assert(rmnet_core_genl_send_resp(NULL,&resp)==-1 && !skbs);assert(rmnet_core_genl_send_resp(&info,NULL)==-1);
 alloc_fault=alloc_step+1;assert(rmnet_core_genl_send_resp(&info,&resp)==-1 && !live);alloc_fault=0;
 fail_put=1;assert(rmnet_core_genl_send_resp(&info,&resp)==-1 && !live);fail_put=0;
 fail_attr=1;assert(rmnet_core_genl_send_resp(&info,&resp)==-1 && !live);fail_attr=0;
 fail_unicast=1;assert(rmnet_core_genl_send_resp(&info,&resp)==-1 && !live);fail_unicast=0;
 assert(!rmnet_core_genl_send_resp(&info,&resp) && !live && !skbs);
 puts("{\"genl_ownership_failures\":6,\"nla_header_space_reserved\":true,\"unicast_consumes_skb_on_error\":true}");
}
