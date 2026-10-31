#include "icnss2_mock.h"
#define ICNSS_MBOX_MSG_MAX_LEN 64
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define BIT_ULL(n) (1ULL<<(n))
#define unlikely(x) (x)
#define PCIE_LOCAL_REG_APPS_TO_Q6 0x3224
#define PCIE_LOCAL_REG_WCSS_IE_IRQ 0x3228
#define SMMU_CB_MAX 3
#define __arch_counter_get_cntvct() 42
#define CNSS_MAX_FEATURE_V01 64
enum cnss_feature_v01{FEATURE=0};
enum icnss_vreg_param{ICNSS_VREG_VOLTAGE,ICNSS_VREG_MODE,ICNSS_VREG_ENABLE};
enum icnss_tcs_seq{ICNSS_TCS_UP_SEQ,ICNSS_TCS_DOWN_SEQ,ICNSS_TCS_ALL_SEQ};
enum icnss_db_msg{MSG=1};enum icnss_smmu_fault_time{STAMP=0};
struct qmp_pkt{size_t size;void*data;};
struct icnss_priv{bool use_direct_qmp;void*qmp,*mbox_chan;int pdc_init_table_len;const char **pdc_init_table;unsigned long device_id;void *mem_base_va;atomic_t suspended;u64 smmu_fault_timestamp[SMMU_CB_MAX],feature_list;};
static int sends,send_failure,writes;static char frame[64];static uintptr_t write_addr[2];static u32 write_val[2];
static ssize_t strscpy(char*d,const char*s,size_t n){size_t len=strlen(s);if(len>=n){memcpy(d,s,n-1);d[n-1]=0;return -E2BIG;}memcpy(d,s,len+1);return len;}
static int send_frame(const char*p,size_t n){assert(n==sizeof(frame));size_t len=strlen(p);for(size_t i=len+1;i<n;i++)assert(p[i]==0);memcpy(frame,p,n);sends++;return send_failure?-EIO:7;}
static int qmp_send(void*q,const char*p,size_t n){return send_frame(p,n);}
static int mbox_send_message(void*q,struct qmp_pkt*p){return send_frame(p->data,p->size);}
static void iowrite32(u32 v,void*a){assert(writes<2);write_addr[writes]=(uintptr_t)a;write_val[writes++]=v;}
/* FUNCTIONS */
int main(void){struct icnss_priv p={0};unsigned cases=0;for(int q=0;q<2;q++){p.use_direct_qmp=q;for(int param=-2;param<=4;param++)for(int seq=-2;seq<=4;seq++){int before=sends;int ret=icnss_aop_set_vreg_param(&p,"vdd",param,seq,1200);assert(ret==(param>=0&&param<3&&seq>=0&&seq<3?0:-EINVAL));assert(sends-before==(ret==0));cases++;}char long_name[100];memset(long_name,'x',99);long_name[99]=0;int before=sends;assert(icnss_aop_set_vreg_param(&p,long_name,0,0,1)==-E2BIG && sends==before);assert(icnss_aop_set_vreg_param(&p,NULL,0,0,1)==-EINVAL);const char *table[]={"{x}","{y}"};p.pdc_init_table=table;p.pdc_init_table_len=2;assert(!icnss_aop_pdc_reconfig(&p));assert(!strcmp(frame,"{y}"));send_failure=1;before=sends;assert(icnss_aop_pdc_reconfig(&p)==-EIO && sends==before+1);send_failure=0;table[0]=long_name;before=sends;assert(icnss_aop_pdc_reconfig(&p)==-E2BIG&&sends==before);table[0]=NULL;assert(icnss_aop_pdc_reconfig(&p)==-E2BIG);}
assert(icnss_ring_doorbell(&p,MSG)==-ENODEV&&!writes);p.mem_base_va=(void*)0x100000;p.suspended=1;assert(icnss_ring_doorbell(&p,MSG)==-EINVAL&&!writes);p.suspended=0;assert(!icnss_ring_doorbell(&p,MSG)&&writes==2);assert(write_addr[0]==0x103224&&write_val[0]==MSG&&write_addr[1]==0x103228&&write_val[1]==1);
for(int i=-2;i<6;i++){icnss_record_smmu_fault_timestamp(&p,i);cases++;}for(int i=0;i<3;i++)assert(p.smmu_fault_timestamp[i]==42);
for(int i=-2;i<66;i++){int ret=icnss_set_feature_list(&p,i);assert(ret==(i>=0&&i<64?0:-EINVAL));cases++;}assert(p.feature_list==UINT64_MAX);assert(icnss_get_feature_list(&p,NULL)==-EINVAL);u64 features=0;assert(!icnss_get_feature_list(&p,&features)&&features==UINT64_MAX);
printf("{\"protocol_cases\":%u,\"both_aop_transports_zero_padded\":true,\"aop_bounds_and_error_propagation\":true,\"doorbell_mapping_suspend_and_register_order\":true,\"feature_and_timestamp_bounds\":true}\n",cases);}
