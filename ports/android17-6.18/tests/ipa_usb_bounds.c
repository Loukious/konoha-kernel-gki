#include "ipa_mock.h"
#define IPA_USB_DBG(...) ((void)0)
#define IPA_USB_DBG_LOW(...) ((void)0)
#define IPA_USB_ERR(...) ((void)0)
#define GSI_CHAN_RE_SIZE_16B 16
#define fallthrough __attribute__((fallthrough))
/* TYPES */
enum ipa3_usb_teth_prot_state { IPA_USB_TETH_PROT_INITIALIZED, IPA_USB_TETH_PROT_CONNECTED, IPA_USB_TETH_PROT_INVALID };
enum ipa3_usb_transport_type { IPA_USB_TRANSPORT_TETH, IPA_USB_TRANSPORT_DPL, IPA_USB_TRANSPORT_TETH_2, IPA_USB_TRANSPORT_MAX };
enum ipa_usb_direction { IPA_USB_DIR_UL, IPA_USB_DIR_DL };
#define IPA3_USB_GET_TTYPE(p) ((p)==IPA_USB_DIAG?IPA_USB_TRANSPORT_DPL:((p)==IPA_USB_RMNET_CV2X?IPA_USB_TRANSPORT_TETH_2:IPA_USB_TRANSPORT_TETH))
struct ipa3_usb_teth_prot_context { enum ipa3_usb_teth_prot_state state; };
struct ipa3_usb_context { struct ipa3_usb_teth_prot_context teth_prot_ctx[IPA_USB_MAX_TETH_PROT_SIZE]; struct mutex general_mutex; bool shutting_down; };
static struct ipa3_usb_context *ipa3_usb_ctx;
static bool ipa3_usb_check_chan_params(struct ipa_usb_xdci_chan_params *p);
static int step,fail_request,fail_connect,active_channels,switch_calls;
static bool channels[3];
static bool ipa3_usb_is_teth_switch_valid(enum ipa_usb_teth_prot p) { assert(p>=0&&p<IPA_USB_MAX_TETH_PROT_SIZE);switch_calls++;return true; }
static int ipa3_usb_request_xdci_channel(struct ipa_usb_xdci_chan_params *p,enum ipa_usb_direction d,struct ipa_req_chan_out_params *o) {
 if(!ipa3_usb_check_chan_params(p))return -EINVAL;
 if(++step==fail_request)return -EIO;
 o->clnt_hdl=d+1;assert(!channels[o->clnt_hdl]);channels[o->clnt_hdl]=true;active_channels++;return 0;
}
static int ipa3_usb_release_xdci_channel(u32 h,enum ipa_usb_direction d,enum ipa3_usb_transport_type t) { assert(h==d+1&&channels[h]);channels[h]=false;active_channels--;return 0; }
static int ipa3_usb_xdci_connect_internal(struct ipa_usb_xdci_connect_params_internal *p) { assert(channels[2]);if(p->teth_prot!=IPA_USB_DIAG)assert(channels[1]);return fail_connect; }
/* FUNCTIONS */
int main(void)
{
 struct ipa3_usb_context ctx={0};mutex_init(&ctx.general_mutex);ipa3_usb_ctx=&ctx;
 struct ipa_usb_xdci_chan_params ul={.teth_prot=IPA_USB_RMNET,.dir=CHAN_DIR_TO_GSI,.xfer_ring_len=64,.xfer_scratch={.const_buffer_size=16}},dl=ul;
 dl.dir=CHAN_DIR_FROM_GSI;
 struct ipa_usb_xdci_connect_params conn={.teth_prot=IPA_USB_RMNET,.max_pkt_size=IPA_USB_SUPER_SPEED_1024B};
 struct ipa_req_chan_out_params a={0},b={0};unsigned bounds=0,rollbacks=0;
 assert(!ipa3_usb_check_chan_params(NULL));
 for(int p=-3;p<IPA_USB_MAX_TETH_PROT_SIZE+3;p++) {
  ul.teth_prot=p;
  for(int d=-2;d<=3;d++)for(int len=0;len<=256;len++)for(int size=0;size<=32;size++) {
   ul.dir=d;ul.xfer_ring_len=len;ul.xfer_scratch.const_buffer_size=size;
   bool expected=p>=0&&p<IPA_USB_MAX_TETH_PROT_SIZE&&(d==0||d==1)&&len>0&&len%16==0&&size>=1&&size<=31;
   assert(ipa3_usb_check_chan_params(&ul)==expected);bounds++;
  }
 }
 ul=(struct ipa_usb_xdci_chan_params){.teth_prot=IPA_USB_RMNET,.dir=CHAN_DIR_TO_GSI,.xfer_ring_len=64,.xfer_scratch={.const_buffer_size=16}};dl=ul;dl.dir=CHAN_DIR_FROM_GSI;
 for(int p=-3;p<IPA_USB_MAX_TETH_PROT_SIZE+3;p++)if(p<0||p>=IPA_USB_MAX_TETH_PROT_SIZE){conn.teth_prot=p;assert(ipa_usb_xdci_connect(&ul,&dl,&a,&b,&conn)==-EINVAL);}
 conn.teth_prot=IPA_USB_RMNET;assert(!switch_calls&&!active_channels);
 dl.teth_prot=IPA_USB_DIAG;assert(ipa_usb_xdci_connect(&ul,&dl,&a,&b,&conn)==-EINVAL);dl.teth_prot=IPA_USB_RMNET;
 ul.dir=CHAN_DIR_FROM_GSI;assert(ipa_usb_xdci_connect(&ul,&dl,&a,&b,&conn)==-EINVAL);ul.dir=CHAN_DIR_TO_GSI;
 assert(ipa_usb_xdci_connect(NULL,&dl,&a,&b,&conn)==-EINVAL);assert(ipa_usb_xdci_connect(&ul,NULL,&a,&b,&conn)==-EINVAL);assert(ipa_usb_xdci_connect(&ul,&dl,NULL,&b,&conn)==-EINVAL);assert(ipa_usb_xdci_connect(&ul,&dl,&a,NULL,&conn)==-EINVAL);assert(ipa_usb_xdci_connect(&ul,&dl,&a,&b,NULL)==-EINVAL);assert(!step&&!active_channels);
 for(int f=1;f<=2;f++){step=0;fail_request=f;assert(ipa_usb_xdci_connect(&ul,&dl,&a,&b,&conn)==-EIO&&!active_channels);rollbacks++;}fail_request=0;
 fail_connect=-ETIMEDOUT;assert(ipa_usb_xdci_connect(&ul,&dl,&a,&b,&conn)==-ETIMEDOUT&&!active_channels);rollbacks++;
 conn.teth_prot=IPA_USB_DIAG;dl.teth_prot=IPA_USB_DIAG;assert(ipa_usb_xdci_connect(NULL,&dl,NULL,&b,&conn)==-ETIMEDOUT&&!active_channels);rollbacks++;
 fail_connect=0;assert(!ipa_usb_xdci_connect(NULL,&dl,NULL,&b,&conn)&&active_channels==1);ipa3_usb_release_xdci_channel(b.clnt_hdl,IPA_USB_DIR_DL,IPA_USB_TRANSPORT_DPL);
 ctx.shutting_down=true;assert(ipa_usb_xdci_connect(NULL,&dl,NULL,&b,&conn)==-ENODEV&&!active_channels);ctx.shutting_down=false;
 ipa3_usb_ctx=NULL;assert(ipa_usb_xdci_connect(NULL,&dl,NULL,&b,&conn)==-ENODEV);
 printf("{\"native_parameter_cases\":%u,\"channel_failure_rollbacks\":%u,\"protocol_direction_identity_validated\":true,\"null_and_shutdown_operations_safe\":true}\n",bounds,rollbacks);
}
