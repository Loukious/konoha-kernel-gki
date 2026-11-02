#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#define GFP_KERNEL 0
#define DMA_TO_DEVICE 0
#define MHI_EOT 1
#define QRTR_EP_NET_ID_AUTO 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
typedef uint32_t u32;
struct completion { int done; };
struct device { void *data; bool pci; };
struct device_node { int dummy; };
struct pci_dev { struct device dev; u32 device; };
struct mhi_controller { struct device *cntrl_dev; };
struct mhi_device { struct device dev; struct mhi_controller *mhi_cntrl; };
struct mhi_device_id { int unused; };
struct sock { int refs; };
struct sk_buff { struct sock *sk; int len; bool freed; };
struct qrtr_endpoint { int (*xmit)(struct qrtr_endpoint *,struct sk_buff *); bool registered; };
struct mhi_result { struct sk_buff *buf_addr; int transaction_status,bytes_xferd; };
struct qrtr_mhi_dev {
 struct qrtr_endpoint ep; struct mhi_device *mhi_dev; struct device *dev;
 struct completion prepared,ringfull; bool stopping;
};
static int waits,wait_fail,stop_at_wait,linear_rc,queue_rc,retries,queued;
static int prepare_rc,endpoint_rc,unregistered,unprepared,alloc_fail;
static int dt_present,dt_match=1,dt_reads,node_refs; static bool dt_rt;
static struct qrtr_mhi_dev *active;
static void *devres;
static struct device_node node;
static void qcom_mhi_qrtr_stop(struct qrtr_mhi_dev *qdev);
static void init_completion(struct completion *c) { c->done=0; }
static void reinit_completion(struct completion *c) { c->done=0; }
static void complete_all(struct completion *c) { c->done=1; }
static int wait_for_completion_interruptible(struct completion *c) {
 waits++; if(waits==stop_at_wait) qcom_mhi_qrtr_stop(active);
 if(waits==wait_fail) return -EINTR;
 if(!c->done) complete_all(c); return 0;
}
static void sock_hold(struct sock *s) { s->refs++; }
static void sock_put(struct sock *s) { assert(s->refs>0);s->refs--; }
static void kfree_skb(struct sk_buff *s) { assert(!s->freed);s->freed=true; }
static void consume_skb(struct sk_buff *s) { kfree_skb(s); }
static int skb_linearize(struct sk_buff *s) { (void)s;return linear_rc; }
static int mhi_queue_skb(struct mhi_device *d,int dir,struct sk_buff *s,int len,int flags) {
 (void)d;(void)dir;(void)s;(void)len;(void)flags;queued++;
 if(retries) { retries--;return -EAGAIN; } return queue_rc;
}
static void *devm_kzalloc(struct device *d,size_t n,int f) { (void)d;(void)f;return devres=alloc_fail?NULL:calloc(1,n); }
static void dev_set_drvdata(struct device *d,void *p) { d->data=p; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static bool dev_is_pci(struct device *d) { return d->pci; }
#define to_pci_dev(d) container_of(d,struct pci_dev,dev)
static struct device_node *of_find_compatible_node(struct device_node *n,void *unused,const char *s) { (void)n;(void)unused;(void)s;if(!dt_present)return NULL;node_refs++;return &node; }
static int of_property_read_u32(struct device_node *n,const char *name,u32 *v) { assert(n==&node);dt_reads++;*v=!strcmp(name,"qcom,dev-id")?(dt_match?7:8):42;return 0; }
static bool of_property_read_bool(struct device_node *n,const char *s) { (void)n;(void)s;return dt_rt; }
static void of_node_put(struct device_node *n) { assert(n==&node);assert(node_refs);node_refs--; }
static int qrtr_endpoint_register(struct qrtr_endpoint *e,u32 id,bool rt,void *x) { (void)id;(void)rt;(void)x;e->registered=!endpoint_rc;return endpoint_rc; }
static void qrtr_endpoint_unregister(struct qrtr_endpoint *e) { struct qrtr_mhi_dev *q=container_of(e,struct qrtr_mhi_dev,ep);assert(e->registered);assert(q->stopping&&q->prepared.done&&q->ringfull.done);e->registered=false;unregistered++; }
static int mhi_prepare_for_transfer_autoqueue(struct mhi_device *d) { (void)d;return prepare_rc; }
static void mhi_unprepare_from_transfer(struct mhi_device *d) { assert(!((struct qrtr_mhi_dev *)d->dev.data)->ep.registered);unprepared++; }
#include "production.inc"
static void reset(void) { waits=wait_fail=stop_at_wait=linear_rc=queue_rc=retries=queued=0;prepare_rc=endpoint_rc=unregistered=unprepared=alloc_fail=0;dt_present=dt_reads=node_refs=0;dt_rt=false; }
static void send_case(int mode) {
 struct mhi_device dev={0};struct qrtr_mhi_dev q={.mhi_dev=&dev};struct sock sock={.refs=1};struct sk_buff skb={.sk=&sock,.len=8};int rc;
 reset();active=&q;dev.dev.data=&q;complete_all(&q.prepared);
 if(mode==1)retries=3;if(mode==2)queue_rc=-EIO;if(mode==3)linear_rc=-ENOMEM;
 if(mode==4)wait_fail=1;if(mode==5){retries=1;wait_fail=2;}
 if(mode==6)qcom_mhi_qrtr_stop(&q);if(mode==7){retries=1;stop_at_wait=2;}
 rc=qcom_mhi_qrtr_send(&q.ep,&skb);
 if(mode<2){assert(rc==0&&!skb.freed&&sock.refs==2);struct mhi_result result={.buf_addr=&skb};qcom_mhi_qrtr_ul_callback(&dev,&result);assert(sock.refs==1&&skb.freed);if(mode==1)assert(queued==4);}
 else assert(rc<0&&skb.freed&&sock.refs==1);
}
int main(void) {
 for(int i=0;i<8;i++)send_case(i);
 struct pci_dev pci={.dev.pci=true,.device=7};struct mhi_controller controller={.cntrl_dev=&pci.dev};struct mhi_device dev={.mhi_cntrl=&controller};u32 id=99;bool rt=true;
 reset();qrtr_mhi_of_parse(&dev,&id,&rt);assert(id==0&&!rt);
 reset();dt_present=1;dt_rt=true;qrtr_mhi_of_parse(&dev,&id,&rt);assert(id==42&&rt&&node_refs==0);
 reset();dt_present=1;dt_match=0;qrtr_mhi_of_parse(&dev,&id,&rt);assert(id==0&&!rt&&node_refs==0);
 pci.dev.pci=false;reset();dt_present=1;qrtr_mhi_of_parse(&dev,&id,&rt);assert(id==0&&!rt&&dt_reads==0);pci.dev.pci=true;
 reset();alloc_fail=1;assert(qcom_mhi_qrtr_probe(&dev,NULL)==-ENOMEM);
 reset();endpoint_rc=-EIO;assert(qcom_mhi_qrtr_probe(&dev,NULL)==-EIO&&dev.dev.data==NULL);free(devres);devres=NULL;
 /* devm allocations live until device unbind; harness frees them via tracked pointer below. */
 reset();prepare_rc=-EIO;assert(qcom_mhi_qrtr_probe(&dev,NULL)==-EIO&&dev.dev.data==NULL&&unregistered==1);free(devres);devres=NULL;
 reset();assert(qcom_mhi_qrtr_probe(&dev,NULL)==0);struct qrtr_mhi_dev *q=dev.dev.data;assert(q->prepared.done);qcom_mhi_qrtr_remove(&dev);assert(dev.dev.data==NULL&&unregistered==1&&unprepared==1);free(q);
 puts("16 MHI retry, configuration and lifecycle scenarios passed");return 0;
}
