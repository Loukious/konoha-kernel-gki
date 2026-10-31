#include "gsi_mock.h"
#define ETH_ALEN 6
#define USB_EP_NONE 0
#define USB_EP_GSI 1
#define USB_SPEED_SUPER 4
#define DEFAULT_MAX_PKT_PER_XFER 15
struct sg_table{int unused;};
/* IPA_TYPES */
/* DWC_TYPES */
enum gsi_ep_op{GSI_EP_OP_PREPARE_TRBS,GSI_EP_OP_STARTXFER,GSI_EP_OP_GET_XFER_IDX,GSI_EP_OP_GET_CH_INFO,GSI_EP_OP_FREE_TRBS,GSI_EP_OP_ENDXFER,GSI_EP_OP_STORE_DBL_INFO,GSI_EP_OP_ENABLE_GSI,GSI_EP_OP_SET_CLR_BLOCK_DBL,GSI_EP_OP_RING_DB,GSI_EP_OP_DISABLE};
struct usb_ep{int mode;bool rings,started,enabled;};
struct usb_gadget{int speed;};struct usb_composite_dev{struct usb_gadget*gadget;};struct usb_configuration{struct usb_composite_dev*cdev;};struct usb_function{struct usb_configuration*config;};
struct gsi_data_port{struct usb_ep*in_ep,*out_ep;struct usb_gsi_request in_request,out_request;struct ipa_usb_ops*ipa_ops;struct ipa_usb_xdci_chan_params ipa_in_channel_params,ipa_out_channel_params;struct ipa_usb_xdci_connect_params ipa_conn_pms;u32 in_xfer_rsc_index,out_xfer_rsc_index;int in_channel_handle,out_channel_handle;u32 in_aggr_size,out_aggr_size;};
struct f_gsi{struct usb_function function;struct gsi_data_port d_port;enum ipa_usb_teth_prot prot_id;};
#define d_port_to_gsi(p) container_of(p,struct f_gsi,d_port)
static int steps,fault,index_value=7;
static bool failing(void){return ++steps==fault;}
static int msm_ep_set_mode(struct usb_ep*ep,int mode){if(mode&&failing())return -EIO;ep->mode=mode;return 0;}
static int usb_gsi_ep_op(struct usb_ep*ep,void*data,enum gsi_ep_op op){if(op!=GSI_EP_OP_FREE_TRBS&&op!=GSI_EP_OP_ENDXFER&&op!=GSI_EP_OP_DISABLE&&failing())return -EIO;switch(op){case GSI_EP_OP_PREPARE_TRBS:assert(!ep->rings);ep->rings=true;break;case GSI_EP_OP_STARTXFER:assert(ep->rings);ep->started=true;break;case GSI_EP_OP_GET_XFER_IDX:return index_value;case GSI_EP_OP_GET_CH_INFO:{struct gsi_channel_info*ci=data;assert(ci->ch_req);ci->xfer_ring_len=128;ci->xfer_ring_base_addr=0x123000;break;}case GSI_EP_OP_ENDXFER:ep->started=false;break;case GSI_EP_OP_FREE_TRBS:assert(ep->rings&&!ep->started);ep->rings=false;break;case GSI_EP_OP_DISABLE:ep->started=false;ep->enabled=false;break;default:break;}return 0;}
static int connect_op(struct ipa_usb_xdci_chan_params*o,struct ipa_usb_xdci_chan_params*i,struct ipa_req_chan_out_params*oo,struct ipa_req_chan_out_params*io,struct ipa_usb_xdci_connect_params*c){if(failing())return -EIO;assert(i->xfer_ring_len==128);assert(c->ipa_to_usb_xferrscidx==7);io->clnt_hdl=9;oo->clnt_hdl=10;return 0;}
static int disconnect_op(u32 o,u32 i,enum ipa_usb_teth_prot p){return 0;}
/* FUNCTIONS */
int main(void){struct usb_ep in={0},out={0};struct usb_gadget gadget={USB_SPEED_SUPER};struct usb_composite_dev cd={&gadget};struct usb_configuration cfg={&cd};struct ipa_usb_ops ops={.xdci_connect=connect_op,.xdci_disconnect=disconnect_op};struct f_gsi g={.function={&cfg},.prot_id=IPA_USB_RMNET,.d_port={.in_ep=&in,.out_ep=&out,.ipa_ops=&ops,.in_request={.num_bufs=8,.buf_len=4096},.out_request={.num_bufs=4,.buf_len=1024}}};unsigned cases=0;for(int f=1;f<=11;f++){memset(&in,0,sizeof(in));memset(&out,0,sizeof(out));steps=0;fault=f;assert(ipa_connect_channels(&g.d_port)==-EIO);assert(!in.rings&&!out.rings&&!in.started&&!out.started);cases++;}steps=0;fault=0;index_value=128;assert(ipa_connect_channels(&g.d_port)==-ERANGE&&!in.rings&&!out.rings);index_value=7;
for(int f=1;f<=6;f++){steps=0;fault=0;assert(!ipa_connect_channels(&g.d_port));steps=0;fault=f;assert(ipa_data_path_enable(&g.d_port)==-EIO);ipa_data_path_disable(&g.d_port);ipa_disconnect_channel(&g.d_port);assert(!in.rings&&!out.rings&&g.d_port.in_channel_handle==-EINVAL);cases++;}printf("{\"channel_failure_rollback_cases\":%u,\"invalid_transfer_index_rejected\":true,\"doorbell_enable_failure_reported\":true}\n",cases);}
