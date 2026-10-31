#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define CVP_DSP_MAX_RESERVED 5
#define CVP_DSP2CPU_RESERVED 7
#define SESSION_NAME_MAX_LEN 256
#define CPU2DSP_MAX_CMD 8
#define DSP2CPU_POWERON 11
#define CVP_DSP_MAX_CMD 24
#define CVP_INVALID_RPMSG_TYPE 0xBADDFACE
#define dprintk(...) ((void)0)
#include "wire_types.inc"
struct rpmsg_device {int unused;};
struct cvp_dsp_apps {struct cvp_dsp_rsp_msg pending_dsp2cpu_rsp;struct cvp_dsp2cpu_cmd pending_dsp2cpu_cmd;int completions[CPU2DSP_MAX_CMD+1];};
static struct cvp_dsp_apps gfa_cv;
static void complete(int *p) {(*p)++;}
#include "wire.inc"
static void reset(void) {memset(&gfa_cv,0,sizeof(gfa_cv));gfa_cv.pending_dsp2cpu_cmd.type=CVP_INVALID_RPMSG_TYPE;gfa_cv.pending_dsp2cpu_rsp.type=2;}
int main(void) {
 unsigned cases=0;
 for (int off=0;off<8;off++) for(int len=-1;len<(int)sizeof(struct cvp_dsp2cpu_cmd)+8;len++) {
  reset();unsigned char *base=calloc(1,(len>0?len:1)+off),*p=base+off;
  if(len>=4){uint32_t type=2;memcpy(p,&type,4);}
  int rc=cvp_dsp_rpmsg_callback(NULL,p,len,NULL,0);
  if(len!=(int)sizeof(struct cvp_dsp2cpu_cmd)&&len!=(int)sizeof(struct cvp_dsp_rsp_msg))assert(rc==-EINVAL);
  else assert(!rc);
  assert(gfa_cv.completions[2]==(len==(int)sizeof(struct cvp_dsp_rsp_msg)));
  assert(!gfa_cv.completions[CPU2DSP_MAX_CMD]);free(base);cases++;
 }
 for(unsigned type=0;type<27;type++) for(int off=0;off<8;off++) {
  reset();struct cvp_dsp2cpu_cmd cmd={.type=type,.session_id=123};unsigned char p[sizeof(cmd)+8];memcpy(p+off,&cmd,sizeof(cmd));assert(!cvp_dsp_rpmsg_callback(NULL,p+off,sizeof(cmd),NULL,0));
  assert(gfa_cv.completions[8]==(type>=11&&type<24));
  if(type>=11&&type<24) {assert(gfa_cv.pending_dsp2cpu_cmd.session_id==123);assert(!cvp_dsp_rpmsg_callback(NULL,p+off,sizeof(cmd),NULL,0));assert(gfa_cv.completions[8]==1);}
  cases++;
 }
 reset();assert(cvp_dsp_rpmsg_callback(NULL,NULL,28,NULL,0)==-EINVAL);
 printf("{\"wire_bounds_alignment_and_types\":%u}\n",cases+1);
}
