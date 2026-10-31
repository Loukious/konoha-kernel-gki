#include "gsi_mock.h"
#define USB_QDSS_CH_EBC "qdss_ebc"
#define USB_QDSS_CH_MDM "qdss_mdm"
#define USB_QDSS_CH_SW "qdss_sw"
#define USB_EP_EBC 1
#define USB_EP_NONE 0
#define EBC_TRB_SIZE 16
#define NUM_EBC_IN_BUF 2
#define USB_SPEED_HIGH 3
#define PAGE_SIZE 4096
struct scatterlist{int unused;};
struct qdss_request{char*buf;int length,actual,status;void*context;struct scatterlist*sg;unsigned num_sgs;};
enum qdss_state{USB_QDSS_CONNECT,USB_QDSS_DISCONNECT,USB_QDSS_CTRL_READ_DONE,USB_QDSS_DATA_WRITE_DONE};
struct usb_request;struct usb_ep{const char*name;void*driver_data,*desc;bool enabled,claimed;};
struct usb_request{void*context,*buf;int length,actual,status;void(*complete)(struct usb_ep*,struct usb_request*);struct scatterlist*sg;unsigned num_sgs;bool queued;};
struct usb_gadget{int speed;};struct usb_composite_dev{struct usb_gadget*gadget;};struct usb_configuration{struct usb_composite_dev*cdev;};struct usb_function{const char*name;struct usb_configuration*config;};struct usb_function_instance{int unused;};
/* QDSS_TYPES */
static LIST_HEAD(usb_qdss_ch_list);static int channel_lock,pm_votes,request_live,request_steps,request_fault,queue_error,enable_steps,enable_fault,notifications,retired;static struct usb_request*pending_req;
static bool qdss_uses_sw_path(struct f_qdss*q){return q->ch.ch_type==USB_EP_NONE;}
static struct usb_request*usb_ep_alloc_request(struct usb_ep*ep,int f){if(++request_steps==request_fault)return NULL;struct usb_request*r=calloc(1,sizeof(*r));assert(r);request_live++;return r;}
static void usb_ep_free_request(struct usb_ep*ep,struct usb_request*r){assert(r&&!r->queued&&request_live>0);request_live--;free(r);}
static int usb_ep_queue(struct usb_ep*ep,struct usb_request*r,int f){if(queue_error)return queue_error;assert(!r->queued);r->queued=true;return 0;}
static int usb_ep_dequeue(struct usb_ep*ep,struct usb_request*r){if(!r->queued)return -EINVAL;r->queued=false;r->status=-ECONNRESET;r->actual=0;if(r->complete)r->complete(ep,r);return 0;}
static void usb_ep_disable(struct usb_ep*ep){ep->enabled=false;}
static int usb_ep_enable(struct usb_ep*ep){if(++enable_steps==enable_fault)return -EIO;assert(!ep->enabled);ep->enabled=true;return 0;}
static int config_ep_by_speed(struct usb_gadget*g,struct usb_function*f,struct usb_ep*ep){if(++enable_steps==enable_fault)return -EIO;ep->desc=(void*)1;return 0;}
static void usb_gadget_autopm_get_async(struct usb_gadget*g){pm_votes++;}
static void usb_gadget_autopm_put_async(struct usb_gadget*g){assert(pm_votes>0);pm_votes--;}
static int set_qdss_data_connection(struct f_qdss*q,int enable){return 0;}
static struct f_qdss*func_to_qdss(struct usb_function*f){return container_of(f,struct f_qdss,port.function);}
#define wait_for_completion(c) assert((c)->done)
#define wait_event(w,p) assert(p)
/* FUNCTIONS */
static void notify(void*p,unsigned e,struct qdss_request*r,struct usb_qdss_ch*ch){notifications++;if(e==USB_QDSS_DATA_WRITE_DONE){assert(r&&r->status==-ECONNRESET);retired++;}}
int main(void){self_module.state=MODULE_STATE_LIVE;struct f_qdss*q=alloc_usb_qdss(mock_name(USB_QDSS_CH_MDM));assert(!IS_ERR(q)&&live==2&&queues==1);char*dup=mock_name(USB_QDSS_CH_MDM);assert(PTR_ERR(alloc_usb_qdss(dup))==-EEXIST);kfree(dup);assert(live==2&&queues==1);assert(usb_qdss_open(NULL,NULL,notify)==NULL);struct usb_qdss_ch*ch=usb_qdss_open(USB_QDSS_CH_SW,NULL,notify);assert(ch==&q->ch&&q->ref.refs==2&&self_module.refs==1);assert(!usb_qdss_open(USB_QDSS_CH_MDM,NULL,notify)&&self_module.refs==1);struct usb_ep data={.name="data",.driver_data=q};q->port.data=&data;q->bound=true;q->usb_connected=1;
assert(usb_qdss_alloc_req(ch,0)==-EINVAL);assert(!usb_qdss_alloc_req(ch,2)&&request_live==2);for(int fail=1;fail<=3;fail++){request_fault=request_steps+fail;assert(usb_qdss_alloc_req(ch,3)==-ENOMEM);assert(request_live==2);}request_fault=0;char buffer[8]={0};struct qdss_request r={.buf=buffer,.length=8};assert(usb_qdss_write(ch,NULL)==-EINVAL);r.length=-1;assert(usb_qdss_write(ch,&r)==-EINVAL);r.length=8;queue_error=-EIO;assert(usb_qdss_write(ch,&r)==-EIO);queue_error=0;assert(!usb_qdss_write(ch,&r));assert(!list_empty(&q->queued_data_pool));struct usb_qdss_opts*opts=kzalloc(sizeof(*opts),0);opts->usb_qdss=q;usb_qdss_free_inst(&opts->func_inst);assert(q->dying&&q->ref.refs==1&&live==4);assert(!usb_qdss_open(USB_QDSS_CH_MDM,NULL,notify));assert(usb_qdss_write(ch,&r)==-EIO);usb_qdss_close(ch);assert(retired==1&&!request_live&&!live&&!queues&&!self_module.refs);
/* The four data configuration/enable errors and control pair failures must
 * retire the single PM vote acquired for a partial composition. */
for(int fault=1;fault<=6;fault++){struct f_qdss d={0};struct usb_ep in={.name="in"},ci={.name="ci"},co={.name="co"};struct usb_gadget gadget={.speed=USB_SPEED_HIGH};struct usb_composite_dev cd={&gadget};struct usb_configuration cfg={&cd};d.port.function.config=&cfg;d.port.data=&in;d.port.ctrl_in=&ci;d.port.ctrl_out=&co;d.data_iface_id=1;d.ctrl_iface_id=2;d.debug_inface_enabled=true;enable_steps=0;enable_fault=fault;int first=qdss_set_alt(&d.port.function,1,0);if(!first){int second=qdss_set_alt(&d.port.function,2,0);if(second!=-EIO)fprintf(stderr,"fault=%d steps=%d second=%d\n",fault,enable_steps,second);assert(second==-EIO);}else assert(first==-EIO);assert(!pm_votes&&!in.enabled&&!ci.enabled&&!co.enabled);}printf("{\"duplicate_channel_and_open_refused\":true,\"partial_allocation_preserves_pool\":true,\"queued_cancel_notifies_before_client_release\":true,\"open_client_outlives_removed_instance\":true,\"partial_endpoint_pm_failures\":6}\n");}
