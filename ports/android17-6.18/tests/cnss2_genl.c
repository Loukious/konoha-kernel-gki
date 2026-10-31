#include "cnss2_mock.h"
/* GENL CONSTANTS */
static u8 *original;static u32 size,expected_offset,expected_segment;static unsigned attempts,sleeps,failures;
static int cnss_genl_send_data(u8 type,char *name,u32 total,u32 segment,u8 end,u32 length,u8 *data)
{
 attempts++;assert(total==size && segment==expected_segment && data==original+expected_offset);
 assert(length && length<=CNSS_GENL_DATA_LEN_MAX && expected_offset+length<=size);
 assert(end==(expected_offset+length==size));
 if(failures){failures--;return -EAGAIN;}
 expected_offset+=length;expected_segment++;return 0;
}
static void msleep(unsigned ms) { assert(ms==CNSS_GENL_SEND_RETRY_DELAY);sleeps++; }
/* FUNCTIONS */
int main(void)
{
 unsigned cases=0;
 for(unsigned n=0;n<4*CNSS_GENL_DATA_LEN_MAX+5;n++) {
  size=n;original=malloc(n?n:1);assert(original);expected_offset=expected_segment=attempts=0;
  assert(!cnss_genl_send_msg(original,1,NULL,n));assert(expected_offset==n);free(original);cases++;
 }
 size=2*CNSS_GENL_DATA_LEN_MAX+1;original=malloc(size);expected_offset=expected_segment=attempts=0;failures=3;
 assert(!cnss_genl_send_msg(original,2,"capture",size));assert(attempts==6 && sleeps==3);free(original);
 size=1;original=malloc(size);expected_offset=expected_segment=attempts=0;failures=10;
 assert(cnss_genl_send_msg(original,1,NULL,1)==-EAGAIN && !expected_offset && attempts==10);free(original);
 assert(cnss_genl_send_msg(NULL,1,NULL,1)==-EINVAL);assert(!cnss_genl_send_msg(NULL,1,NULL,0));
 printf("{\"native_fragmentation_cases\":%u,\"retry_preserves_segment_identity\":true,\"terminal_error_propagated\":true,\"null_payload_rejected\":true}\n",cases);
}
