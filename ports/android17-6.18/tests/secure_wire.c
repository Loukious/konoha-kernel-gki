#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32; typedef uint64_t u64;
#define U32_MAX UINT32_MAX
#define MAX_ARGS 64
#define PAGE_SIZE 4096
#define INVOKE_MESSAGE_ALIGN_BYTES 8U
#define ALIGN(x,a) (((x)+((a)-1))&~((typeof(x))(a)-1))
#define PAGE_ALIGN(x) ALIGN((x),PAGE_SIZE)
#define align_offset(x) ALIGN((x),8U)
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,y) ((x)=(y))
#define OIC_FLAG_BUSY 1
#define SI_ARG_FLAGS_UADDR 1
#define SI_OBJECT_OP_RELEASE 65535U
#define SI_OT_CB_OBJECT 2
#define SI_OT_USER 1
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define KERN_INFO 0
#define DUMP_PREFIX_OFFSET 0
#define print_hex_dump(...) ((void)0)
struct si_buffer { union {void *addr;void *uaddr;}; size_t size; };
enum arg_type {SI_AT_END,SI_AT_IB,SI_AT_OB,SI_AT_IO,SI_AT_OO};
struct si_arg {enum arg_type type;char flags;union {struct si_buffer b;void *o;};};
union qtee_smcinvoke_msg_arg {struct {u32 offset,size;} b;u32 o;};
struct qtee_object_invoke {u32 cxt,op,counts;union qtee_smcinvoke_msg_arg args[];};
struct qtee_callback {u32 result,cxt,op,counts;union qtee_smcinvoke_msg_arg args[];};
struct si_object_invoke_ctx {unsigned flags,context_id;struct {struct si_buffer msg;} in,out;};
#define counts_num__bi_(x) (((x)>>0)&15U)
#define counts_num__bo_(x) (((x)>>4)&15U)
#define counts_num__oi_(x) (((x)>>8)&15U)
#define counts_num__oo_(x) (((x)>>12)&15U)
#define counts_idx__bi_(x) 0U
#define counts_idx__bo_(x) counts_num__bi_(x)
#define counts_idx__oi_(x) (counts_idx__bo_(x)+counts_num__bo_(x))
#define counts_idx__oo_(x) (counts_idx__oi_(x)+counts_num__oi_(x))
#define counts_total(x) (counts_idx__oo_(x)+counts_num__oo_(x))
#define FOR_ARGS(i,c,t) for(i=counts_idx##t(c);i<counts_idx##t(c)+counts_num##t(c);i++)
#define for_each_input_buffer(i,c) FOR_ARGS(i,c,__bi_)
#define for_each_output_buffer(i,c) FOR_ARGS(i,c,__bo_)
#define OFFSET_TO_BUFFER_ARGS(m,n) ALIGN(offsetof(typeof(*m),args)+(n)*sizeof((m)->args[0]),8U)
struct si_object {int refcount;};
static struct si_object objects[16];static bool present[16];static unsigned released;
static struct si_object *erase_si_object(u32 id) {unsigned n=id&0x7fffffffU;if(n>=16||!present[n])return NULL;present[n]=false;return &objects[n];}
static int si_object_type(u32 id) {return id&0x80000000U?SI_OT_CB_OBJECT:SI_OT_USER;}
static const char *si_object_name(struct si_object *o) {assert(o);return "object";}
static int kref_read(int *r) {return *r;}
static void put_si_object(struct si_object *o) {assert(o);assert(o->refcount==1);o->refcount=0;released++;}
static u32 global_version;
/* ASYNC STRUCTS */
/* FUNCTIONS */
int main(void) {
 unsigned cases=0;size_t length;u32 counts;struct si_arg args[MAX_ARGS+1]={0};
 assert(si_message_size(NULL,&length,&counts)==-EINVAL);
 for(unsigned type=1;type<=4;type++)for(unsigned n=0;n<=64;n++) {
  memset(args,0,sizeof(args));for(unsigned i=0;i<n;i++){args[i].type=type;args[i].b.addr=(void*)1;}
  assert((si_message_size(args,&length,&counts)==0)==(n<=15));cases++;
 }
 args[0]=(struct si_arg){.type=SI_AT_IB,.b={.addr=(void*)1,.size=SIZE_MAX}};args[1].type=SI_AT_END;
 assert(si_message_size(args,&length,&counts)==-E2BIG);args[0].b.size=0;args[0].flags=2;assert(si_message_size(args,&length,&counts)==-EINVAL);
 unsigned char *storage=calloc(1,4096);assert(storage);struct qtee_callback *cb=(void*)storage;
 struct si_object_invoke_ctx ctx={.out.msg={.addr=cb,.size=4096}};
 for(unsigned c=0;c<65536;c+=37)for(unsigned len=0;len<=256;len+=8) {
  cb->counts=c;ctx.out.msg.size=len;for(unsigned j=0;j<counts_total(c);j++){cb->args[j].b.offset=ALIGN(16+counts_total(c)*8,8U);cb->args[j].b.size=0;}assert(si_callback_valid(&ctx,cb)==(len>=16 && ALIGN(16+counts_total(c)*8,8U)<=len));cases++;
 }
 cb->counts=0x10000;ctx.out.msg.size=4096;assert(!si_callback_valid(&ctx,cb));cb->counts=0x11;
 for(unsigned off=0;off<4200;off+=7)for(unsigned sz=0;sz<4200;sz+=67) {
  cb->args[0].b.offset=off;cb->args[0].b.size=sz;cb->args[1].b.offset=4096;cb->args[1].b.size=0;
  assert(si_callback_valid(&ctx,cb)==(off>=32&&off<=4096&&sz<=4096-off));cases++;
 }
 ctx.flags=OIC_FLAG_BUSY;cb->args[0].b.offset=128;cb->args[0].b.size=13;cb->args[1].b.offset=256;cb->args[1].b.size=4;
 struct si_buffer b=async_si_buffer(&ctx);assert(b.addr==storage+264&&b.size==4096-264);
 cb->args[0].b.size=UINT32_MAX;b=async_si_buffer(&ctx);assert(!b.addr&&!b.size);
 ctx.flags=0;ctx.out.msg.size=4096;struct async_msg *msg=(void*)storage;
 for(unsigned sz=0;sz<128;sz++)for(unsigned n=0;n<48;n++) {
  memset(storage,0,4096);msg->op_release.count=n;released=0;
  size_t got=async_release_handler(&ctx,msg,sz);bool valid=sz>=12&&n<=(sz-12)/4;
  assert(got==(valid?12+n*4:0));assert(!released);cases++;
 }
 memset(storage,0,4096);msg->header.version=0x10002;msg->header.op=ASYNC_OP_RELEASE;msg->op_release.count=4;
 msg->op_release.obj[0]=0x80000003U;msg->op_release.obj[1]=0x80000003U;msg->op_release.obj[2]=0x80000099U;msg->op_release.obj[3]=3;
 present[3]=true;objects[3].refcount=1;__fetch__async_reqs(&ctx);assert(released==1&&global_version==0x10002);assert(!msg->header.version);
 msg->header.version=0x20000;msg->header.op=ASYNC_OP_RELEASE;msg->op_release.count=UINT32_MAX;__fetch__async_reqs(&ctx);assert(released==1);
 free(storage);printf("{\"protocol_bounds_cases\":%u,\"duplicate_missing_release_ids\":true,\"async_offset_and_version\":true}\n",cases);
}
