#!/usr/bin/env python3
"""Check real gauge transactions and single-owner WCN mux composition."""
from pathlib import Path
import hashlib,json,re,shutil,struct,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'audio_machine'))
from verify_smartpa import function
sys.path.insert(0,str(HERE.parent.parent))
from build_devicetrees import read_fdt
SOURCE='drivers/power/supply/mca/mca_hardware_ic/fuelgauge_ic/bq27z561/bq27z561.c'
fixture=r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
#define BQ_FG_REG_ALT_MAC 0
struct bq_fg_chip { int data_lock, i2c_rw_lock; u8 regs[1]; };
static u8 reply[36];
static int transport_error, locks;
static void mutex_lock(int *l) { assert(!*l); *l=1; locks++; }
static void mutex_unlock(int *l) { assert(*l); *l=0; locks--; }
static int fg_write_read_block(struct bq_fg_chip *bq,u8 reg,u8 *buf,u8 n,u8 m) {
 assert(bq->data_lock && bq->i2c_rw_lock); assert(n==2 && m==36);
 if(transport_error) return transport_error;
 memcpy(buf,reply,m);return 0;
}
/* SOURCE */
static void response(int payload) {
 memset(reply,0,sizeof(reply));reply[0]=0x4c;reply[1]=0;
 for(int i=0;i<payload;i++)reply[2+i]='A'+i;
 reply[35]=payload+4; reply[34]=checksum(reply,payload+2);
}
int main(void) {
 struct bq_fg_chip bq={0}; u8 out[34];
 for(int payload=3;payload<=32;payload++) {
  response(payload);memset(out,0xa5,sizeof(out));
  assert(fg_mac_read_block(&bq,0x4c,out+1,PROBE_LEN)==0);
  assert(out[0]==0xa5 && out[1+PROBE_LEN]==0xa5);
  assert(!memcmp(out+1,reply+2,PROBE_LEN)); assert(!locks);
 }
 for(int payload=0;payload<3;payload++) {
  response(payload);assert(fg_mac_read_block(&bq,0x4c,out,PROBE_LEN)==-EMSGSIZE);
 }
 for(int len=1;len<=32;len++) {
  response(len);assert(fg_mac_read_block(&bq,0x4c,out,len)==0);
  if(len>1) {response(len-1);assert(fg_mac_read_block(&bq,0x4c,out,len)==-EMSGSIZE);}
 }
 for(int len=0;len<=255;len++) {
  if(len>=4 && len<=36)continue;
  response(3);reply[35]=len;assert(fg_mac_read_block(&bq,0x4c,out,3)==-EMSGSIZE);
 }
 response(9);reply[34]^=1;assert(fg_mac_read_block(&bq,0x4c,out,3)==-EIO);
 response(9);reply[0]^=1;assert(fg_mac_read_block(&bq,0x4c,out,3)==-EIO);
 response(9);transport_error=-EREMOTEIO;assert(fg_mac_read_block(&bq,0x4c,out,3)==-EREMOTEIO);
 assert(!locks);assert(fg_mac_read_block(&bq,0x4c,NULL,3)==-EINVAL);
 assert(fg_mac_read_block(&bq,0x4c,out,0)==-EINVAL);
 assert(fg_mac_read_block(&bq,0x4c,out,33)==-EINVAL);
 puts("variable-length identity accepted; fixed-field bounds, checksum, echo and I2C failures enforced");
}
"""
def gauge():
 results=[]
 for version in ['candidate','before']:
  src=(HERE/version/SOURCE).read_text()
  probe=function(src,'bq_fg_probe')
  dtype,count=re.search(r'\b(char|u8) data\[(\d+)\];',probe).groups()
  assert 'fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, data, sizeof(data))' in probe
  code=fixture.replace('/* SOURCE */',function(src,'checksum')+'\n'+function(src,'fg_mac_read_block')).replace('PROBE_LEN',count)
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'check.c';p.write_text(code);exe=Path(tmp)/'check'
   subprocess.run([shutil.which('cc'),'-fsanitize=address,undefined','-g','-o',str(exe),str(p)],check=True,capture_output=True)
   r=subprocess.run([str(exe)],capture_output=True,text=True)
  assert (r.returncode==0)==(version=='candidate'),r.stderr
  results.append({'source':version,'probe_bytes':int(count),'exit':r.returncode,'result':r.stdout.strip()})
 src=(HERE/'candidate'/SOURCE).read_text()
 assert 'FFC_TERMINATION_BYTE + 1' in function(src,'fg_get_manufacture_data')
 assert 't_buf, 3)' in function(src,'fg_read_vendor')
 assert 'data, 5)' in function(src,'fg_get_device_name')
 assert 'char data[6] = { 0 };' in function(src,'fg_get_device_name')
 return results

def canonical(nodes):
 ph={struct.unpack('>I',v['phandle'])[0]:p for p,v in nodes.items() if 'phandle' in v}
 result={}
 for p,props in nodes.items():
  if p.startswith('/__'):continue
  result[p]={}
  for k,v in props.items():
   if k in ['phandle','linux,phandle']:continue
   if k.startswith('pinctrl-') and k!='pinctrl-names':
    v=tuple(ph[struct.unpack('>I',v[i:i+4])[0]] for i in range(0,len(v),4))
   result[p][k]=v
 return result

def dt(previous,current):
 receipts=[]
 for board in ['tuna.dtb','tuna7.dtb']:
  old=read_fdt(previous/'checked'/board);new=read_fdt(current/'checked'/board)
  tlmm='/soc/pinctrl@f000000';bt='/soc/bt_tuna';wifi='/soc/qcom,wcn7750'
  state=tlmm+'/wcn-shared-sw-ctrl'
  assert not any(k.startswith('pinctrl') for k in old[tlmm])
  assert new[tlmm]['pinctrl-names']==b'default\0'
  ph={struct.unpack('>I',v['phandle'])[0]:p for p,v in new.items() if 'phandle' in v}
  assert ph[struct.unpack('>I',new[tlmm]['pinctrl-0'])[0]]==state
  assert new[state+'/mux']==old[tlmm+'/icnss_sw_ctrl/mux']==old[tlmm+'/cnss_wlan_sw_ctrl/mux']
  assert new[bt]['pinctrl-1']==b'' and new[wifi]['pinctrl-0']==b''
  assert old[bt]['pinctrl-names']==new[bt]['pinctrl-names']
  assert old[wifi]['pinctrl-names']==new[wifi]['pinctrl-names']
  owners=[]
  for path,props in new.items():
   for key,value in props.items():
    if key.startswith('pinctrl-') and key!='pinctrl-names':
     for i in range(0,len(value),4):
      target=ph[struct.unpack('>I',value[i:i+4])[0]]
      if any(p.startswith(target+'/') and b'gpio80\0' in v.get('pins',b'') for p,v in new.items()):owners.append((path,key))
  assert owners==[(tlmm,'pinctrl-0')],owners
  a,b=canonical(old),canonical(new)
  changed={(p,k) for p in a for k in set(a[p])|set(b[p]) if a[p].get(k)!=b[p].get(k)}
  assert changed=={(tlmm,'pinctrl-names'),(tlmm,'pinctrl-0'),(bt,'pinctrl-1'),(wifi,'pinctrl-0')},changed
  assert set(b)-set(a)=={state,state+'/mux'}
  receipts.append({'board':board,'owners':owners,'property_changes':sorted(changed),'new_nodes':sorted(set(b)-set(a))})
 return receipts
if __name__=='__main__':
 report={'status':'PASS','gauge':gauge(),'dt':dt(Path(sys.argv[1]),Path(sys.argv[2]))}
 (HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
