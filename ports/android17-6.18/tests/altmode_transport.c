#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define MSG_OWNER_USBC_PAN 32780
#define MSG_TYPE_REQ_RESP 1
#define MSG_TYPE_NOTIFY 2
#define USBC_WRITE_BUFFER_SIZE 8
#define NOTIFY_PAYLOAD_SIZE 16
#define USBC_CMD_WRITE_REQ 0x15
#define USBC_NOTIFY_IND 0x16
#define MAX_NUM_PORTS 4
#define ALTMODE_WAIT_MS 1000
#define ALTMODE_PAN_ACK 0x11
#define GET_OP(x) ((x)&255)
#define GET_SVID(x) ((x)>>16)
#define IDR_KEY_GEN(s,p) (((s)<<8)|(p))
#define IDR_KEY(c) IDR_KEY_GEN((c)->data.svid,(c)->port_index)
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>(uintptr_t)-4096)
#define ERR_PTR(e) ((void*)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define pr_err(...) ((void)0)
#define altmode_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#define msecs_to_jiffies(x) (x)
#define set_bit(b,p) (*(p)|=1ul<<(b))
#define INIT_WORK(w,f) ((w)->fn=(f))
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *h) {h->next=h->prev=h;}
static void list_add_tail(struct list_head *n,struct list_head *h) {n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
#define list_add list_add_tail
static void list_del(struct list_head *n) {n->next->prev=n->prev;n->prev->next=n->next;}
static void list_del_init(struct list_head *n) {list_del(n);INIT_LIST_HEAD(n);}
#define list_empty(h) ((h)->next==(h))
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
#define list_for_each_entry_safe(pos,tmp,head,member) for(pos=container_of((head)->next,__typeof__(*pos),member),tmp=container_of(pos->member.next,__typeof__(*pos),member);&pos->member!=(head);pos=tmp,tmp=container_of(tmp->member.next,__typeof__(*tmp),member))
struct kref {int count;};
static void kref_init(struct kref *r) {r->count=1;}
static void kref_get(struct kref *r) {assert(r->count>0);r->count++;}
static void kref_put(struct kref *r,void(*f)(struct kref*)) {assert(r->count>0);if(!--r->count)f(r);}
struct atomic_t {int value;};
static void atomic_set(struct atomic_t *a,int n) {a->value=n;}
static int atomic_read(struct atomic_t *a) {return a->value;}
static void atomic_inc(struct atomic_t *a) {a->value++;}
struct work_struct {void(*fn)(struct work_struct*);bool pending;};
struct delayed_work {struct work_struct work;};
static bool schedule_work(struct work_struct *w) {bool old=w->pending;w->pending=true;return !old;}
static bool schedule_delayed_work(struct delayed_work *w,int delay) {(void)delay;return schedule_work(&w->work);}
static void cancel_work_sync(struct work_struct *w) {w->pending=false;}
struct completion {bool done;};
static void reinit_completion(struct completion *c) {c->done=false;}
static void complete(struct completion *c) {c->done=true;}
static void complete_all(struct completion *c) {c->done=true;}
static int wait_for_completion_timeout(struct completion *c,int timeout) {(void)timeout;return c->done;}
struct device_node {int refs;};
struct device {struct device_node *of_node;};
struct of_phandle_args {struct device_node *np;unsigned args_count,args[1];};
struct pmic_glink_hdr {u32 owner,type,opcode;};
struct usbc_notify_ind_msg {struct pmic_glink_hdr hdr;u8 payload[16];u32 reserved;};
struct usbc_write_buffer_req_msg {struct pmic_glink_hdr hdr;u8 buf[8];u32 reserved;};
struct altmode_pan_ack_msg {u32 cmd_type;u8 port_index;};
struct altmode_client_data {u16 svid;const char *name;void *priv;int(*callback)(void*,void*,size_t);};
struct altmode_client;
struct idr {struct {int key;struct altmode_client *client;} entries[8];};
struct altmode_dev {struct kref ref;pthread_mutex_t client_lock,write_lock;struct idr client_idr;bool removing,response_pending,resync_required;unsigned long ack_ports;struct atomic_t pan_en_sent,link_generation;struct list_head client_list;struct completion response_received;struct delayed_work send_pan_en_work,send_pan_ack_work;void *pgclient;};
struct altmode_message {struct list_head node;u8 payload[16];};
struct altmode_client {struct kref ref;struct altmode_dev *amdev;struct altmode_client_data data;struct list_head c_node,messages;bool closing;u8 port_index;struct work_struct client_cb_work;};
static int live,allocation_step,allocation_fail,parse_error,transport_error,mode,write_calls,callback_count,injected;
static unsigned parse_port=1,parse_count=1;
static struct device_node node={1};
static struct altmode_dev *provider;
static int altmode_callback(void*,void*,size_t);
enum pmic_glink_state {PMIC_GLINK_STATE_DOWN,PMIC_GLINK_STATE_UP};
static void altmode_state_cb(void*,enum pmic_glink_state);
static void *kzalloc(size_t n,int gfp) {(void)gfp;if(++allocation_step==allocation_fail)return NULL;void*p=calloc(1,n);if(p)live++;return p;}
static char *kstrdup(const char *s,int gfp) {(void)gfp;if(++allocation_step==allocation_fail)return NULL;char*p=strdup(s);if(p)live++;return p;}
static void kfree(const void *p) {if(p){assert(live>0);live--;free((void*)p);}}
static void altmode_release(struct kref *r) {struct altmode_dev *p=container_of(r,struct altmode_dev,ref);pthread_mutex_destroy(&p->client_lock);pthread_mutex_destroy(&p->write_lock);kfree(p);provider=NULL;}
static struct altmode_dev *to_altmode_device(struct device_node *np) {assert(np==&node);if(!provider||provider->removing)return NULL;kref_get(&provider->ref);return provider;}
static int of_parse_phandle_with_args(struct device_node *np,const char *name,const char*cells,int i,struct of_phandle_args *out) {(void)np;(void)name;(void)cells;(void)i;if(parse_error)return parse_error;out->np=&node;node.refs++;out->args_count=parse_count;out->args[0]=parse_port;return 0;}
static void of_node_put(struct device_node *n) {assert(n&&n->refs>1);n->refs--;}
static struct altmode_client *idr_find(struct idr *d,int key) {for(int i=0;i<8;i++)if(d->entries[i].client&&d->entries[i].key==key)return d->entries[i].client;return NULL;}
static int idr_alloc(struct idr *d,struct altmode_client *c,int key,int end,int gfp) {(void)end;(void)gfp;if(++allocation_step==allocation_fail)return -ENOMEM;if(idr_find(d,key))return -ENOSPC;for(int i=0;i<8;i++)if(!d->entries[i].client){d->entries[i].key=key;d->entries[i].client=c;return key;}return -ENOSPC;}
static void idr_remove(struct idr *d,int key) {for(int i=0;i<8;i++)if(d->entries[i].client&&d->entries[i].key==key){d->entries[i].client=NULL;return;}}
static int pmic_glink_write(void *pgclient,void *data,size_t len) {struct altmode_dev *p=pgclient;assert(p->response_pending);assert(len==sizeof(struct usbc_write_buffer_req_msg));struct usbc_write_buffer_req_msg *request=data;assert(request->hdr.owner==MSG_OWNER_USBC_PAN&&request->hdr.type==1&&request->hdr.opcode==0x15&&!request->reserved);write_calls++;if(transport_error)return transport_error;if(mode==1)return 0;if(mode==2){altmode_state_cb(p,PMIC_GLINK_STATE_DOWN);altmode_state_cb(p,PMIC_GLINK_STATE_UP);}struct pmic_glink_hdr reply={MSG_OWNER_USBC_PAN,1,0x15};assert(!altmode_callback(p,&reply,sizeof(reply)));return 0;}
/* FUNCTIONS */
static void init_provider(void) {provider=kzalloc(sizeof(*provider),0);assert(provider);kref_init(&provider->ref);pthread_mutex_init(&provider->client_lock,NULL);pthread_mutex_init(&provider->write_lock,NULL);INIT_LIST_HEAD(&provider->client_list);provider->pgclient=provider;}
static int client_notify(void *priv,void *data,size_t len) {struct altmode_dev *p=priv;assert(len==16);u8 *bytes=data;assert(bytes[0]==1);assert(bytes[1]==(u8)callback_count);callback_count++;if(!injected){injected=1;struct usbc_notify_ind_msg m={{MSG_OWNER_USBC_PAN,1,0xff010016},{1,1},0};assert(!altmode_callback(p,&m,sizeof(m)));}assert(!altmode_send_ack(p,1));return 0;}
static void *thread_send(void *data) {struct altmode_client *c=data;for(int i=0;i<128;i++){u8 bytes[8]={0};assert(!altmode_send_data(c,bytes,sizeof(bytes)));}return NULL;}
int main(void) {
 int cases=0;struct device dev={&node};struct altmode_client_data cd={0xff01,"dp",NULL,client_notify};
 init_provider();cd.priv=provider;
 for(int f=1;f<=3;f++){allocation_step=0;allocation_fail=f;struct altmode_client *c=altmode_register_client(&dev,&cd);assert(IS_ERR_OR_NULL(c)&&PTR_ERR(c)==-ENOMEM&&live==1&&provider->ref.count==1&&node.refs==1);cases++;}
 allocation_fail=0;for(unsigned port=4;port<256;port++){parse_port=port;assert(PTR_ERR(altmode_register_client(&dev,&cd))==-EINVAL&&node.refs==1);cases++;}parse_port=1;parse_count=0;assert(PTR_ERR(altmode_register_client(&dev,&cd))==-EINVAL&&node.refs==1);parse_count=1;
 struct altmode_client *client=altmode_register_client(&dev,&cd);assert(!IS_ERR_OR_NULL(client)&&provider->ref.count==2);assert(PTR_ERR(altmode_register_client(&dev,&cd))==-ENOSPC);assert(live==3&&provider->ref.count==2);
 u8 bytes[8]={0};for(size_t len=0;len<12;len++){int ret=altmode_send_data(client,bytes,len);assert(ret==(len>8?-EINVAL:0));cases++;}assert(altmode_send_data(client,NULL,1)==-EINVAL);assert(!altmode_send_data(client,NULL,0));
 transport_error=-EIO;assert(altmode_send_data(client,bytes,8)==-EIO&&!provider->response_pending);transport_error=0;mode=1;assert(altmode_send_data(client,bytes,8)==-ETIMEDOUT&&provider->resync_required);int before=write_calls;assert(altmode_send_data(client,bytes,8)==-ENOTCONN&&write_calls==before);mode=0;altmode_state_cb(provider,PMIC_GLINK_STATE_UP);assert(!altmode_send_data(client,bytes,8));mode=2;assert(altmode_send_data(client,bytes,8)==-ENOTCONN);mode=0;
 struct usbc_notify_ind_msg msg={{MSG_OWNER_USBC_PAN,1,0xff010016},{1},0};for(size_t len=0;len<=40;len++){u8 b[40]={0};memcpy(b,&msg,sizeof(msg));int ret=altmode_callback(provider,b,len);assert(ret==(len==sizeof(msg)?0:-EINVAL));cases++;}client_cb_work(&client->client_cb_work);assert(callback_count==2);callback_count=injected=0;injected=1;
 for(int i=0;i<100;i++){msg.payload[1]=i;assert(!altmode_callback(provider,&msg,sizeof(msg)));}client_cb_work(&client->client_cb_work);assert(callback_count==100&&list_empty(&client->messages));
 msg.hdr.type=2;msg.payload[1]=100;assert(!altmode_callback(provider,&msg,sizeof(msg)));client_cb_work(&client->client_cb_work);assert(callback_count==101);msg.hdr.type=3;assert(altmode_callback(provider,&msg,sizeof(msg))==-EINVAL);msg.hdr.type=1;
 for(unsigned port=4;port<256;port++){msg.payload[0]=port;assert(altmode_callback(provider,&msg,sizeof(msg))==-EINVAL);cases++;}for(unsigned port=0;port<4;port++){if(port==1)continue;msg.payload[0]=port;assert(!altmode_callback(provider,&msg,sizeof(msg)));}assert(provider->ack_ports==13);
 pthread_t threads[4];for(int i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,thread_send,client));for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 msg.payload[0]=1;msg.payload[1]=100;assert(!altmode_callback(provider,&msg,sizeof(msg)));provider->removing=true;kref_put(&provider->ref,altmode_release);assert(provider&&live==4);assert(altmode_send_data(client,bytes,8)==-ENODEV);assert(altmode_callback(provider,&msg,sizeof(msg))==-ENODEV);assert(!altmode_deregister_client(client));assert(!provider&&!live&&node.refs==1);assert(altmode_send_data(ERR_PTR(-EINVAL),bytes,8)==-ENODEV);
 printf("{\"bounds_and_allocation_cases\":%d,\"fifo_messages\":103,\"serialized_concurrent_writes\":512,\"early_timeout_and_ssr_replies\":true,\"offline_handle_lifetime\":true}\n",cases);
}
