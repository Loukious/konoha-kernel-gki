#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define __iomem
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define EXTCON_USB 1
#define EXTCON_CHG_USB_SDP 2
#define EXTCON_JIG 3
#define TTY_NORMAL 0
#define WAKEUP_CHARS 256
#define POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT 7
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define IS_ERR_OR_NULL(p) (!(p))
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef uint64_t phys_addr_t;typedef int irqreturn_t;
struct device {int ignored;};
struct extcon_dev {int state[4];};
struct work_struct {int queued;};
struct power_supply {int refs;};
struct clk {int enabled;};
struct kfifo {unsigned in,out;unsigned char buf[256];};
struct tty_port {struct kfifo xmit_fifo;};
struct uart_state {struct tty_port port;};
struct uart_port {struct uart_state *state;void *membase;unsigned char x_char;bool stopped;unsigned fifosize,type;struct {unsigned rx,tx,overrun;} icount;int locked;};
struct platform_device {struct device dev;void *data;};
struct kernel_param {void *arg;};
union power_supply_propval {int intval;};
static int eud_lock,eud_uart_driver,enable;
static struct platform_device *eud_private;
static unsigned fail_at,calls,loglen;static int ops[512];
static unsigned int mmio[0x2000/4],rx_data[256],tx_data[256],rx_pos,tx_pos,flip_count,wakeups,usb_events,chg_events,queue_count;
static int chg_value,secure_value,sysrq_byte=-1;
static struct power_supply psy;
static void (*event_hook)(void);
static int operation(int op) {assert(loglen<ARRAY_SIZE(ops));ops[loglen++]=op;calls++;return calls==fail_at?-71:0;}
static void observe(int op) {assert(loglen<ARRAY_SIZE(ops));ops[loglen++]=op;}
static void reset_trace(void) {calls=fail_at=loglen=0;}
static void mutex_lock(int *l) {assert(!*l);*l=1;}
static void mutex_unlock(int *l) {assert(*l);*l=0;}
static void uart_port_lock_irqsave(struct uart_port *p,unsigned long *f) {assert(!p->locked);p->locked=1;*f=0;}
static void uart_port_unlock_irqrestore(struct uart_port *p,unsigned long f) {assert(p->locked);p->locked=0;}
static int clk_prepare_enable(struct clk *c) {if(!c)return 0;int r=operation(10);if(!r)c->enabled++;return r;}
static void clk_disable_unprepare(struct clk *c) {if(c){assert(c->enabled>0);c->enabled--;observe(11);}}
static void *platform_get_drvdata(struct platform_device *p) {return p->data;}
static int qcom_scm_io_readl(phys_addr_t p,u32 *v) {int r=operation(12);if(!r)*v=secure_value;return r;}
static int qcom_scm_io_writel(phys_addr_t p,u32 v) {int r=operation(13);if(!r)secure_value=v;return r;}
static int extcon_set_state(struct extcon_dev *d,unsigned id,bool v) {d->state[id]=v;return 0;}
static int extcon_set_state_sync(struct extcon_dev *d,unsigned id,bool v) {int r=operation(14);if(!r){d->state[id]=v;if(id==EXTCON_USB)usb_events++;if(event_hook){void(*h)(void)=event_hook;event_hook=NULL;h();}}return r;}
static struct power_supply *power_supply_get_by_name(const char *name) {psy.refs++;return &psy;}
static void power_supply_put(struct power_supply *p) {assert(p->refs>0);p->refs--;observe(29);}
static int power_supply_set_property(struct power_supply *p,int key,const union power_supply_propval *v) {assert(p->refs>0);chg_events++;chg_value=v->intval;return 0;}
static int kstrtoint(const char *s,unsigned base,int *v) {char *end;errno=0;long n=strtol(s,&end,base);if(errno||end==s||n<INT_MIN||n>INT_MAX)return -EINVAL;if(*end=='\n')end++;if(*end)return -EINVAL;*v=n;return 0;}
static unsigned readl_relaxed(const void *p) {ptrdiff_t i=(const unsigned *)p-mmio;assert(i>=0&&i<ARRAY_SIZE(mmio));if(i==0x14/4){assert(rx_pos<ARRAY_SIZE(rx_data));return rx_data[rx_pos++];}return mmio[i];}
static unsigned char readb_relaxed(const void *p) {return readl_relaxed(p);}
static void writel_relaxed(u32 v,void *p) {ptrdiff_t i=(unsigned *)p-mmio;assert(i>=0&&i<ARRAY_SIZE(mmio));if(i==0x08/4){assert(tx_pos<ARRAY_SIZE(tx_data));assert(v<=255);tx_data[tx_pos++]=v;}else mmio[i]=v;}
static void writew_relaxed(u16 v,void *p) {writel_relaxed(v,p);}
#define wmb() ((void)0)
#define usleep_range(a,b) ((void)0)
#define udelay(a) ((void)0)
static int queue_work(void *q,struct work_struct *w) {queue_count++;w->queued=1;return 1;}
static void *system_freezable_wq;
static unsigned uart_fifo_get(struct uart_port *p,unsigned char *ch) {struct kfifo *f=&p->state->port.xmit_fifo;if(f->in==f->out)return 0;*ch=f->buf[f->out++%256];p->icount.tx++;return 1;}
static bool uart_tx_stopped(struct uart_port *p) {return p->stopped;}
static unsigned kfifo_len(struct kfifo *f) {return f->in-f->out;}
static bool kfifo_is_empty(struct kfifo *f) {return f->in==f->out;}
static void uart_write_wakeup(struct uart_port *p) {wakeups++;}
static int uart_handle_sysrq_char(struct uart_port *p,unsigned char ch) {return ch==sysrq_byte;}
static void uart_insert_char(struct uart_port *p,unsigned a,unsigned b,unsigned char ch,unsigned flag) {assert(p->state);}
static void tty_flip_buffer_push(struct tty_port *p) {flip_count++;}
static void disable_irq_wake(int irq) {observe(20);}
static void devm_free_irq(struct device *d,int irq,void *chip) {assert(!eud_private);observe(21);}
static void cancel_work_sync(struct work_struct *w) {w->queued=0;observe(22);}
static void uart_remove_one_port(int *driver,struct uart_port *p) {observe(23);}
static void device_init_wakeup(struct device *d,bool on) {assert(!on);observe(30);}
/* TYPES */
/* FUNCTIONS */
static struct eud_chip chip;
static struct platform_device pdev;
static struct uart_state state;
static struct extcon_dev extcon;
static struct clk refclk,ahbclk;
static void setup(void) {
 memset(&chip,0,sizeof(chip));memset(&pdev,0,sizeof(pdev));memset(&state,0,sizeof(state));memset(&extcon,0,sizeof(extcon));memset(mmio,0,sizeof(mmio));memset(&refclk,0,sizeof(refclk));memset(&ahbclk,0,sizeof(ahbclk));
 chip.dev=&pdev.dev;chip.extcon=&extcon;chip.eud_reg_base=mmio;chip.port.membase=mmio;chip.port.state=&state;chip.port.fifosize=14;chip.eud_clkref_clk=&refclk;chip.eud_ahb2phy_clk=&ahbclk;pdev.data=&chip;eud_private=&pdev;enable=0;secure_value=0;psy.refs=0;event_hook=NULL;
 rx_pos=tx_pos=flip_count=wakeups=usb_events=chg_events=queue_count=0;reset_trace();
}
static void inject_event(void) {mmio[EUD_REG_CTL_OUT_1/4]=BIT(4);usb_attach_detach(&chip);}
static int find_op(int op) {for(unsigned i=0;i<loglen;i++)if(ops[i]==op)return i;return -1;}
int main(void) {
 unsigned tx_cases=0,rx_cases=0,irq_cases=0,control_cases=0,cleanup_cases=0,mask_cases=0;
 for(unsigned start=0;start<256;start++)for(unsigned len=0;len<=32;len++)for(unsigned mode=0;mode<4;mode++) {
  setup();chip.uart_active=true;chip.port.stopped=mode&1;chip.port.x_char=mode&2?0xfd:0;struct kfifo *f=&state.port.xmit_fifo;f->out=start;f->in=start+len;
  for(unsigned i=0;i<len;i++)f->buf[(start+i)%256]=(unsigned char)(start+i);
  unsigned urgent=!!chip.port.x_char;unsigned sent=(mode&1)?0:(len<14-urgent?len:14-urgent);
  eud_uart_tx(&chip);assert(tx_pos==sent+urgent);assert(chip.port.icount.tx==sent+urgent);assert(kfifo_len(f)==len-sent);
  if(urgent)assert(tx_data[0]==0xfd);for(unsigned i=0;i<sent;i++)assert(tx_data[i+urgent]==(unsigned char)(start+i));tx_cases++;
 }
 for(unsigned active=0;active<2;active++)for(unsigned len=0;len<256;len++) {
  setup();chip.uart_active=active;mmio[EUD_REG_COM_RX_ID/4]=UART_ID;mmio[EUD_REG_COM_RX_LEN/4]=len;for(unsigned i=0;i<256;i++)rx_data[i]=i;
  if(!active)chip.port.state=NULL;eud_uart_rx(&chip);assert(rx_pos==(len>14?14:len));assert(chip.port.icount.rx==(active?rx_pos:0));assert(chip.port.icount.overrun==(len>14));assert(flip_count==active);rx_cases++;
 }
 for(unsigned mask=0;mask<32;mask++)for(unsigned status=0;status<64;status++) {
  setup();mmio[EUD_REG_INT1_EN_MASK/4]=mask;mmio[EUD_REG_INT_STATUS_1/4]=status;mmio[EUD_REG_CTL_OUT_1/4]=BIT(4)|BIT(6);unsigned handled=status&mask&31;
  assert(handle_eud_irq(1,&chip)==(handled?IRQ_HANDLED:IRQ_NONE));assert(chip.pending_events==(handled&(EUD_INT_VBUS|EUD_INT_CHGR)));eud_event_notifier(&chip.eud_work);assert(usb_events==!!(handled&EUD_INT_VBUS));assert(chg_events==!!(handled&EUD_INT_CHGR));assert(!chip.pending_events);if(chip.usb_psy)power_supply_put(chip.usb_psy);assert(!psy.refs);irq_cases++;
 }
 setup();chip.pending_events=EUD_INT_VBUS;chip.usb_attach=false;event_hook=inject_event;eud_event_notifier(&chip.eud_work);assert(usb_events==2&&extcon.state[EXTCON_USB]);irq_cases++;
 for(unsigned mask=0;mask<256;mask++)for(unsigned action=0;action<3;action++) {
  setup();mmio[EUD_REG_INT1_EN_MASK/4]=mask;
  if(action==0){eud_start_tx(&chip.port);assert(mmio[EUD_REG_INT1_EN_MASK/4]==(mask|EUD_INT_TX));}
  if(action==1){eud_stop_tx(&chip.port);assert(mmio[EUD_REG_INT1_EN_MASK/4]==(mask&~EUD_INT_TX));}
  if(action==2){eud_shutdown(&chip.port);assert(mmio[EUD_REG_INT1_EN_MASK/4]==(mask&~(EUD_INT_TX|EUD_INT_RX)));}mask_cases++;
 }
 for(unsigned secure=0;secure<2;secure++) {
  setup();chip.secure_eud_en=secure;assert(!enable_eud(&pdev));assert(chip.eud_enabled&&chip.maybe_enabled&&refclk.enabled==1);unsigned steps=calls;assert(!disable_eud(&pdev));assert(!chip.eud_enabled&&!chip.maybe_enabled&&!refclk.enabled);control_cases++;
  for(unsigned fail=1;fail<=steps;fail++) {
   setup();chip.secure_eud_en=secure;fail_at=fail;assert(enable_eud(&pdev)==-71);assert(!chip.eud_enabled);reset_trace();assert(!disable_eud(&pdev));msm_eud_cleanup(&pdev);assert(!refclk.enabled&&!chip.eud_enabled&&!chip.maybe_enabled);control_cases++;
  }
 }
 setup();chip.secure_eud_en=true;assert(!enable_eud(&pdev));reset_trace();fail_at=2;assert(disable_eud(&pdev)==-71);assert(!chip.eud_enabled&&chip.maybe_enabled&&refclk.enabled==1);reset_trace();assert(!enable_eud(&pdev)&&chip.eud_enabled);assert(!disable_eud(&pdev));control_cases++;
 setup();chip.secure_eud_en=true;fail_at=5;assert(enable_eud(&pdev)==-71&&chip.maybe_enabled);reset_trace();fail_at=2;assert(enable_eud(&pdev)==-71&&refclk.enabled==1);reset_trace();assert(!disable_eud(&pdev));control_cases++;
 setup();chip.removing=true;assert(eud_startup(&chip.port)==-ENODEV);mmio[EUD_REG_INT1_EN_MASK/4]=0xff;eud_start_tx(&chip.port);assert(mmio[EUD_REG_INT1_EN_MASK/4]==(0xff&~EUD_INT_ALL));control_cases++;
 const char *bad[]={"2","-1","1garbage","", "2147483648"};for(unsigned i=0;i<ARRAY_SIZE(bad);i++){setup();struct kernel_param kp={&enable};assert(param_eud_set(bad[i],&kp)==-EINVAL);assert(!calls);control_cases++;}
 setup();struct kernel_param kp={&enable};chip.suspended=true;assert(param_eud_set("1",&kp)==-EBUSY&&!enable);control_cases++;
 for(unsigned bits=0;bits<128;bits++) {
  setup();chip.irq_registered=bits&1;chip.irq_wake_enabled=bits&2;chip.uart_registered=bits&4;chip.ahb_enabled=bits&8;ahbclk.enabled=!!chip.ahb_enabled;chip.eud_clkref_enabled=bits&16;refclk.enabled=!!chip.eud_clkref_enabled;chip.eud_enabled=bits&32;chip.maybe_enabled=!!chip.eud_enabled;if(bits&64){chip.usb_psy=&psy;psy.refs=1;}
  msm_eud_cleanup(&pdev);assert(chip.stopped&&chip.removing&&!eud_private&&!refclk.enabled&&!ahbclk.enabled&&!psy.refs);assert(mmio[EUD_REG_INT1_EN_MASK/4]==0);assert(find_op(22)>=0);if(bits&1)assert(find_op(21)<find_op(22));if(bits&4)assert(find_op(22)<find_op(23));unsigned n=loglen;msm_eud_cleanup(&pdev);assert(n==loglen);cleanup_cases++;
 }
 printf("{\"tx_cases\":%u,\"rx_cases\":%u,\"irq_cases\":%u,\"irq_mask_cases\":%u,\"control_cases\":%u,\"cleanup_cases\":%u}\n",tx_cases,rx_cases,irq_cases,mask_cases,control_cases,cleanup_cases);
}
