#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#define ERESTARTSYS 512
#define GFP_ATOMIC 0
#define READ_ONCE(x) (x)
#define EPOLLERR 8
#define EPOLLHUP 16
#define EPOLLIN 1
#define EPOLLRDNORM 64
#define EPOLLPRI 2
#define TIOCM_DTR 2
#define TIOCM_RTS 4
#define TIOCM_CD 64
#define TIOCM_RI 128
#define ACM_CTRL_DTR 1
#define ACM_CTRL_RTS 2
#define ACM_CTRL_DCD 1
#define ACM_CTRL_RI 8
#define BRIDGE_TX_BUF_SIZE 2048
#define USB_DIR_IN 128
#define USB_TYPE_CLASS 32
#define USB_RECIP_INTERFACE 1
#define USB_CDC_NOTIFY_SERIAL_STATE 32
#define fallthrough __attribute__((fallthrough))
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define __user
#define cpu_to_le16(x) (x)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define min(a,b) ((a)<(b)?(a):(b))
#define min_t(t,a,b) min((t)(a),(t)(b))
#define spin_lock_irqsave(p,f) ((void)(p),(f)=0)
#define spin_unlock_irqrestore(p,f) ((void)(p),(void)(f))
#define wake_up(p) ((p)->wakes++)
#define wait_event_interruptible(q,expr) ((void)(q),(expr)?0:wait_result)
#define mutex_lock_interruptible(p) ((void)(p),0)
#define mutex_unlock(p) ((void)(p))
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint16_t __le16;
typedef unsigned __poll_t;
typedef int poll_table;
struct list_head { struct list_head *next,*prev; };
static void init_list(struct list_head *h) {h->next=h->prev=h;}
static void list_add_tail(struct list_head *n,struct list_head *h) {n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del_init(struct list_head *n) {n->prev->next=n->next;n->next->prev=n->prev;init_list(n);}
#define list_empty(h) ((h)->next==(h))
#define list_entry(p,t,m) container_of(p,t,m)
#define list_first_entry(h,t,m) list_entry((h)->next,t,m)
struct device {int refs;};
static void get_device(struct device *d) {assert(d->refs>0);d->refs++;}
static void put_device(struct device *d) {assert(d->refs>0);d->refs--;}
struct cdev {int unused;};
struct inode {struct cdev *i_cdev;};
struct file {void *private_data;};
struct mutex {int unused;};
struct wait_queue {int wakes;};
struct usb_ep;
struct usb_request {struct list_head list;void *buf,*context;unsigned length,actual;int status,zero;void (*complete)(struct usb_ep *,struct usb_request *);};
struct usb_ep {void *driver_data;struct list_head pending;};
struct usb_cdc_notification {u8 bmRequestType,bNotificationType;u16 wValue,wIndex,wLength;} __attribute__((packed));
struct cserial {struct usb_ep *in,*out,*notify;struct usb_request *notify_req;u16 serial_state,port_handshake_bits;u8 pending,q_again,data_id;unsigned (*get_dtr)(struct cserial*),(*get_rts)(struct cserial*),(*send_ring_indicator)(struct cserial*,unsigned),(*send_carrier_detect)(struct cserial*,unsigned);};
struct f_cdev {struct cdev fcdev_cdev;struct device dev;struct mutex io_lock;int port_lock;struct wait_queue open_wq,read_wq;bool is_connected,dying,port_open,cbits_updated;unsigned long connection_id;unsigned cbits_to_modem;struct list_head read_pool,read_queued,write_pool;struct usb_request *current_rx_req;u8 *current_rx_buf;size_t pending_rx_bytes;struct cserial port_usb;unsigned long nbytes_to_port_bridge,nbytes_from_port_bridge,nbytes_to_host,nbytes_from_host;};
static struct f_cdev *copy_port;
static int action,copy_fault,queue_fault,live,queued,wait_result=-ERESTARTSYS,start_calls;
static void usb_cser_start_rx(struct f_cdev *p) {(void)p;start_calls++;}
static void usb_cser_disconnect(struct f_cdev *);
static void usb_cser_notify_modem(void *,int);
static void poll_wait(struct file *f,struct wait_queue *w,poll_table *p) {(void)f;(void)w;(void)p;}
static int usb_ep_queue(struct usb_ep *ep,struct usb_request *r,int flags) {(void)flags;if(queue_fault)return -EIO;assert(list_empty(&r->list));list_add_tail(&r->list,&ep->pending);queued++;return 0;}
static void usb_ep_free_request(struct usb_ep *ep,struct usb_request *r) {(void)ep;assert(list_empty(&r->list));assert(live>0);live--;free(r);}
static void usb_ep_disable(struct usb_ep *ep) {while(!list_empty(&ep->pending)){struct usb_request *r=list_first_entry(&ep->pending,struct usb_request,list);list_del_init(&r->list);queued--;r->status=-ESHUTDOWN;r->complete(ep,r);}}
#define kfree(p) free(p)
static size_t user_copy(void *dst,const void *src,size_t n) {size_t lost=copy_fault?min(n,(size_t)copy_fault):0;memcpy(dst,src,n-lost);if(action){int a=action;action=0;usb_cser_disconnect(copy_port);if(a==2){copy_port->is_connected=true;copy_port->connection_id++;}}return lost;}
#define copy_to_user user_copy
#define copy_from_user user_copy
static struct f_cdev *cser_to_port(struct cserial *c) {return container_of(c,struct f_cdev,port_usb);}
/* FUNCTIONS */
static struct usb_request *req_new(struct f_cdev *p,struct list_head *pool,int receive) {struct usb_request *r=calloc(1,sizeof(*r));assert(r);r->buf=malloc(2048);assert(r->buf);memset(r->buf,0x51,2048);r->length=r->actual=2048;r->context=p;r->complete=receive?usb_cser_read_complete:usb_cser_write_complete;init_list(&r->list);if(pool)list_add_tail(&r->list,pool);live++;return r;}
static unsigned marker(struct cserial *c,unsigned v) {(void)c;return v;}
static void init_port(struct f_cdev *p,struct usb_ep *in,struct usb_ep *out,struct usb_ep *notify) {memset(p,0,sizeof(*p));memset(in,0,sizeof(*in));memset(out,0,sizeof(*out));memset(notify,0,sizeof(*notify));init_list(&in->pending);init_list(&out->pending);init_list(&notify->pending);in->driver_data=out->driver_data=p;p->port_usb.in=in;p->port_usb.out=out;p->port_usb.notify=notify;init_list(&p->read_pool);init_list(&p->read_queued);init_list(&p->write_pool);p->is_connected=p->port_open=true;p->dev.refs=1;p->port_usb.send_ring_indicator=p->port_usb.send_carrier_detect=marker;p->port_usb.get_dtr=dun_cser_get_dtr;p->port_usb.get_rts=dun_cser_get_rts;copy_port=p;}
int main(void) {
 struct f_cdev p;struct usb_ep in,out,notify;char buf[4096]={0};struct file f={.private_data=&p};int cases=0;
 for(int a=0;a<3;a++)for(int fault=0;fault<4;fault++) {init_port(&p,&in,&out,&notify);struct usb_request *r=req_new(&p,&p.read_queued,1);r->actual=37;action=a;copy_fault=fault;ssize_t n=f_cdev_read(&f,buf,80,NULL);assert(n==37-fault);if(a)assert(!live);else {assert(live==1);copy_fault=0;assert(f_cdev_read(&f,buf,80,NULL)==fault);usb_cser_disconnect(&p);assert(!live);}cases++;}
 for(int a=0;a<3;a++)for(int fault=0;fault<2;fault++)for(int q=0;q<2;q++) {init_port(&p,&in,&out,&notify);req_new(&p,&p.write_pool,0);action=a;copy_fault=fault;queue_fault=q;ssize_t n=f_cdev_write(&f,buf,sizeof(buf),NULL);assert(n==(a?-ENODEV:fault?-EFAULT:q?-EIO:2048));usb_cser_disconnect(&p);assert(!live&&!queued);cases++;}
 copy_fault=queue_fault=0;init_port(&p,&in,&out,&notify);p.port_open=false;struct inode ino={.i_cdev=&p.fcdev_cdev};struct file opened={0};assert(!f_cdev_open(&ino,&opened));assert(p.dev.refs==2);assert(f_cdev_open(&ino,&f)==-EBUSY);p.dying=true;assert(!f_cdev_release(&ino,&opened));assert(p.dev.refs==1);assert(f_cdev_open(&ino,&opened)==-ENODEV);
 for(unsigned h=0;h<256;h++) {assert(convert_acm_sigs_to_uart(h)==((h&1?TIOCM_DTR:0)|(h&2?TIOCM_RTS:0)));cases++;}
 init_port(&p,&in,&out,&notify);struct usb_request *nr=req_new(&p,NULL,0);nr->complete=usb_cser_notify_complete;p.port_usb.notify_req=nr;p.port_usb.data_id=7;
 for(unsigned state=0;state<16;state++) {p.port_usb.serial_state=state;assert(!port_notify_serial_state(&p.port_usb));assert(p.port_usb.pending);struct usb_cdc_notification *n=nr->buf;assert(n->wLength==2&&n->wIndex==7);u16 emitted;memcpy(&emitted,n+1,2);assert(emitted==state);p.port_usb.serial_state=state^9;assert(!port_notify_serial_state(&p.port_usb));assert(p.port_usb.q_again&&queued==1);list_del_init(&nr->list);queued--;nr->status=0;usb_cser_notify_complete(&notify,nr);assert(p.port_usb.pending&&!p.port_usb.q_again);memcpy(&emitted,n+1,2);assert(emitted==(state^9));usb_ep_disable(&notify);assert(!p.port_usb.pending&&!queued);cases++;}
 for(unsigned set=0;set<256;set++)for(unsigned clear=0;clear<256;clear++) {p.port_usb.serial_state=2;assert(!f_cdev_tiocmset(&p,set,clear));unsigned expected=2|(set&TIOCM_RI?8:0)|(set&TIOCM_CD?1:0);expected&=~((clear&TIOCM_RI?8:0)|(clear&TIOCM_CD?1:0));assert(p.port_usb.serial_state==expected);assert((f_cdev_tiocmget(&p)&(TIOCM_RI|TIOCM_CD))==((expected&8?TIOCM_RI:0)|(expected&1?TIOCM_CD:0)));usb_ep_disable(&notify);cases++;}
 queue_fault=1;assert(port_notify_serial_state(&p.port_usb)==-EIO&&!p.port_usb.pending);queue_fault=0;usb_cser_disconnect(&p);assert(port_notify_serial_state(&p.port_usb)==-ENODEV);assert(f_cdev_tiocmget(&p)==-ENODEV);usb_cser_free_req(&notify,nr);assert(!live&&!queued);
 printf("{\"copy_disconnect_generation_cases\":24,\"serial_control_and_notification_cases\":%d,\"file_references\":true,\"request_resources_balanced\":true}\n",cases-24);
}
