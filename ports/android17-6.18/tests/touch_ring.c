#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define CMD_DATA_BUF_SIZE 256
#define COMMON_DATA_BUF_SIZE 10
#define EXPORT_SYMBOL_GPL(x)
#define LOG_DEBUG(...)
typedef int8_t s8;typedef int32_t s32;
enum common_data_cmd { SET_CUR_VALUE=0 };enum common_data_mode { TOUCH_MODE=1 };
typedef struct { s8 touch_id;uint8_t cmd;uint16_t mode,data_len;s32 data_buf[CMD_DATA_BUF_SIZE]; } common_data_t;
typedef struct {int common_data_buf_lock,common_data_buf_index;common_data_t common_data_buf[COMMON_DATA_BUF_SIZE];} xiaomi_touch_data_t;
static xiaomi_touch_data_t panel;
static xiaomi_touch_data_t *get_xiaomi_touch_data(int id){return id==0?&panel:NULL;}
#define mutex_lock(p) assert(!(*(p))++)
#define mutex_unlock(p) assert((*(p))--)
#define atomic_read(p) (*(p))
#define atomic_inc(p) (++*(p))
#define atomic_set(p,n) (*(p)=(n))
#define COMMON_DATA_NOTIFY 1
static unsigned notifications;
static void notify_xiaomi_touch(xiaomi_touch_data_t *p,int t){assert(p==&panel&&t==1);notifications++;}
/* FUNCTIONS */
int main(void){int data[CMD_DATA_BUF_SIZE];for(int i=0;i<CMD_DATA_BUF_SIZE;i++)data[i]=i+123;unsigned cases=0;
for(int len=-2;len<=CMD_DATA_BUF_SIZE+2;len++){int old=notifications,idx=panel.common_data_buf_index;memset(&panel.common_data_buf[idx],0xaa,sizeof(common_data_t));add_common_data_to_buf(0,SET_CUR_VALUE,TOUCH_MODE,len,data);bool valid=len>=0&&len<=CMD_DATA_BUF_SIZE;assert(notifications==old+valid);if(valid){common_data_t *p=&panel.common_data_buf[idx];assert(p->data_len==len);for(int i=0;i<len;i++)assert(p->data_buf[i]==data[i]);for(int i=len;i<CMD_DATA_BUF_SIZE;i++)assert(!p->data_buf[i]);assert(panel.common_data_buf_index==(idx+1)%COMMON_DATA_BUF_SIZE);}else assert(panel.common_data_buf_index==idx);cases++;}
int old=notifications;add_common_data_to_buf(0,0,1,1,NULL);add_common_data_to_buf(-1,0,1,1,data);add_common_data_to_buf(0,0,1,INT_MAX,data);assert(notifications==old);printf("{\"ring_boundary_cases\":%u,\"unused_words_zeroed\":true,\"negative_and_large_lengths_rejected\":true}\n",cases+3);}
