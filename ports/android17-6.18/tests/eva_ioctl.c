#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#define __user
#define MAX_HFI_PKT_SIZE 490
#define dprintk(...) ((void)0)
typedef uint32_t u32;
struct eva_kmd_hfi_packet {u32 pkt_data[MAX_HFI_PKT_SIZE];unsigned long oob_buf;};
struct eva_kmd_hfi_synx_packet {u32 pkt_data[MAX_HFI_PKT_SIZE];unsigned long oob_buf;};
struct eva_kmd_arg {union {struct eva_kmd_hfi_packet hfi_pkt;struct eva_kmd_hfi_synx_packet hfi_synx_pkt;}data;};
struct cvp_hal_session_cmd_pkt {u32 size,packet_type,session_id;};struct cvp_hfi_msg_session_hdr {u32 size,packet_type,session_id;};
static int reads,fail_at;
#define get_user(v,p) ({int err= ++reads==fail_at?-EFAULT:0;if(!err)(v)=*(p);err;})
#define put_user(v,p) ({*(p)=(v);0;})
static int get_pkt_index(struct cvp_hal_session_cmd_pkt *p){return p->packet_type==1?0:-EINVAL;}
static unsigned get_msg_size(struct cvp_hfi_msg_session_hdr *p){return 24;}
static int _copy_fence_pkt_from_user(struct eva_kmd_arg *k,struct eva_kmd_arg *u){memcpy(k,u,sizeof(*k));return 0;}
#include "ioctl.inc"
int main(void){struct eva_kmd_arg u={0},k={0};struct cvp_hal_session_cmd_pkt hdr={0};unsigned cases=0;
 for(unsigned size=0;size<MAX_HFI_PKT_SIZE*4+8;size++){u.data.hfi_pkt.pkt_data[0]=size;u.data.hfi_pkt.pkt_data[1]=1;reads=0;fail_at=0;bool valid=size>=12&&size<=MAX_HFI_PKT_SIZE*4&&size%4==0;assert((_get_pkt_hdr_from_user(&u,&hdr)==0)==valid);assert((_get_fence_pkt_hdr_from_user(&u,&hdr)==0)==valid);cases++;}
 for(int f=0;f<2;f++){u.data.hfi_pkt.pkt_data[0]=32;u.data.hfi_pkt.pkt_data[1]=1;assert(!_get_pkt_hdr_from_user(&u,&hdr));u.data.hfi_pkt.pkt_data[0]=UINT32_MAX;u.data.hfi_pkt.pkt_data[1]=UINT32_MAX;assert(!_copy_checked_pkt_from_user(&k,&u,&hdr,f));assert(k.data.hfi_pkt.pkt_data[0]==32&&k.data.hfi_pkt.pkt_data[1]==1);cases++;}
 for(int f=1;f<=2;f++){reads=0;fail_at=f;assert(_get_pkt_hdr_from_user(&u,&hdr)==-EFAULT);cases++;}fail_at=0;assert(_copy_pkt_from_user(&k,&u,UINT32_MAX,2)==-EINVAL);assert(_copy_pkt_from_user(&k,&u,MAX_HFI_PKT_SIZE-1,2)==-EINVAL);assert(_copy_pkt_to_user(&k,&u,MAX_HFI_PKT_SIZE+1)==-EINVAL);printf("{\"ioctl_bounds_and_header_snapshot_cases\":%u}\n",cases+3);}
