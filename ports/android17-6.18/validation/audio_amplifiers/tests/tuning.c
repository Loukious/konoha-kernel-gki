#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define __packed __attribute__((packed))
#define __user
#define WRITE_ONCE(x,v) ((x)=(v))
#define READ_ONCE(x) (x)
#define pr_info(...) ((void)0)
struct mutex {int depth;};
typedef int wait_queue_head_t;
static void mutex_lock(struct mutex *m) {assert(!m->depth);m->depth++;}
static void mutex_unlock(struct mutex *m) {assert(m->depth==1);m->depth--;}
static int wakeups,reads,fail_read,fail_write,mutate;
static void wake_up_interruptible(wait_queue_head_t *q) {(void)q;wakeups++;}
/* PRODUCTION_HEADER */
static int copy_from_user(void *dst,const void *src,size_t size) {
 if (++reads==fail_read)return 1;
 memcpy(dst,src,size);
 if(mutate&&reads==1)((dev_comm_data_t *)src)->payload_size=UINT32_MAX;
 return 0;
}
static int copy_to_user(void *dst,const void *src,size_t size) {
 if(fail_write)return 1;memcpy(dst,src,size);return 0;
}
/* PRODUCTION_FUNCTIONS */
static unsigned cases;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);abort();}cases++;}while(0)
int main(void) {
 sipa_sync_t q={0};unsigned char msg[DATA_MAX_LEN]={0},out[DATA_MAX_LEN]={0};
 dev_comm_data_t *hdr=(void *)msg;hdr->param_id=123;
 myCircularQueueCreate(&q);CHECK(q.head==0&&q.tail==0&&q.k==MAX_SIZE);
 CHECK(myCircularQueueDeQueue(out,&q)==-EAGAIN);
 CHECK(myCircularQueueEnQueue(&q,msg,sizeof(*hdr)-1)==-EMSGSIZE);
 CHECK(myCircularQueueEnQueue(&q,msg,DATA_MAX_LEN+1)==-EMSGSIZE);
 hdr->payload_size=DATA_MAX_LEN-sizeof(*hdr);memset(hdr->payload,0x5a,hdr->payload_size);
 CHECK(sipa_tuning_write(msg,&q)==0);CHECK(wakeups==1);
 CHECK(myCircularQueueDeQueue(out,&q)==0);CHECK(memcmp(msg,out,sizeof(msg))==0);
 hdr->payload_size=UINT32_MAX;CHECK(sipa_tuning_write(msg,&q)==-EMSGSIZE);
 hdr->payload_size=1;reads=0;fail_read=1;CHECK(sipa_tuning_write(msg,&q)==-EFAULT);
 reads=0;fail_read=2;CHECK(sipa_tuning_write(msg,&q)==-EFAULT);CHECK(q.head==q.tail);
 reads=0;fail_read=0;mutate=1;hdr->payload_size=1;CHECK(sipa_tuning_write(msg,&q)==0);
 mutate=0;CHECK(myCircularQueueDeQueue(out,&q)==0);CHECK(((dev_comm_data_t*)out)->payload_size==1);
 hdr->payload_size=0;
 for(int i=0;i<MAX_SIZE-1;i++) {hdr->param_id=i;CHECK(sipa_tuning_write(msg,&q)==0);}
 CHECK(sipa_tuning_write(msg,&q)==-ENOSPC);
 fail_write=1;CHECK(myCircularQueueDeQueue(out,&q)==-EFAULT);fail_write=0;
 for(int i=0;i<MAX_SIZE-1;i++){CHECK(myCircularQueueDeQueue(out,&q)==0);CHECK(((dev_comm_data_t*)out)->param_id==(unsigned)i);}
 CHECK(myCircularQueueDeQueue(out,&q)==-EAGAIN);
 for(int i=0;i<MAX_SIZE*3;i++){hdr->param_id=i;CHECK(sipa_tuning_write(msg,&q)==0);CHECK(myCircularQueueDeQueue(out,&q)==0);CHECK(((dev_comm_data_t*)out)->param_id==(unsigned)i);}
 CHECK(sipa_tuning_write(msg,&q)==0);myCircularQueueCreate(&q);CHECK(myCircularQueueDeQueue(out,&q)==-EAGAIN);
 printf("PASS %u tuning cases\n",cases);return 0;
}
