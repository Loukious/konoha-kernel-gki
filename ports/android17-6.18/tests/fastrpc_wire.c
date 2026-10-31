#include "fastrpc_services.h"
#include "wire_types.inc"
struct fastrpc_invoke_ctx {u64 ctxid;};
struct fastrpc_channel_ctx {int domain_id;spinlock_t lock;int ctx_idr;};
static struct fastrpc_invoke_ctx known={.ctxid=0x12340040};
static int signals,notifications,completions,logs;static bool present;
#define trace_fastrpc_transport_response(...) ((void)0)
static void fastrpc_handle_signal_rpmsg(u64 msg,struct fastrpc_channel_ctx *c) {(void)msg;(void)c;signals++;}
static void fastrpc_notif_find_process(int d,struct fastrpc_channel_ctx *c,struct dsp_notif_rsp *n) {(void)d;(void)c;assert(n->type==STATUS_RESPONSE);notifications++;}
static void fastrpc_update_rxmsg_buf(struct fastrpc_channel_ctx *c,u64 ctx,int ret,u32 f,u32 e,u32 v,s64 t) {(void)c;(void)ctx;(void)ret;(void)f;(void)e;(void)v;(void)t;logs++;}
static s64 get_timestamp_in_ns(void){return 0;}
static void *idr_find(int *p,unsigned long id){(void)p;return present && id==1?&known:NULL;}
static void fastrpc_notify_user_ctx(struct fastrpc_invoke_ctx *c,int ret,u32 flags,u32 early){assert(c==&known);(void)ret;(void)flags;(void)early;completions++;}
#include "wire.inc"
int main(void) {
 struct fastrpc_channel_ctx c={0};spin_lock_init(&c.lock);unsigned cases=0;
 for(int off=0;off<8;off++)for(int len=-2;len<=64;len++)for(int kind=0;kind<5;kind++) {
  size_t n=len>0?len:0;unsigned char *raw=calloc(1,n+off+1),*p=raw+off;
  union {struct fastrpc_invoke_rspv2 r;struct dsp_notif_rsp n;} v={0};
  v.r.ctx=known.ctxid;v.r.version=FASTRPC_RSP_VERSION2;
  if(kind==1){v.n.ctx=FASTRPC_NOTIF_CTX_RESERVED;v.n.type=STATUS_RESPONSE;}
  if(kind==2){v.n.ctx=FASTRPC_NOTIF_CTX_RESERVED;v.n.type=0;}
  if(kind==3)v.r.version=999;
  if(kind==4)v.r.ctx++;
  memcpy(p,&v,n<sizeof(v)?n:sizeof(v));signals=notifications=completions=logs=0;present=true;
  int r=fastrpc_handle_rpc_response(&c,p,len);
  if(len<8)assert(r==-EINVAL && !signals && !notifications && !logs);
  else if(len==8)assert(!r && signals==1 && !logs);
  else if(kind==1 || kind==2){if((size_t)len<sizeof(struct dsp_notif_rsp))assert(r==-EINVAL);else if(kind==1)assert(!r && notifications==1);else assert(r==-ENOENT);assert(!logs);}
  else if((size_t)len<sizeof(struct fastrpc_invoke_rsp))assert(r==-EINVAL && !logs);
  else if((size_t)len>=sizeof(struct fastrpc_invoke_rspv2) && kind==3)assert(r==-EINVAL && !logs);
  else assert(!r && logs==1 && completions==1);
  free(raw);cases++;
 }
 assert(fastrpc_handle_rpc_response(NULL,NULL,0)==-EINVAL);
 struct fastrpc_invoke_rspv2 r={.ctx=known.ctxid,.version=2};present=false;completions=0;assert(!fastrpc_handle_rpc_response(&c,&r,sizeof(r)) && !completions);
 present=true;r.ctx+=0x10000;assert(!fastrpc_handle_rpc_response(&c,&r,sizeof(r)) && !completions);
 printf("{\"wire_length_alignment_cases\":%u,\"missing_and_stale_contexts\":2}\n",cases);
}
