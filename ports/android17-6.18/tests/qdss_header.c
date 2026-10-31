#include "gsi_mock.h"
#define CONFIG_USB_F_QDSS QDSS_ENABLED
#define IS_ENABLED(x) (x)
/* QDSS_PUBLIC */
_Static_assert(__builtin_types_compatible_p(typeof(&usb_qdss_alloc_req),int (*)(struct usb_qdss_ch*,int)),"QDSS enabled/disabled allocation ABI differs");
int main(void){
#if !QDSS_ENABLED
assert(usb_qdss_alloc_req(NULL,1)==-ENODEV);assert(usb_qdss_write(NULL,NULL)==-ENODEV);
#endif
printf("{\"public_alloc_req_arguments\":2,\"selected\":%d}\n",QDSS_ENABLED);}
