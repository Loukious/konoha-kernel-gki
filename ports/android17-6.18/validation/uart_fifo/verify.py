#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,re,shutil,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'audio_machine'))
from verify_smartpa import function
SRC='drivers/tty/serial/msm_geni_serial.c'
fixture=r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
#define __iomem
#define RX_FIFO_DEPTH_MSK_256_BYTES (255u<<16)
#define RX_FIFO_DEPTH_SHFT 16
#define DEF_FIFO_DEPTH_WORDS 16
#define SE_GENI_RX_FIFOn 0
#define GET_DEV_PORT(p) ((p)->private_data)
#define dev_err(...) ((void)0)
#define dev_err_ratelimited(...) ((void)0)
#define dump_ipc(...) ((void)0)
struct tty_port { int unused; };
struct uart_state { struct tty_port port; };
struct uart_port { void *dev; unsigned char *membase; struct uart_state *state; void *private_data; unsigned int fifosize; struct { unsigned int rx; } icount; };
struct msm_geni_serial_port { struct uart_port uport; unsigned int tx_fifo_depth,tx_fifo_width,rx_fifo_depth; unsigned int *rx_fifo; int se; void *ipc_log_rx; };
static unsigned int hw_depth=64,hw_width=32,reads,inserted,allocated;
static u32 geni_se_get_tx_fifo_depth(int *s) { return hw_depth; }
static u32 geni_se_get_tx_fifo_width(int *s) { return hw_width; }
static u32 geni_se_get_rx_fifo_depth(int *s) { return hw_depth; }
static void ioread32_rep(void *addr,void *buf,int count) {
 assert(count<=allocated);reads++;
 for(int i=0;i<count;i++)((u32*)buf)[i]=0x41414141;
}
static int tty_insert_flip_string(struct tty_port *p,const unsigned char *bytes,int count) {
 assert(count>=0 && count<=allocated*4);
 for(int i=0;i<count;i++)assert(bytes[i]=='A');inserted=count;return count;
}
static void tty_flip_buffer_push(struct tty_port *p) {}
/* SOURCE */
int main(void) {
 struct uart_state state={0};unsigned char mmio[4]={0};struct msm_geni_serial_port p={0};
 p.uport.state=&state;p.uport.membase=mmio;p.uport.private_data=&p;
 allocated=ALLOCATION;p.rx_fifo=calloc(allocated,sizeof(u32));assert(p.rx_fifo);
 for(int depth=4;depth<=255;depth++) {
  hw_depth=depth;hw_width=32;assert(get_tx_fifo_size(&p)==0);
  assert(p.rx_fifo_depth==depth && p.uport.fifosize==depth*4);
  for(int count=1;count<=depth;count++) {
   for(int last=0;last<=4;last++) {
    unsigned int before=p.uport.icount.rx;
    int bytes=count*4-(last?4-last:0);
    assert(handle_rx_hs(&p.uport,count,last,1,false)==bytes);
    assert(p.uport.icount.rx-before==bytes && inserted==bytes);
   }
  }
  reads=0;assert(handle_rx_hs(&p.uport,depth+1,0,0,false)==-EIO);assert(!reads);
 }
 reads=0;assert(handle_rx_hs(&p.uport,0,0,0,false)==0);assert(!reads);
 assert(handle_rx_hs(&p.uport,1,5,1,false)==-EIO);assert(!reads);
 assert(handle_rx_hs(&p.uport,256,0,0,false)==-EIO);assert(!reads);
 assert(handle_rx_hs(&p.uport,1,0,0,true)==0);assert(reads==1);
 for(int depth=0;depth<4;depth++) {hw_depth=depth;assert(get_tx_fifo_size(&p)!=0);}
 hw_depth=256;assert(get_tx_fifo_size(&p)==-ENXIO);
 hw_depth=64;
 for(int width=0;width<40;width++) {
  hw_width=width;assert((get_tx_fifo_size(&p)==0)==(width>0 && width<=32 && width%8==0));
 }
 assert(get_tx_fifo_size(NULL)==-ENODEV);free(p.rx_fifo);
 puts("hardware FIFO sizes, receive bounds and corrupt statuses passed");
}
"""
results=[]
for variant in ['candidate','before','small_buffer','missing_status_guard']:
 version='before' if variant=='before' else 'candidate'
 text=(HERE/version/SRC).read_text()
 define=re.search(r'^#define MAX_RX_FIFO_DEPTH_WORDS[^\n]*',text,re.M)
 body=(define[0]+'\n' if define else '')+function(text,'get_tx_fifo_size')+'\n'+function(text,'handle_rx_hs')
 alloc=re.search(r'port->rx_fifo = devm_kzalloc\(uport->dev, \(([^;\n]+) \* sizeof\(u32\)\)',function(text,'msm_geni_serial_port_init'))[1].replace('port->rx_fifo_depth','DEF_FIFO_DEPTH_WORDS')
 if variant=='small_buffer':alloc='DEF_FIFO_DEPTH_WORDS'
 if variant=='missing_status_guard':body=body.replace('rx_fifo_wc > port->rx_fifo_depth ||','false ||').replace('rx_fifo_wc > MAX_RX_FIFO_DEPTH_WORDS ||','false ||')
 code=fixture.replace('/* SOURCE */',body).replace('ALLOCATION',alloc)
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'test.c';p.write_text(code);exe=Path(tmp)/'test'
  subprocess.run([shutil.which('cc'),'-fsanitize=address,undefined','-g','-O1',str(p),'-o',str(exe)],check=True,capture_output=True)
  r=subprocess.run([str(exe)],capture_output=True,text=True)
 assert (r.returncode==0)==(variant=='candidate'),(variant,r.stderr)
 results.append({'variant':variant,'exit':r.returncode,'output':r.stdout.strip()})
report={'status':'PASS','cases':'all 252 supported depths, every receive count and trailing-byte length; invalid depths/widths, empty/drop paths, and three compiled regressions','results':results,'source_sha256':hashlib.sha256((HERE/'candidate'/SRC).read_bytes()).hexdigest()}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
