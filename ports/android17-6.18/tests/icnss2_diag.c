#include "icnss2_mock.h"
#define QMI_WLFW_MAX_DATA_SIZE_V01 6144
#define QMI_RESULT_SUCCESS_V01 0
#define QMI_WLFW_ATHDIAG_READ_REQ_V01 1
#define WLFW_ATHDIAG_READ_REQ_MSG_V01_MAX_MSG_LEN 100
#define QMI_WLFW_ATHDIAG_WRITE_REQ_V01 2
#define WLFW_ATHDIAG_WRITE_REQ_MSG_V01_MAX_MSG_LEN 100
struct qmi_txn{void*resp;};
struct icnss_priv{int qmi;unsigned long state;struct{int qmi_timeout;}ctrl_params;};
struct wlfw_athdiag_read_req_msg_v01{u32 offset,mem_type,data_len;};
struct wlfw_athdiag_read_resp_msg_v01{struct{int result,error;}resp;u8 data_valid;u32 data_len;u8 data[6144];};
struct wlfw_athdiag_write_req_msg_v01{u32 offset,mem_type,data_len;u8 data[6144];};
struct wlfw_athdiag_write_resp_msg_v01{struct{int result,error;}resp;};
static int wlfw_athdiag_read_resp_msg_v01_ei[1],wlfw_athdiag_read_req_msg_v01_ei[1],wlfw_athdiag_write_resp_msg_v01_ei[1],wlfw_athdiag_write_req_msg_v01_ei[1];
static unsigned response_len;static bool valid=true;static int transaction_failure,send_failure,wait_failure,sends,cancels;static u32 request_len;
static int qmi_txn_init(void*q,struct qmi_txn*t,void*e,void*r){t->resp=r;return transaction_failure?-EIO:0;}
static int qmi_send_request(void*q,void*a,struct qmi_txn*t,int id,int len,void*e,void*request){sends++;if(send_failure)return -EIO;if(id==1){struct wlfw_athdiag_read_resp_msg_v01*r=t->resp;struct wlfw_athdiag_read_req_msg_v01*p=request;request_len=p->data_len;r->data_valid=valid;r->data_len=response_len;memset(r->data,0xa5,sizeof(r->data));}else{struct wlfw_athdiag_write_req_msg_v01*p=request;assert(p->data_len<=sizeof(p->data));for(unsigned i=0;i<p->data_len;i++)assert(p->data[i]==0x7a);}return 0;}
static int qmi_txn_wait(struct qmi_txn*t,int timeout){return wait_failure?-ETIMEDOUT:0;}
static void qmi_txn_cancel(struct qmi_txn*t){cancels++;}
/* FUNCTIONS */
int main(void){struct icnss_priv p={0};unsigned cases=0;for(unsigned len=1;len<=6144;len=len==1?32:len==32?6144:6145){u8*data=malloc(len);unsigned responses[]={0,len-1,len,6144,6145};for(unsigned r=0;r<5;r++){unsigned response=responses[r];response_len=response;int ret=wlfw_athdiag_read_send_sync_msg(&p,0,0,len,data);assert(ret==(response<len||response>6144?-EINVAL:0));if(!ret)for(unsigned i=0;i<len;i++)assert(data[i]==0xa5);assert(!live);cases++;if(response==6145)break;}memset(data,0x7a,len);assert(!wlfw_athdiag_write_send_sync_msg(&p,0,0,len,data));free(data);cases++;}u8 byte=0;assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,0,&byte)==-EINVAL);assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,6145,&byte)==-EINVAL);assert(wlfw_athdiag_write_send_sync_msg(&p,0,0,6145,&byte)==-EINVAL);assert(wlfw_athdiag_write_send_sync_msg(&p,0,0,1,NULL)==-EINVAL);
for(int i=1;i<=2;i++){alloc_fault=alloc_step+i;assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,1,&byte)==-ENOMEM&&!live);alloc_fault=0;cases++;}transaction_failure=1;assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,1,&byte)==-EIO&&!live);transaction_failure=0;send_failure=1;assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,1,&byte)==-EIO&&!live&&cancels==1);send_failure=0;wait_failure=1;assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,1,&byte)==-ETIMEDOUT&&!live);wait_failure=0;valid=false;response_len=1;assert(wlfw_athdiag_read_send_sync_msg(&p,0,0,1,&byte)==-EINVAL&&!live);printf("{\"diagnostic_cases\":%u,\"response_copy_bounded_by_request\":true,\"request_bounds_and_failures\":true}\n",cases+8);}
