#include "ipa_mock.h"
#define GSIERR(...) ((void)0)
#define GSI_VER_2_9 29
#define GSI_VER_3_0 30
/* REGISTER CONSTANTS */
static struct { unsigned gsi_ver; } context;
static typeof(context) *gsihal_ctx=&context;
/* FUNCTIONS */
int main(void)
{
 unsigned sizes[]={0,1,3,4,8,16,32,64,65,UINT_MAX};unsigned cases=0;
 for(unsigned v=28;v<=30;v++){context.gsi_ver=v;u32 mask=v==30?0xffffff:(v==29?0xfffff:0xffff);
  for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++){unsigned size=sizes[i];
   for(unsigned len=0;len<66000;len++){
    bool valid=(size==4||size==8||size==16||size==32||size==64)&&len>=size&&len%size==0&&!(len&~mask)&&(v<30||len/size<65536);
    assert(gsihal_check_ring_length_valid(len,size)==valid);cases++;
   }
   u32 edges[]={mask-1,mask,mask+1,UINT_MAX,65536*size};
   for(unsigned j=0;j<sizeof(edges)/sizeof(edges[0]);j++){unsigned len=edges[j];bool valid=(size==4||size==8||size==16||size==32||size==64)&&len>=size&&len%size==0&&!(len&~mask)&&(v<30||len/size<65536);assert(gsihal_check_ring_length_valid(len,size)==valid);cases++;}
  }
 }
 printf("{\"ring_size_and_element_cases\":%u,\"zero_and_unknown_elements_rejected\":true,\"native_register_masks_preserved\":true}\n",cases);
}
