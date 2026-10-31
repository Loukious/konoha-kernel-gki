#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GFP_KERNEL 0
#define U16_MAX 65535
struct i2c_client { int unused; };
struct mock_aw { struct i2c_client *i2c; };
struct aw8697_container { int len;unsigned char data[]; };
struct aw86927_container { int len;unsigned char data[]; };
static int fail_alloc,live,send_result=-100000,send_calls,read_result,write_result,write_calls,last_value;
static void *kmalloc(size_t n,int flags){(void)flags;if(fail_alloc)return NULL;void *p=malloc(n);assert(p);live++;return p;}
static void *vmalloc(size_t n){return kmalloc(n,0);}
static void kfree(void *p){assert(live==1);live--;free(p);}
static int i2c_master_send(struct i2c_client *i,const unsigned char *data,unsigned int len){(void)i;assert(data[0]==23&&len>1);for(unsigned int n=1;n<len;n++)assert(data[n]==(unsigned char)(n-1));send_calls++;return send_result==-100000?(int)len:send_result;}
static int aw8697_i2c_read(struct mock_aw *a,unsigned char reg,unsigned char *value){(void)a;(void)reg;if(read_result<0)return read_result;*value=0xa5;return read_result;}
static int aw86927_i2c_read(struct mock_aw *a,unsigned char reg,unsigned char *value){return aw8697_i2c_read(a,reg,value);}
static int aw8697_i2c_write(struct mock_aw *a,unsigned char reg,unsigned char value){(void)a;(void)reg;write_calls++;last_value=value;return write_result;}
static int aw86927_i2c_write(struct mock_aw *a,unsigned char reg,unsigned char value){return aw8697_i2c_write(a,reg,value);}
/* SOURCE */
int main(void){
 unsigned char *data=malloc(U16_MAX);assert(data);for(int n=0;n<U16_MAX;n++)data[n]=n;struct i2c_client i={0};struct mock_aw a={&i};int cases=0;
 int (*writes[])(struct mock_aw *,unsigned char,unsigned char *,unsigned int)={aw8697_i2c_writes,aw86927_i2c_writes};int (*bits[])(struct mock_aw *,unsigned char,unsigned int,unsigned char)={aw8697_i2c_write_bits,aw86927_i2c_write_bits};
 for(int chip=0;chip<2;chip++){
  assert(writes[chip](&a,23,NULL,1)==-EINVAL&&writes[chip](&a,23,data,0)==-EINVAL&&writes[chip](&a,23,data,U16_MAX)==-EINVAL&&writes[chip](&a,23,data,UINT_MAX)==-EINVAL);fail_alloc=1;assert(writes[chip](&a,23,data,1)==-ENOMEM&&!send_calls&&!live);fail_alloc=0;
  for(int n=1;n<64;n++)for(int ret=-1;ret<=n+1;ret++){send_result=ret;int result=writes[chip](&a,23,data,n);assert(result==(ret<0?ret:ret==n+1?ret:-EIO)&&!live);cases++;}
  send_result=-100000;assert(writes[chip](&a,23,data,U16_MAX-1)==U16_MAX&&!live);read_result=-EIO;write_calls=0;assert(bits[chip](&a,23,0xf0,2)==-EIO&&!write_calls);read_result=0;write_result=-EIO;assert(bits[chip](&a,23,0xf0,2)==-EIO&&last_value==0xa2);write_result=7;assert(bits[chip](&a,23,0xf0,2)==7&&last_value==0xa2);send_calls=0;
 }
 assert(!aw8697_alloc_rtp(0)&&!aw86927_alloc_rtp(0)&&!aw8697_alloc_rtp((size_t)INT_MAX+1)&&!aw86927_alloc_rtp(SIZE_MAX));
 struct aw8697_container *x=aw8697_alloc_rtp(8192);assert(x);memset(x->data,0x55,8192);kfree(x);struct aw86927_container *y=aw86927_alloc_rtp(8192);assert(y);memset(y->data,0x55,8192);kfree(y);free(data);
 printf("{\"short_error_transfer_cases\":%d,\"allocation_failure_safe\":true,\"length_bounds_checked\":true,\"read_modify_write_errors_propagate\":true,\"rtp_allocation_includes_header\":true}\n",cases);
}
