#include "ipa_mock.h"
#define IPADBG_LOW(...) ((void)0)
#define IPA_STATS_INC_CNT(x) ((x)++)
#define TASK_INTERRUPTIBLE 1
#define MAX_SCHEDULE_TIMEOUT LONG_MAX
#define current NULL
/* MESSAGE TYPE */
struct ipa3_push_msg { struct list_head link; struct ipa_msg_meta meta; void *buff; void (*callback)(void *, u32, u32); };
struct ipa3_context { struct mutex msg_lock; struct list_head msg_list; wait_queue_head_t msg_waitq; struct { unsigned msg_r[256]; } stats; };
static struct ipa3_context context, *ipa3_ctx=&context;
struct file { unsigned f_flags; };
static unsigned waiters, waits, releases, copied, copy_calls, copy_fault;
static bool interrupted;
static char *output_base;
static size_t output_size;
#define DEFINE_WAIT_FUNC(n,f) int n=0
#define add_wait_queue(q,w) ((void)(q),(void)(w),waiters++)
#define remove_wait_queue(q,w) ((void)(q),(void)(w),assert(waiters==1),waiters--)
#define signal_pending(t) interrupted
static size_t copy_to_user(char *dest, const void *source, size_t size)
{
 assert(dest>=output_base && (size_t)(dest-output_base)<=output_size);
 assert(size<=output_size-(size_t)(dest-output_base));
 if (++copy_calls==copy_fault) return size ? size : 1;
 memcpy(dest,source,size);copied+=size;return 0;
}
static void wait_woken(int *wait, int task, long timeout) { waits++;interrupted=true; }
static void release_payload(void *buffer,u32 size,u32 type) { assert(buffer && type==3);releases++;kfree(buffer); }
static struct ipa3_push_msg *enqueue(unsigned len)
{
 struct ipa3_push_msg *msg=kzalloc(sizeof(*msg),0);assert(msg);
 msg->meta=(struct ipa_msg_meta){.msg_type=3,.msg_len=len};
 if(len) { msg->buff=kzalloc(len,0);assert(msg->buff);memset(msg->buff,0x6b,len);msg->callback=release_payload; }
 list_add_tail(&msg->link,&context.msg_list);return msg;
}
/* FUNCTIONS */
static ssize_t read_message(unsigned len, unsigned fault, bool nonblock)
{
 struct file f={.f_flags=nonblock?O_NONBLOCK:0};loff_t pos=0;
 output_base=malloc(len?len:1);assert(output_base);output_size=len;
 memset(output_base,0xa5,len?len:1);copy_calls=copied=0;copy_fault=fault;
 ssize_t result=ipa3_read(&f,output_base,len,&pos);
 assert(!waiters && !context.msg_lock.held);
 if(result>0) { assert((unsigned)result==copied);assert(output_base[0]==3); }
 free(output_base);output_base=NULL;return result;
}
int main(void)
{
 INIT_LIST_HEAD(&context.msg_list);unsigned cases=0;
 for(unsigned payload=0;payload<=1024;payload++) {
  unsigned total=sizeof(struct ipa_msg_meta)+payload;
  enqueue(payload);unsigned before=releases;
  assert(read_message(0,0,true)==0 && !list_empty(&context.msg_list));cases++;
  for(unsigned shortlen=1;shortlen<total;shortlen++) {
   assert(read_message(shortlen,0,true)==-EMSGSIZE);
   assert(!list_empty(&context.msg_list) && releases==before && !copied);cases++;
  }
  assert(read_message(total,0,true)==total && list_empty(&context.msg_list) && !live);cases++;
 }
 for(unsigned fault=1;fault<=2;fault++) {
  enqueue(65535);unsigned before=releases;
  assert(read_message(65539,fault,true)==-EFAULT);
  assert(list_empty(&context.msg_list) && releases==before+1 && !live);cases++;
 }
 assert(read_message(64,0,true)==-EAGAIN);
 interrupted=true;assert(read_message(64,0,false)==-EINTR);interrupted=false;
 assert(read_message(64,0,false)==-EINTR && waits==1);
 enqueue(12);enqueue(24);assert(read_message(28,0,false)==16 && !list_empty(&context.msg_list));
 assert(read_message(28,0,true)==28 && list_empty(&context.msg_list) && !live);
 printf("{\"message_length_and_copy_cases\":%u,\"short_read_keeps_queued_message\":true,\"copy_fault_releases_payload\":true,\"blocking_and_nonblocking_reads\":true,\"native_metadata_size\":%zu}\n",cases,sizeof(struct ipa_msg_meta));
}
