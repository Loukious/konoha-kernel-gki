#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
typedef int atomic_t;
#define UART_XMIT_SIZE 4096
#define DMA_RX_BUF_SIZE 2048
#define NUM_RX_BUF 4
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define min(a,b) ((a)<(b)?(a):(b))
#define READ_ONCE(a) (a)
#define UART_PORT_CLOSED_SHUTDOWN 0
#define UART_PORT_OPEN 1
#define GENI_OUTPUT_CTRL 0x24
#define UART_LOG_DBG(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct kfifo { unsigned int in, out; u8 data[UART_XMIT_SIZE]; };
struct tty_port { struct kfifo xmit_fifo; };
struct uart_state { struct tty_port port; };
struct uart_port { struct uart_state *state; struct {unsigned int tx,rx;} icount; void *dev,*membase; bool stopped; };
struct pinctrl_state { int id; };
struct clk { int id, enabled; };
struct icc_path { int enabled, disabled; };
struct geni_se { struct {struct icc_path *path; unsigned int avg_bw;} icc_paths[3]; };
struct msm_geni_serial_rsc { void *geni_pinctrl; struct pinctrl_state *geni_gpio_active,*geni_gpio_sleep,*geni_gpio_shutdown; struct clk *se_clk,*m_ahb_clk,*s_ahb_clk; };
struct work_struct {int unused;};
struct dma_chan {int unused;};
struct uart_gsi {struct dma_chan *tx_c,*rx_c;};
struct msm_geni_serial_port {
 struct uart_port uport;
 struct geni_se se;
 struct msm_geni_serial_rsc serial_rsc;
 bool tx_mapped,removing,tx_cancel_pending,tx_pm_vote,clocks_on,icc_ready,gsi_rx_done;
 atomic_t flush_buffers,stop_rx_inprogress;
 unsigned int xmit_size,count;
 u8 *tx_buf;
 dma_addr_t tx_dma;
 u64 tx_generation,tx_active_generation;
 int port_state,xfer,tx_xfer;
 void *ipc_log_rx,*rx_gsi_buf[NUM_RX_BUF],*tx_wq;
 struct work_struct tx_cancel_work,rx_cancel_work,tx_xfer_work;
 struct uart_gsi *gsi;
};
struct msm_gpi_dma_async_tx_cb_param { void *userdata; unsigned int length; };
static int atomic_read(const atomic_t *v) {return *v;}
static void atomic_set(atomic_t *v,int n){*v=n;}
static unsigned int kfifo_len(const struct kfifo *f){return f->in-f->out;}
static bool kfifo_is_empty(const struct kfifo *f){return !kfifo_len(f);}
static unsigned int kfifo_out_peek(const struct kfifo *f,void *v,unsigned int n) {
 n=min(n,kfifo_len(f)); for(unsigned int i=0;i<n;i++) ((u8*)v)[i]=f->data[(f->out+i)%UART_XMIT_SIZE]; return n;
}
static void fifo_append(struct kfifo *f,const void *v,unsigned int n) {
 assert(kfifo_len(f)+n<=UART_XMIT_SIZE); for(unsigned int i=0;i<n;i++)f->data[(f->in++)%UART_XMIT_SIZE]=((const u8*)v)[i];
}
static void uart_xmit_advance(struct uart_port *u,unsigned int n){assert(n<=kfifo_len(&u->state->port.xmit_fifo));u->state->port.xmit_fifo.out+=n;u->icount.tx+=n;}
static unsigned int unmaps,last_unmap_size;static dma_addr_t last_unmap_addr;
static void geni_se_tx_dma_unprep(struct geni_se *s,dma_addr_t a,unsigned int n){(void)s;unmaps++;last_unmap_addr=a;last_unmap_size=n;}
static int operation,fail1,fail2,mmio,clock_balance,pin_state;
static bool fail(void){operation++;return operation==fail1||operation==fail2;}
static int clk_prepare_enable(struct clk *c){if(fail())return -EIO;c->enabled++;clock_balance++;return 0;}
static void clk_disable_unprepare(struct clk *c){assert(c->enabled==1);c->enabled--;clock_balance--;}
static int pinctrl_select_state(void *p,struct pinctrl_state *s){(void)p;assert(s);if(fail())return -EIO;pin_state=s->id;return 0;}
static int icc_enable(struct icc_path *p){p->enabled=1;return fail()?-EIO:0;}
static int icc_disable(struct icc_path *p){p->disabled++;p->enabled=0;return fail()?-EIO:0;}
static int icc_set_bw(struct icc_path *p,unsigned int a,unsigned int b){(void)p;(void)a;(void)b;return fail()?-EIO:0;}
static void geni_write_reg(unsigned int v,void *m,unsigned int o){(void)v;(void)m;(void)o;assert(clock_balance==3);mmio++;}
static void udelay(unsigned int x){(void)x;}
static unsigned int received,receive_limit=UINT_MAX,rx_queue,pushes;static u8 received_data[DMA_RX_BUF_SIZE];
static int tty_insert_flip_string(struct tty_port *t,const unsigned char *p,unsigned int n){(void)t;n=min(n,receive_limit);assert(n<=DMA_RX_BUF_SIZE);memcpy(received_data,p,n);received+=n;return n;}
static void tty_flip_buffer_push(struct tty_port *t){(void)t;pushes++;}
static void dump_ipc(struct uart_port *u,void *ipc,const char *tag,char *p,int off,int n){(void)u;(void)ipc;(void)tag;(void)p;(void)off;assert(n>=0&&n<=DMA_RX_BUF_SIZE);}
static void msm_geni_uart_rx_queue_dma_tre(int index,struct uart_port *u){(void)u;assert(index>=0&&index<NUM_RX_BUF);rx_queue++;}
static int lock_balance,terminate_error,terminations,queued,power_puts,wakeups;
static void uart_port_lock_irqsave(struct uart_port *u,unsigned long *f){(void)u;*f=0;assert(!lock_balance);lock_balance++;}
static void uart_port_unlock_irqrestore(struct uart_port *u,unsigned long f){(void)u;(void)f;assert(lock_balance==1);lock_balance--;}
static int dmaengine_terminate_sync(struct dma_chan *c){assert(c&&!lock_balance);terminations++;return terminate_error;}
static bool uart_tx_stopped(struct uart_port *u){return u->stopped;}
static bool queue_work(void *q,struct work_struct *w){(void)q;(void)w;queued++;return true;}
static void complete(int *c){(*c)++;}
static void msm_geni_serial_power_off(struct uart_port *u){(void)u;power_puts++;}
static void uart_write_wakeup(struct uart_port *u){(void)u;wakeups++;}
/* STAGED_DEFINITIONS */
static uint64_t snapshot_cases,packing_cases,power_cases,rx_cases,cancel_cases;
static u8 bytes[UART_XMIT_SIZE],snapshot[UART_XMIT_SIZE],appended[32];
static void test_snapshots(void){
 unsigned int lengths[]={0,1,3,31,127,511,4095,4096};
 for(unsigned int i=0;i<UART_XMIT_SIZE;i++) bytes[i]=(u8)(i*179+131);
 memset(appended,0xa5,sizeof(appended));
 for(unsigned int offset=0;offset<UART_XMIT_SIZE;offset++)for(unsigned int li=0;li<ARRAY_SIZE(lengths);li++)for(int mode=0;mode<4;mode++){
  unsigned int len=lengths[li]; struct uart_state state={0}; struct kfifo *f=&state.port.xmit_fifo;
  struct msm_geni_serial_port p={.uport.state=&state,.tx_buf=snapshot};
  f->in=f->out=offset;fifo_append(f,bytes,len);unmaps=0;
  unsigned int n=msm_geni_tx_snapshot(&p); assert(n==len&&kfifo_len(f)==len&&!memcmp(snapshot,bytes,n));
  if(n){
   p.tx_mapped=true;p.tx_dma=(mode&1)?(dma_addr_t)0:0x123456789ULL;p.xmit_size=n;
   assert(msm_geni_tx_snapshot(&p)==0);
   unsigned int extra=0;
   if(mode==1){extra=min((unsigned int)sizeof(appended),UART_XMIT_SIZE-len);fifo_append(f,appended,extra);}
   if(mode==2){p.tx_generation++;f->in=f->out=0;fifo_append(f,appended,sizeof(appended));extra=sizeof(appended);}
   if(mode==3)p.uport.state=NULL;
   assert(!memcmp(snapshot,bytes,n));
   msm_geni_tx_complete(&p);assert(unmaps==1&&last_unmap_size==n&&last_unmap_addr==((mode&1)?0:0x123456789ULL));
   assert(!p.tx_mapped&&p.xmit_size==0&&p.uport.icount.tx==n);
   assert(kfifo_len(f)==(mode==3?len:extra));
   msm_geni_tx_complete(&p);msm_geni_tx_unmap(&p);assert(unmaps==1&&p.uport.icount.tx==n);
  }
  snapshot_cases++;
 }
 struct uart_state state={0};struct msm_geni_serial_port p={.uport.state=&state,.tx_buf=snapshot};fifo_append(&state.port.xmit_fifo,bytes,32);
 p.removing=true;assert(!msm_geni_tx_snapshot(&p));p.removing=false;
 p.flush_buffers=1;assert(!msm_geni_tx_snapshot(&p));p.flush_buffers=0;
 p.tx_cancel_pending=true;assert(!msm_geni_tx_snapshot(&p));p.tx_cancel_pending=false;
 p.uport.state=NULL;assert(!msm_geni_tx_snapshot(&p));snapshot_cases+=4;
}
static void test_packing(void){
 for(unsigned int n=0;n<=4;n++)for(unsigned int pair=0;pair<=UINT16_MAX;pair++){
  u8 input[4]={(u8)pair,(u8)(pair>>8),(u8)~pair,(u8)(pair>>8)^0x80};u32 word=msm_geni_pack_fifo(input,n);
  for(unsigned int i=0;i<4;i++)assert(((word>>(i*8))&255)==(i<n?input[i]:0));packing_cases++;
 }
}
static void test_power(void){
 for(int f1=0;f1<24;f1++)for(int f2=0;f2<24;f2++){
  struct clk clocks[3]={{.id=0},{.id=1},{.id=2}};struct icc_path paths[3]={0};
  struct pinctrl_state active={1},sleep={2},shutdown={3};
  struct msm_geni_serial_port p={.icc_ready=true,.port_state=UART_PORT_OPEN,.serial_rsc={.se_clk=&clocks[0],.m_ahb_clk=&clocks[1],.s_ahb_clk=&clocks[2],.geni_gpio_active=&active,.geni_gpio_sleep=&sleep,.geni_gpio_shutdown=&shutdown}};
  for(int i=0;i<3;i++)p.se.icc_paths[i].path=&paths[i];
  operation=mmio=clock_balance=pin_state=0;fail1=f1;fail2=f2;
  int ret=msm_geni_serial_resources_on(&p);
  if(ret){assert(!p.clocks_on&&!clock_balance&&!mmio);}else{assert(p.clocks_on&&clock_balance==3&&mmio==1);int before=operation;assert(!msm_geni_serial_resources_on(&p)&&before==operation);}
  fail1=fail2=0;assert(!msm_geni_serial_resources_on(&p));assert(p.clocks_on&&clock_balance==3);
  operation=0;fail1=f1;fail2=f2;ret=msm_geni_serial_resources_off(&p);
  if(ret){assert(p.clocks_on&&clock_balance==3);}else{assert(!p.clocks_on&&!clock_balance);for(int i=0;i<3;i++)assert(!paths[i].enabled&&paths[i].disabled);}
  fail1=fail2=0;assert(!msm_geni_serial_resources_off(&p));assert(!p.clocks_on&&!clock_balance);for(int i=0;i<3;i++)assert(!paths[i].enabled);
  p.port_state=UART_PORT_CLOSED_SHUTDOWN;assert(!msm_geni_serial_resources_on(&p));assert(!msm_geni_serial_resources_off(&p));assert(pin_state==3);power_cases++;
 }
}
static void test_rx(void){
 unsigned int lengths[]={0,1,2,3,4,2047,2048,2049,UINT_MAX};
 u8 rxbuf[NUM_RX_BUF][DMA_RX_BUF_SIZE];for(unsigned int i=0;i<NUM_RX_BUF;i++)memset(rxbuf[i],0x81+i,DMA_RX_BUF_SIZE);
 for(unsigned int index=0;index<=NUM_RX_BUF;index++)for(unsigned int li=0;li<ARRAY_SIZE(lengths);li++)for(int mode=0;mode<5;mode++){
  struct uart_state state={0};struct msm_geni_serial_port p={.uport.state=mode==1?NULL:&state,.count=index,.removing=mode==2,.stop_rx_inprogress=mode==3};
  for(unsigned int i=0;i<NUM_RX_BUF;i++)p.rx_gsi_buf[i]=rxbuf[i];
  receive_limit=mode==4?13:UINT_MAX;received=rx_queue=pushes=0;
  struct msm_gpi_dma_async_tx_cb_param cb={.userdata=&p,.length=lengths[li]};msm_geni_uart_gsi_rx_cb(&cb);
  bool valid=index<NUM_RX_BUF&&lengths[li]<=DMA_RX_BUF_SIZE&&(mode==0||mode==4);
  if(valid){unsigned int n=min(lengths[li],receive_limit);assert(received==n&&p.uport.icount.rx==n&&rx_queue==1&&pushes==1&&p.count==(index+1)%NUM_RX_BUF);for(unsigned int i=0;i<n;i++)assert(received_data[i]==(u8)(0x81+index));}
  else assert(!received&&!rx_queue&&!pushes&&p.count==index);rx_cases++;
 }
}
static void test_cancel(void){
 for(int failure=0;failure<2;failure++)for(int populated=0;populated<2;populated++)for(int flushing=0;flushing<2;flushing++)for(int stopped=0;stopped<2;stopped++){
  struct uart_state state={0};struct dma_chan c={0};struct uart_gsi gsi={.tx_c=&c,.rx_c=&c};
  struct msm_geni_serial_port p={.uport={.state=&state,.stopped=stopped},.gsi=&gsi,.tx_buf=snapshot,.tx_mapped=true,.tx_dma=0,.xmit_size=12,.tx_cancel_pending=true,.tx_pm_vote=true,.flush_buffers=flushing,.stop_rx_inprogress=1,.gsi_rx_done=true};
  if(populated)fifo_append(&state.port.xmit_fifo,bytes,12);
  terminate_error=failure?-EIO:0;queued=power_puts=unmaps=terminations=lock_balance=0;
  msm_geni_uart_gsi_cancel_tx(&p.tx_cancel_work);
  assert(terminations==1&&!lock_balance);
  if(failure)assert(!unmaps&&p.tx_mapped&&p.tx_cancel_pending&&!power_puts);
  else {bool pending=populated&&!flushing&&!stopped;assert(unmaps==1&&!p.tx_mapped&&!p.tx_cancel_pending&&queued==pending&&power_puts==!pending);}
  msm_geni_uart_gsi_cancel_rx(&p.rx_cancel_work);
  if(failure)assert(p.stop_rx_inprogress&&p.gsi_rx_done&&!p.xfer);
  else assert(!p.stop_rx_inprogress&&!p.gsi_rx_done&&p.xfer==1);
  cancel_cases++;
 }
}
int main(void){test_snapshots();test_packing();test_power();test_rx();test_cancel();printf("{\"snapshot_cases\":%llu,\"packing_cases\":%llu,\"power_failure_pairs\":%llu,\"rx_cases\":%llu,\"cancel_cases\":%llu}\n",(unsigned long long)snapshot_cases,(unsigned long long)packing_cases,(unsigned long long)power_cases,(unsigned long long)rx_cases,(unsigned long long)cancel_cases);}
