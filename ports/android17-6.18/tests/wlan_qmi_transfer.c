#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define GFP_KERNEL 0
#define __exit
#define __rcu
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define unlikely(x) (x)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define msecs_to_jiffies(x) (x)
#define scnprintf snprintf
#define CNSS_MAX_FILE_SIZE (32*1024*1024)
#define CNSS_PLAT_IPC_MAX_USER 1
#define CNSS_PLAT_IPC_QMI_FILE_TXN_TIMEOUT 10000
#define CNSS_DAEMON_CFG_WAIT_RETRY 200
#define CNSS_DAEMON_CFG_WAIT_MS 50
#define QMI_INIT_RETRY_MAX_TIMES 240
#define QMI_INIT_RETRY_DELAY_MS 250
#define cnss_plat_ipc_err(...) ((void)0)
#define cnss_plat_ipc_dbg(...) ((void)0)
#define cnss_plat_ipc_info(...) ((void)0)
struct sockaddr_qrtr {unsigned short sq_family;u32 sq_node,sq_port;};
struct qmi_handle {int marker;};
struct qmi_txn {int marker;};
struct qmi_elem_info {int marker;};
struct qmi_response_type_v01 {unsigned short result,error;};
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&(n),&(n)}
static void list_add_tail(struct list_head *n,struct list_head *h) {n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n) {n->prev->next=n->next;n->next->prev=n->prev;}
#define list_for_each_entry_safe(p,n,h,m) for(struct list_head *_it=(h)->next,*_next; _it!=(h)&&((_next=_it->next),(p)=(void *)_it,(n)=(void *)_next,1); _it=_next)
#define wait_event(w,c) do {unsigned _wait=0;(void)(w);while(!(c)){usleep(1000);assert(++_wait<10000);}} while(0)
struct mutex {pthread_mutex_t native;};
typedef int wait_queue_head_t;
struct completion {atomic_int done;};
static void mutex_lock(struct mutex *m) {assert(!pthread_mutex_lock(&m->native));}
static void mutex_unlock(struct mutex *m) {assert(!pthread_mutex_unlock(&m->native));}
static void init_completion(struct completion *c) {c->done=0;}
static void complete(struct completion *c) {c->done=1;}
static void wake_up_all(wait_queue_head_t *w) {(void)w;}
struct srcu_struct {pthread_rwlock_t native;};
#define DEFINE_MUTEX(n) struct mutex n={PTHREAD_MUTEX_INITIALIZER}
#define DEFINE_STATIC_SRCU(n) static struct srcu_struct n={PTHREAD_RWLOCK_INITIALIZER}
static int srcu_read_lock(struct srcu_struct *s) {assert(!pthread_rwlock_rdlock(&s->native));return 0;}
static void srcu_read_unlock(struct srcu_struct *s,int idx) {(void)idx;assert(!pthread_rwlock_unlock(&s->native));}
static void synchronize_srcu(struct srcu_struct *s) {assert(!pthread_rwlock_wrlock(&s->native));assert(!pthread_rwlock_unlock(&s->native));}
#define srcu_dereference(p,s) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_access_pointer(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_dereference_protected(p,c) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define rcu_assign_pointer(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
#define RCU_INIT_POINTER(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
struct idr {void *entry[32];};
static int id_result,fail_alloc;
static atomic_int allocations;
static void *kzalloc(size_t n,int flags) {(void)flags;if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);allocations++;return p;}
static void kfree(void *p) {if(p)allocations--;free(p);}
static int idr_alloc_cyclic(struct idr *i,void *p,int start,int end,int flags) {(void)flags;assert(!start&&!end);if(id_result<0)return id_result;assert(id_result<32&&!i->entry[id_result]);i->entry[id_result]=p;return id_result;}
static void *idr_find(struct idr *i,int n) {return n>=0&&n<32?i->entry[n]:NULL;}
static void idr_remove(struct idr *i,int n) {assert(n>=0&&n<32&&i->entry[n]);i->entry[n]=NULL;}
#define idr_for_each_entry(i,p,n) for((n)=0;(n)<32;(n)++) if(((p)=(i)->entry[n]))
/* HEADERS */
/* STRUCTURES */
static struct cnss_plat_ipc_qmi_svc_ctx plat_ipc_qmi_svc;
static LIST_HEAD(cnss_plat_ipc_daemon_records);
static DEFINE_MUTEX(cnss_plat_ipc_user_lock);
DEFINE_STATIC_SRCU(cnss_plat_ipc_user_srcu);
static struct qmi_handle live_handle;
struct work_struct {int marker;};
static struct work_struct cnss_plat_ipc_init_work;
static int cnss_plat_ipc_qmi_ops,cnss_plat_ipc_qmi_req_handlers[1];
static int handle_init_error,server_error,initializations,releases,cancellations;
static int qmi_handle_init(struct qmi_handle *h,int size,void *ops,void *handlers) {(void)h;(void)size;(void)ops;(void)handlers;initializations++;return handle_init_error;}
static int qmi_add_server(struct qmi_handle *h,int id,int version,int instance) {(void)h;(void)id;(void)version;(void)instance;return server_error;}
static void qmi_handle_release(struct qmi_handle *h) {assert(h);releases++;}
static void cancel_work_sync(struct work_struct *w) {(void)w;cancellations++;}
static void cnss_plat_ipc_logging_deinit(void) {}
static void idr_destroy(struct idr *i) {for(int n=0;n<32;n++)assert(!i->entry[n]);}
static struct sockaddr_qrtr peer={42,7,9},foreign={42,7,10};
static int sends,send_result,auto_mode,ind_result;
static bool gate_send,inside_send,release_send;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv=PTHREAD_COND_INITIALIZER;
static pthread_t responder;
static bool responder_live;
static u32 response_len,response_index;
static u8 response_data[CNSS_PLAT_IPC_QMI_MAX_DATA_SIZE_V01];
static unsigned config_waits;
static void msleep(unsigned ms) {assert(ms==50||ms==250);config_waits++;}
static int qmi_send_response(struct qmi_handle *h,struct sockaddr_qrtr *sq,struct qmi_txn *t,int id,int max,struct qmi_elem_info *ei,void *message) {
 (void)t;(void)max;(void)ei;assert(h==&live_handle&&sq->sq_family==peer.sq_family);sends++;
 if(id==CNSS_PLAT_IPC_QMI_FILE_UPLOAD_RESP_V01) {struct cnss_plat_ipc_qmi_file_upload_resp_msg_v01 *r=message;response_len=r->seg_buf_len;response_index=r->seg_index;memcpy(response_data,r->seg_buf,response_len);}
 if(gate_send) {pthread_mutex_lock(&gate);inside_send=true;pthread_cond_broadcast(&gate_cv);while(!release_send)pthread_cond_wait(&gate_cv,&gate);pthread_mutex_unlock(&gate);}
 return send_result;
}
static int qmi_send_indication(struct qmi_handle *,struct sockaddr_qrtr *,int,int,struct qmi_elem_info *,void *);
static int wait_for_completion_timeout(struct completion *,int);
/* FUNCTIONS */
static void setup(void) {
 assert(!allocations);memset(&plat_ipc_qmi_svc,0,sizeof(plat_ipc_qmi_svc));assert(!pthread_mutex_init(&plat_ipc_qmi_svc.file_idr_lock.native,NULL));
 plat_ipc_qmi_svc.svc_hdl=&live_handle;for(int i=0;i<2;i++){plat_ipc_qmi_svc.qmi_client_ctx[i].client_connected=true;plat_ipc_qmi_svc.qmi_client_ctx[i].client_sq=peer;}
 id_result=0;fail_alloc=0;sends=send_result=ind_result=auto_mode=0;gate_send=inside_send=release_send=false;responder_live=false;
 handle_init_error=server_error=initializations=releases=cancellations=0;
}
static void teardown(void) {if(responder_live){assert(!pthread_join(responder,NULL));responder_live=false;}cnss_plat_ipc_clear_users();while(cnss_plat_ipc_daemon_records.next!=&cnss_plat_ipc_daemon_records){struct list_head *n=cnss_plat_ipc_daemon_records.next;struct cnss_plat_ipc_daemon_record *r=(void *)n;list_del(n);kfree(r);}assert(!allocations&&!plat_ipc_qmi_svc.active_files);assert(!pthread_mutex_destroy(&plat_ipc_qmi_svc.file_idr_lock.native));}
static struct cnss_plat_ipc_file_data *file(char *b,u32 capacity,u32 size) {
 struct cnss_plat_ipc_file_data *f=cnss_plat_ipc_init_file_data("file",b,capacity,size);assert(!IS_ERR_OR_NULL(f));f->peer=peer;f->peer_valid=true;return f;
}
static void upload(struct cnss_plat_ipc_file_data *f,u32 index,struct sockaddr_qrtr *sq) {struct cnss_plat_ipc_qmi_file_upload_req_msg_v01 r={f->id,index};cnss_plat_ipc_qmi_file_upload_req_handler(&live_handle,sq,NULL,&r);}
static void download(struct cnss_plat_ipc_file_data *f,u32 total,u32 index,u32 len,u8 end,struct sockaddr_qrtr *sq) {
 struct cnss_plat_ipc_qmi_file_download_req_msg_v01 *r=calloc(1,sizeof(*r));assert(r);r->file_id=f->id;r->file_size=total;r->seg_index=index;r->seg_buf_len=len;r->end=end;memset(r->seg_buf,0x5a,sizeof(r->seg_buf));cnss_plat_ipc_qmi_file_download_req_handler(&live_handle,sq,NULL,r);free(r);
}
struct automatic {u32 id,size;int mode;};
static void *automatic_reply(void *arg) {
 struct automatic *a=arg;
 if(a->mode==1){for(u32 i=0;i<(a->size+61439)/61440;i++){struct cnss_plat_ipc_qmi_file_upload_req_msg_v01 r={a->id,i};cnss_plat_ipc_qmi_file_upload_req_handler(&live_handle,&peer,NULL,&r);}}
 else {struct cnss_plat_ipc_qmi_file_download_req_msg_v01 r={.file_id=a->id,.file_size=7,.end=1,.seg_buf_len=7};memcpy(r.seg_buf,"payload",7);cnss_plat_ipc_qmi_file_download_req_handler(&live_handle,&peer,NULL,&r);}
 free(a);return NULL;
}
static int qmi_send_indication(struct qmi_handle *h,struct sockaddr_qrtr *sq,int id,int max,struct qmi_elem_info *ei,void *message) {
 (void)max;(void)ei;assert(h==&live_handle&&sq->sq_node==peer.sq_node);
 if(ind_result<0)return ind_result;
 if(auto_mode){struct automatic *a=calloc(1,sizeof(*a));assert(a);a->mode=auto_mode;if(id==CNSS_PLAT_IPC_QMI_FILE_UPLOAD_IND_V01){struct cnss_plat_ipc_qmi_file_upload_ind_msg_v01 *r=message;a->id=r->file_id;a->size=r->file_size;assert(!strcmp(r->file_name,"file"));}else{struct cnss_plat_ipc_qmi_file_download_ind_msg_v01 *r=message;a->id=r->file_id;}
 assert(!pthread_create(&responder,NULL,automatic_reply,a));responder_live=true;}
 return 0;
}
static int wait_for_completion_timeout(struct completion *c,int timeout) {
 assert(timeout==10000);if(!responder_live)return 0;assert(!pthread_join(responder,NULL));responder_live=false;return c->done;
}
static void allocation_cases(void) {
 setup();char b[8];fail_alloc=1;assert(PTR_ERR(cnss_plat_ipc_init_file_data("f",b,8,8))==-ENOMEM);fail_alloc=0;
 int errors[]={-ENOMEM,-ENOSPC,-EINVAL,-EAGAIN};for(int i=0;i<4;i++){id_result=errors[i];assert(PTR_ERR(cnss_plat_ipc_init_file_data("f",b,8,8))==errors[i]&&!allocations);}
 id_result=0;plat_ipc_qmi_svc.stopping=true;assert(PTR_ERR(cnss_plat_ipc_init_file_data("f",b,8,8))==-ESHUTDOWN);teardown();
}
static unsigned upload_cases(void) {
 unsigned n=0;char *b=malloc(61441);assert(b);memset(b,0x6b,61441);
 for(int mode=0;mode<8;mode++) {setup();struct cnss_plat_ipc_file_data *f=file(b,61441,61441);if(mode==0){upload(f,0,&foreign);assert(!sends&&!f->offset&&f->status==-EINPROGRESS);}
 else if(mode==1){f->upload=false;upload(f,0,&peer);assert(!sends&&!f->offset);}
 else if(mode==2){upload(f,1,&peer);assert(f->status==-EPROTO&&!sends);}
 else if(mode==3){fail_alloc=1;upload(f,0,&peer);fail_alloc=0;assert(f->status==-ENOMEM&&!f->offset);}
 else if(mode==4){send_result=-EIO;upload(f,0,&peer);assert(f->status==-EIO&&!f->offset);}
 else if(mode==5){upload(f,0,&peer);assert(response_len==61440&&!response_index&&response_data[0]==0x6b&&f->offset==61440&&f->status==-EINPROGRESS);upload(f,1,&peer);assert(response_len==1&&response_index==1&&f->status==0&&f->offset==61441);upload(f,2,&peer);assert(sends==2);}
 else if(mode==6){f->peer_valid=false;upload(f,0,&peer);assert(!sends);}
 else {f->id=0;struct cnss_plat_ipc_qmi_file_upload_req_msg_v01 r={UINT32_MAX,0};cnss_plat_ipc_qmi_file_upload_req_handler(&live_handle,&peer,NULL,&r);assert(!sends);}
 int status=f->status;assert(cnss_plat_ipc_deinit_file_data(f)==status);teardown();n++;}
 free(b);return n;
}
static unsigned download_cases(void) {
 unsigned n=0;char b[16];
 for(unsigned mode=0;mode<14;mode++){setup();memset(b,0xcc,sizeof(b));struct cnss_plat_ipc_file_data *f=file(b,sizeof(b),0);
 switch(mode){case 0:download(f,8,0,8,1,&foreign);assert(!sends&&f->status==-EINPROGRESS);break;
 case 1:f->upload=true;download(f,8,0,8,1,&peer);assert(!sends);break;
 case 2:download(f,17,0,8,1,&peer);assert(f->status==-EPROTO);break;
 case 3:download(f,8,0,8,2,&peer);assert(f->status==-EPROTO);break;
 case 4:download(f,8,1,8,1,&peer);assert(f->status==-EPROTO);break;
 case 5:download(f,8,0,9,1,&peer);assert(f->status==-EPROTO);break;
 case 6:download(f,8,0,0,0,&peer);assert(f->status==-EPROTO);break;
 case 7:download(f,8,0,8,0,&peer);assert(f->status==-EPROTO);break;
 case 8:download(f,8,0,4,1,&peer);assert(f->status==-EPROTO);break;
 case 9:send_result=-ENOSPC;download(f,8,0,8,1,&peer);assert(f->status==-ENOSPC&&!f->offset&&b[0]==(char)0xcc);break;
 case 10:download(f,8,0,4,0,&peer);assert(f->offset==4);download(f,9,1,4,1,&peer);assert(f->status==-EPROTO&&f->offset==4);break;
 case 11:download(f,8,0,4,0,&peer);download(f,8,1,4,1,&peer);assert(!f->status&&f->offset==8&&b[7]==0x5a&&b[8]==(char)0xcc);break;
 case 12:download(f,0,0,0,1,&peer);assert(!f->status&&!f->offset);break;
 default:download(f,8,0,UINT32_MAX,1,&peer);assert(f->status==-EPROTO);break;}
 int status=f->status;assert(cnss_plat_ipc_deinit_file_data(f)==status);teardown();n++;}
 return n;
}
static unsigned api_cases(void) {
 unsigned n=0;char b[16];
 for(int mode=0;mode<7;mode++){setup();u32 size=sizeof(b);memset(b,0xaa,sizeof(b));int ret;
 if(mode==0){ret=cnss_plat_ipc_qmi_file_download(1,"file",b,&size);assert(ret==-ETIMEDOUT&&size==16);}
 else if(mode==1){ind_result=-EIO;ret=cnss_plat_ipc_qmi_file_download(1,"file",b,&size);assert(ret==-EIO&&size==16);}
 else if(mode==2){auto_mode=2;ret=cnss_plat_ipc_qmi_file_download(1,"file",b,&size);assert(!ret&&size==7&&!memcmp(b,"payload",7));}
 else if(mode==3){auto_mode=1;ret=cnss_plat_ipc_qmi_file_upload(1,"file",(u8 *)b,16);assert(!ret);}
 else if(mode==4){auto_mode=2;send_result=-EIO;ret=cnss_plat_ipc_qmi_file_download(1,"file",b,&size);assert(ret==-EIO&&size==16&&b[0]==(char)0xaa);}
 else if(mode==5){plat_ipc_qmi_svc.qmi_client_ctx[1].client_connected=false;ret=cnss_plat_ipc_qmi_file_upload(1,"file",(u8 *)b,16);assert(ret==-ENOTCONN);}
 else {ret=cnss_plat_ipc_qmi_file_download(-1,"file",b,&size);assert(ret==-EINVAL);assert(cnss_plat_ipc_qmi_file_download(1,"file",b,NULL)==-EINVAL);}
 teardown();n++;}return n;
}
static void *racing_handler(void *arg){upload(arg,0,&peer);return NULL;}
static atomic_bool freed;
static void *racing_deinit(void *arg){assert(!cnss_plat_ipc_deinit_file_data(arg));atomic_store(&freed,true);return NULL;}
static void race_case(void){setup();char b[8]={0};struct cnss_plat_ipc_file_data *f=file(b,8,8);gate_send=true;atomic_store(&freed,false);pthread_t a,d;assert(!pthread_create(&a,NULL,racing_handler,f));pthread_mutex_lock(&gate);while(!inside_send)pthread_cond_wait(&gate_cv,&gate);pthread_mutex_unlock(&gate);assert(!pthread_create(&d,NULL,racing_deinit,f));usleep(10000);assert(!atomic_load(&freed));pthread_mutex_lock(&gate);release_send=true;pthread_cond_broadcast(&gate_cv);pthread_mutex_unlock(&gate);assert(!pthread_join(a,NULL)&&!pthread_join(d,NULL));assert(atomic_load(&freed));teardown();}
static void disconnect_case(void){setup();char b[8];struct cnss_plat_ipc_file_data *a=file(b,8,8);id_result=1;struct cnss_plat_ipc_file_data *other=file(b,8,8);other->peer=foreign;cnss_plat_ipc_qmi_disconnect_cb(&live_handle,peer.sq_node,peer.sq_port);assert(a->status==-ENOTCONN&&other->status==-EINPROGRESS);assert(cnss_plat_ipc_deinit_file_data(a)==-ENOTCONN);assert(cnss_plat_ipc_deinit_file_data(other)==-EINPROGRESS);teardown();}
static bool cb_inside,cb_release;
static atomic_bool withdrew;
static void user_callback(void *arg,bool connected) {assert(arg==&cb_inside&&connected);assert(cnss_plat_ipc_register(1,user_callback,arg)==-ENOSPC);pthread_mutex_lock(&gate);cb_inside=true;pthread_cond_broadcast(&gate_cv);while(!cb_release)pthread_cond_wait(&gate_cv,&gate);pthread_mutex_unlock(&gate);}
static void *notify_user(void *arg) {(void)arg;cnss_plat_ipc_qmi_update_user(1);return NULL;}
static void *withdraw_user(void *arg) {cnss_plat_ipc_unregister(1,arg);atomic_store(&withdrew,true);return NULL;}
static void callback_case(void) {
 setup();assert(cnss_plat_ipc_register(-1,user_callback,&cb_inside)==-EINVAL);assert(cnss_plat_ipc_register(1,NULL,&cb_inside)==-EINVAL);fail_alloc=1;assert(cnss_plat_ipc_register(1,user_callback,&cb_inside)==-ENOMEM);fail_alloc=0;
 cb_inside=cb_release=false;atomic_store(&withdrew,false);assert(!cnss_plat_ipc_register(1,user_callback,&cb_inside));cnss_plat_ipc_unregister(1,&cb_release);assert(allocations==1);
 pthread_t a,b;assert(!pthread_create(&a,NULL,notify_user,NULL));pthread_mutex_lock(&gate);while(!cb_inside)pthread_cond_wait(&gate_cv,&gate);pthread_mutex_unlock(&gate);assert(!pthread_create(&b,NULL,withdraw_user,&cb_inside));usleep(10000);assert(!atomic_load(&withdrew));pthread_mutex_lock(&gate);cb_release=true;pthread_cond_broadcast(&gate_cv);pthread_mutex_unlock(&gate);assert(!pthread_join(a,NULL)&&!pthread_join(b,NULL));assert(atomic_load(&withdrew)&&!allocations);plat_ipc_qmi_svc.stopping=true;assert(cnss_plat_ipc_register(1,user_callback,&cb_inside)==-ESHUTDOWN);teardown();
}
static void config_case(void) {
 setup();struct cnss_plat_ipc_qmi_init_setup_req_msg_v01 r={1,2,3};send_result=-EIO;cnss_plat_ipc_qmi_init_setup_req_handler(&live_handle,&peer,NULL,&r);config_waits=0;assert(!cnss_plat_ipc_qmi_daemon_config()&&config_waits==200&&!allocations);
 send_result=0;cnss_plat_ipc_qmi_init_setup_req_handler(&live_handle,&peer,NULL,&r);struct cnss_plat_ipc_daemon_config *old=cnss_plat_ipc_qmi_daemon_config();assert(old&&old->initialized&&old->cal_file_available_bitmask==3);
 r.cal_file_available_bitmask=8;cnss_plat_ipc_qmi_init_setup_req_handler(&live_handle,&peer,NULL,&r);struct cnss_plat_ipc_daemon_config *current=cnss_plat_ipc_qmi_daemon_config();assert(current!=old&&current->cal_file_available_bitmask==8&&old->cal_file_available_bitmask==3);int before=allocations;cnss_plat_ipc_qmi_init_setup_req_handler(&live_handle,&foreign,NULL,&r);assert(allocations==before&&cnss_plat_ipc_qmi_daemon_config()==current);
 cnss_plat_ipc_qmi_disconnect_cb(&live_handle,peer.sq_node,peer.sq_port);assert(!cnss_plat_ipc_qmi_daemon_config()&&old->cal_file_available_bitmask==3);teardown();
}
static void *exit_file_owner(void *arg) {struct cnss_plat_ipc_file_data *f=arg;while(!atomic_load(&f->complete.done))usleep(100);assert(cnss_plat_ipc_deinit_file_data(f)==-ESHUTDOWN);return NULL;}
static void service_cases(void) {
 for(int mode=0;mode<5;mode++){setup();plat_ipc_qmi_svc.svc_hdl=NULL;if(mode==0)fail_alloc=1;else if(mode==1)handle_init_error=-EAGAIN;else if(mode==2)server_error=-EIO;else if(mode==3)plat_ipc_qmi_svc.stopping=true;
 cnss_plat_ipc_init_fn(NULL);fail_alloc=0;if(mode<4){assert(!plat_ipc_qmi_svc.svc_hdl&&!allocations);if(mode==1)assert(initializations==241);if(mode==2)assert(releases==1);}else{assert(plat_ipc_qmi_svc.svc_hdl&&allocations==1);assert(!cnss_plat_ipc_register(1,user_callback,&cb_inside));cnss_plat_ipc_qmi_svc_exit();assert(!allocations&&!plat_ipc_qmi_svc.svc_hdl&&releases==1&&cancellations==1);}teardown();}
 setup();plat_ipc_qmi_svc.svc_hdl=kzalloc(sizeof(struct qmi_handle),0);char buf[8];struct cnss_plat_ipc_file_data *f=file(buf,8,8);pthread_t owner;assert(!pthread_create(&owner,NULL,exit_file_owner,f));cnss_plat_ipc_qmi_svc_exit();assert(!pthread_join(owner,NULL));assert(!allocations&&!plat_ipc_qmi_svc.active_files);teardown();
}
int main(void){allocation_cases();unsigned up=upload_cases(),down=download_cases(),api=api_cases();for(int i=0;i<100;i++)race_case();disconnect_case();for(int i=0;i<100;i++)callback_case();config_case();service_cases();printf("{\"upload_cases\":%u,\"download_cases\":%u,\"api_cases\":%u,\"handler_deinit_races\":100,\"allocation_and_idr_faults\":6,\"disconnect_peer_isolation\":true,\"callback_withdrawal_races\":100,\"immutable_config_and_publication_cases\":5,\"service_init_and_active_file_shutdown_cases\":6}\n",up,down,api);}
