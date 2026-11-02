#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define GFP_KERNEL 0
#define aw_dev_info(...) ((void)0)
#define aw_dev_err(...) ((void)0)
#define aw_dev_dbg(...) ((void)0)
/* PRODUCTION_HEADER */
struct aw_device {int reg_num;};
struct aw882xx {struct aw_device *aw_pa;struct aw882xx_i2c_packet i2c_packet;void *dev,*i2c;};
static int allocations,alloc_fail,send_result=1000,send_count;
static void *kmalloc(size_t size,int flags){(void)flags;if(alloc_fail)return NULL;allocations++;return malloc(size);}
static void kfree(void *p){if(p){allocations--;free(p);}}
static int i2c_master_send(void *i2c,const char *data,int size){(void)i2c;assert(data[0]>=0);send_count++;return send_result==1000?size:send_result;}
/* PRODUCTION_FUNCTIONS */
static unsigned cases;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"failed line %d\n",__LINE__);abort();}cases++;}while(0)
int main(void) {
 struct aw_device dev={256};struct aw882xx aw={.aw_pa=&dev};int d[AWRW_HDR_MAX]={0,1,2,1,0};
 CHECK(aw882xx_awrw_data_check(&aw,d)==0);
 d[1]=2;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);d[1]=1;
 d[2]=1;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);d[2]=2;
 d[4]=-1;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);
 d[4]=256;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);
 d[4]=255;CHECK(aw882xx_awrw_data_check(&aw,d)==0);
 d[3]=2;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);
 d[4]=0;d[3]=0;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);
 d[3]=-1;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);
 d[3]=256;CHECK(aw882xx_awrw_data_check(&aw,d)==-EINVAL);
 d[3]=255;CHECK(aw882xx_awrw_data_check(&aw,d)==0);
 const char good[]="0x00 0x01 0x02 0x01 0x01 0x12 0x34";
 CHECK(aw882xx_awrw_parse_buf(&aw,good,strlen(good))==0);CHECK(allocations==0&&send_count==1);
 alloc_fail=1;CHECK(aw882xx_awrw_parse_buf(&aw,good,strlen(good))==-ENOMEM);alloc_fail=0;
 CHECK(aw882xx_awrw_parse_buf(&aw,good,24)==-EINVAL);
 const char bad[]="0x00 0x01 0x02 0x01 0x01 0x12 junk";
 CHECK(aw882xx_awrw_parse_buf(&aw,bad,strlen(bad))==-EINVAL);CHECK(allocations==0);
 send_result=1;CHECK(aw882xx_awrw_parse_buf(&aw,good,strlen(good))<0);CHECK(allocations==0);
 send_result=-EIO;CHECK(aw882xx_awrw_parse_buf(&aw,good,strlen(good))<0);CHECK(allocations==0);
 CHECK(aw882xx_awrw_parse_buf(&aw,"bad",3)==-EINVAL);
 const char read[]="0x01 0x01 0x02 0x01 0xff";
 CHECK(aw882xx_awrw_parse_buf(&aw,read,strlen(read))==0);
 CHECK(aw.i2c_packet.status==AWRW_I2C_ST_READ&&aw.i2c_packet.reg_addr==255);
 printf("PASS %u AW bounds cases\n",cases);return 0;
}
