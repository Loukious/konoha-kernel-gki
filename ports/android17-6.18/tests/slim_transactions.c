#include "slim_mock.h"
struct device_driver{void*of_match_table;const void*bus;struct module*owner;};struct device_node;struct ida{int unused;};struct idr{void*slot[256];unsigned cursor;};
#define dev_get_drvdata(d) ((d)->data)
#define dev_set_drvdata(d,p) ((d)->data=(p))
#define GENMASK(h,l) (((~0UL)<<(l))&((~0UL)>>(63-(h))))
#define SLIMBUS_NAME_SIZE 32
/* PUBLIC HEADER */
/* PRIVATE HEADER */
static int pm_votes,pm_ret,tx_ret,respond_now=1,response_len=4,transfers,idr_fault;
static u8 last_tid;static u8 response[32];
static struct slim_controller ctrl;
static int pm_runtime_resume_and_get(struct device*d){if(pm_ret<0)return pm_ret;pm_votes++;return pm_ret;}
static void pm_runtime_mark_last_busy(struct device*d){}
static void pm_runtime_put_autosuspend(struct device*d){assert(pm_votes>0);pm_votes--;}
static void*idr_find(struct idr*i,unsigned id){assert(id<256);return i->slot[id];}
static int idr_alloc_cyclic(struct idr*i,void*p,int start,int end,int f){if(idr_fault)return -ENOSPC;for(int n=0;n<end-start;n++){unsigned id=start+(i->cursor++%(end-start));if(!i->slot[id]){i->slot[id]=p;return id;}}return -ENOSPC;}
static void idr_remove(struct idr*i,unsigned id){assert(id<256);i->slot[id]=NULL;}
#define memcpy_fromio(d,s,n) memcpy(d,s,n)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define HZ 250
#define msecs_to_jiffies(n) (n)
#define DECLARE_COMPLETION_ONSTACK(n) struct completion n={0}
#define wait_for_completion_timeout(c,t) ((c)->done?1:0)
#define clamp(x,l,h) ((x)<(l)?(l):(x)>(h)?(h):(x))
static bool refcount_dec_and_test(refcount_t*r){assert(*r>0);return --*r==0;}
static int transfer(struct slim_controller*c,struct slim_msg_txn*t){transfers++;last_tid=t->tid;if(respond_now&&t->tid)slim_msg_response(c,response,t->tid,response_len);return tx_ret;}
/* FUNCTIONS */
static void reset(void){assert(!live&&!pm_votes);memset(&ctrl,0,sizeof(ctrl));ctrl.xfer_msg=transfer;pm_ret=tx_ret=idr_fault=0;respond_now=1;response_len=4;transfers=0;alloc_fault=0;memset(response,0x5a,sizeof(response));}
static void clean_idr(void){for(int id=0;id<256;id++)assert(!ctrl.tid_idr.slot[id]);}
int main(void){unsigned cases=0;u8 buffer[16];struct slim_device dev={.ctrl=&ctrl,.laddr=9};for(int len=0;len<33;len++){reset();response_len=len;memset(buffer,0xa5,sizeof(buffer));int ret=slim_read(&dev,0x800,4,buffer);assert(ret==(len==4?0:-EMSGSIZE));if(ret<0)for(int i=0;i<16;i++)assert(buffer[i]==0xa5);else{for(int i=0;i<len;i++)assert(buffer[i]==0x5a);for(int i=4;i<16;i++)assert(buffer[i]==0xa5);}assert(!pm_votes&&!live);clean_idr();cases++;}
reset();struct completion native_done={0};struct slim_val_inf native_msg={.num_bytes=4,.rbuf=buffer};struct slim_msg_txn native_txn={.mt=SLIM_MSG_MT_DEST_REFERRED_USER,.msg=&native_msg,.comp=&native_done,.reply_len=4};assert(!slim_alloc_txn_tid(&ctrl,&native_txn));memset(buffer,0xa5,sizeof(buffer));slim_msg_response(&ctrl,response,native_txn.tid,8);assert(native_done.done&&native_txn.result==-EMSGSIZE&&buffer[4]==0xa5);clean_idr();
for(int scenario=0;scenario<7;scenario++){reset();int expected=0;switch(scenario){case 0:pm_ret=-EIO;expected=-EIO;break;case 1:ctrl.sched.clk_state=SLIM_CLK_PAUSED;expected=-EHOSTDOWN;break;case 2:idr_fault=1;expected=-ENOSPC;break;case 3:tx_ret=-EAGAIN;respond_now=0;expected=-EAGAIN;break;case 4:tx_ret=-EIO;expected=-EIO;break;case 5:respond_now=0;expected=-ETIMEDOUT;break;case 6:pm_ret=1;break;}assert(slim_read(&dev,0x800,4,buffer)==expected);assert(!pm_votes&&!live);clean_idr();slim_msg_response(&ctrl,response,last_tid,4);assert(!pm_votes);cases++;}
for(int now=0;now<2;now++){reset();respond_now=now;struct completion done={0};struct slim_val_inf msg={.num_bytes=4,.rbuf=buffer,.comp=&done};assert(!slim_xfer_msg(&dev,&msg,SLIM_MSG_MC_REQUEST_VALUE));if(!now){assert(live==1&&pm_votes==1&&!done.done);memset(&msg,0,sizeof(msg));slim_msg_response(&ctrl,response,last_tid,4);}assert(done.done&&!live&&!pm_votes);clean_idr();cases++;}
reset();struct completion done={0};struct slim_val_inf msg={.num_bytes=4,.rbuf=buffer,.comp=&done};alloc_fault=alloc_step+1;assert(slim_xfer_msg(&dev,&msg,SLIM_MSG_MC_REQUEST_VALUE)==-ENOMEM&&!live&&!pm_votes);alloc_fault=0;respond_now=0;tx_ret=-EIO;assert(slim_xfer_msg(&dev,&msg,SLIM_MSG_MC_REQUEST_VALUE)==-EIO&&!live&&!pm_votes);clean_idr();
for(size_t count=0;count<300;count++){reset();if(count>=1&&count<=16)response_len=count;int ret=slim_read(&dev,0x800,count,buffer);assert(ret==(count>=1&&count<=16?0:-EINVAL));assert(!live&&!pm_votes);cases++;}reset();assert(slim_read(&dev,0xbff,2,buffer)==-EINVAL);assert(slim_write(&dev,0x10000,1,buffer)==-EINVAL);assert(!transfers);assert(!slim_writeb(&dev,0xbff,0x20)&&!pm_votes);printf("{\"transaction_cases\":%u,\"reply_bounds\":true,\"all_error_paths_cancel_tid_and_balance_pm\":true,\"async_immediate_and_delayed_ownership\":true,\"late_reply_ignored\":true,\"wrapper_truncation_rejected\":true}\n",cases);}
