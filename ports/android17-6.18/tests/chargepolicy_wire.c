#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef int64_t s64;
#define U16_MAX 65535
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define READ_ONCE(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define swab32 __builtin_bswap32
#define mca_log_info(...) ((void)0)
#define ADSP_VDM_REQ_INIT 200
#define ADSP_VDM_REQ_NAK 255
#define PROTOCOL_PD_MAX_PDO_NUMS 7
#define ADAPTER_CAP_MAX_NR 7
/* HEADER_DEFINITIONS */
/* ENUM_DEFINITIONS */
static int last_prop, last_len, writes, reads, fault, read_state;
static unsigned char last_payload[128];
static struct pd_pdo source_pdos[7];
static unsigned int g_cur_port;
static int mca_adsp_glink_write_prop(int prop, const void *p, size_t len) { writes++;last_prop=prop;last_len=len;assert(len<=sizeof(last_payload));memcpy(last_payload,p,len);return fault; }
static int mca_adsp_glink_read_prop(int prop, void *p, size_t len) { reads++;last_prop=prop;last_len=len;if(fault)return fault;if(prop==PD_PROP_CURRENT_STATE)memcpy(p,&read_state,len);else if(prop==PD_PROP_PDOS)memcpy(p,source_pdos,len);else memset(p,0,len);return 0; }
static int protocol_class_pd_get_pdos(unsigned int port, struct pd_pdo *pdos, int n) { (void)port;assert(n==7);if(fault)return fault;memcpy(pdos,source_pdos,sizeof(source_pdos));return 0; }
static int strscpy(char *out, const char *in, size_t len) { size_t n=strlen(in);if(!len)return -E2BIG;size_t copy=n<len?n:len-1;memcpy(out,in,copy);out[copy]=0;return n<len?(int)n:-E2BIG; }
/* STAGED_DEFINITIONS */
int main(void) {
 unsigned char unaligned[20];for(int i=0;i<20;i++)unaligned[i]=i;unsigned char original[20];memcpy(original,unaligned,20);
 for(unsigned int n=0;n<=20;n++) {int before=writes;int r=adsp_pd_protocol_request_vdm_cmd(USBPD_UVDM_AUTHENTICATION,(u32 *)(unaligned+1),n,NULL);assert((r==0)==(n==16));assert(writes-before==(n==16));assert(!memcmp(original,unaligned,20));}
 for(int i=0;i<4;i++)for(int j=0;j<4;j++)assert(last_payload[i*4+j]==unaligned[1+i*4+3-j]);
 fault=-EIO;assert(adsp_pd_protocol_request_vdm_cmd(USBPD_UVDM_AUTHENTICATION,(u32 *)(unaligned+1),16,NULL)==-EIO);fault=0;
 assert(adsp_pd_protocol_request_vdm_cmd((enum uvdm_state)123,(u32 *)unaligned,4,NULL)==-EINVAL);
 for(int n=-1;n<=8;n++) {int before=reads;struct pd_pdo pdos[7];int r=adsp_pd_protocol_get_pdos(pdos,n,NULL);assert((r==0)==(n>0&&n<=7));assert(reads-before==(n>0&&n<=7));}
 unsigned int power=456;int before=reads;assert(adsp_pd_protocol_get_pps_max_power(&power,NULL)==0&&reads==before+1&&last_prop==PD_PROP_PPS_MAX_POWER);
 assert(adsp_pd_protocol_select_pps_pdo(-1,1000,NULL)==-EINVAL);assert(adsp_pd_protocol_select_pps_pdo(9000,INT_MAX,NULL)==-EINVAL);assert(adsp_pd_protocol_select_pps_pdo(INT_MAX,1000,NULL)==-EINVAL);assert(adsp_pd_protocol_select_pps_pdo(9000,3000,NULL)==0);u32 packed;memcpy(&packed,last_payload,4);assert(packed==(60|(450U<<16)));
 for(int state=-1;state<5;state++)for(int len=0;len<16;len++) {read_state=state;char *out=malloc(len?len:1);memset(out,0xaa,len?len:1);int r=adsp_pd_protocol_get_current_state(out,len,NULL);if(len>0)assert(out[len-1]==0||r==0);if(!len)assert(r==-EINVAL);free(out);}
 struct adapter_power_cap_info caps;memset(&caps,0,sizeof(caps));
 assert(protocol_class_pd_collect_caps(&caps,false)==-ENODATA);
 source_pdos[0]=(struct pd_pdo){.min_volt=5000,.max_volt=5000,.max_current=3000};
 source_pdos[1]=(struct pd_pdo){.min_volt=3300,.max_volt=11000,.max_current=5000};
 assert(!protocol_class_pd_collect_caps(&caps,false)&&caps.nums==1&&caps.cap[0].max_power==15000);
 assert(!protocol_class_pd_collect_caps(&caps,true)&&caps.nums==1&&caps.cap[0].max_power==55000);
 int curr;assert(!protocol_class_pps_adapter_get_adv_current(&caps,10000,&curr)&&curr==5000);assert(protocol_class_pps_adapter_get_adv_current(&caps,12000,&curr)==-ENODATA&&curr==0);
 source_pdos[0].max_current=-1;assert(protocol_class_pd_collect_caps(&caps,false)==-EPROTO);source_pdos[0].max_current=INT_MAX;source_pdos[0].min_volt=source_pdos[0].max_volt=INT_MAX;assert(protocol_class_pd_collect_caps(&caps,false)==-EOVERFLOW);
 caps.nums=8;assert(protocol_class_pps_adapter_get_adv_current(&caps,10000,&curr)==-EINVAL);
 puts("{\"passed\":true,\"vdm_lengths_and_input_preservation\":true,\"pdo_bounds_and_overflow\":true,\"state_buffer_lengths\":80,\"firmware_errors\":true}");
}
