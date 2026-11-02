#!/usr/bin/env python3
"""Exercise optional fuel-gauge type and battery monitor resource parsing."""
from pathlib import Path
import json,hashlib,subprocess
H=Path(__file__).resolve().parent;W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
def fn(s,sig):
 start=s.index(sig);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
a=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#define MAX_BTB 2
#define MCA_BMD_DUAL_FG 1
#define ADC_SCHEME 0
#define GPIO_SCHEME 1
#define INT_SCHEME 2
#define IIC_SCHEME 3
#define GPIOF_IN 0
#define EPROBE_DEFER 517
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
#define PTR_ERR(x) ((int)(intptr_t)(x))
struct device_node {bool present;int cells,fg,scheme[2];};
struct device {struct device_node *of_node;};
struct mca_bmd_dev {struct device *dev;int fg_type;struct {int scheme,gpio;void *channel;} bmd_scheme[MAX_BTB];};
static int gpio_error,adc_error,requests;
static bool of_property_present(struct device_node *n,const char *prop){assert(!strcmp(prop,"fg_type"));return n->present;}
static int of_property_count_u32_elems(struct device_node *n,const char *p){return !strcmp(p,"fg_type")?n->cells:2;}
static int mca_parse_dts_u32_array(struct device_node *n,const char *p,int *a,int size){assert(!strcmp(p,"btb_bmd_scheme")&&size==2);memcpy(a,n->scheme,sizeof(n->scheme));return 0;}
static int mca_parse_dts_u32(struct device_node *n,const char *p,int *v,int def){assert(!strcmp(p,"fg_type"));if(!n->present||n->cells<1){*v=def;return -EINVAL;}*v=n->fg;return 0;}
static int of_get_named_gpio(struct device_node *n,const char *p,int i){(void)n;assert(!strcmp(p,"btb_gpio")&&i==0);return gpio_error?gpio_error:15;}
static int devm_gpio_request_one(struct device *d,int gpio,int flags,const char *name){(void)d;assert(gpio==15&&flags==GPIOF_IN&&!strcmp(name,"btb_gpio"));requests++;return 0;}
static void *devm_iio_channel_get(struct device *d,const char *name){(void)d;assert(!strcmp(name,"btb_adc"));return adc_error?ERR_PTR(adc_error):(void *)1;}
'''
t=r'''
int main(void){unsigned count=0;
 for(unsigned mode=0;mode<7;mode++)for(int left=0;left<4;left++)for(int right=0;right<4;right++)for(unsigned err=0;err<3;err++){
 struct device_node n={.present=mode!=0,.cells=mode==4?0:mode==5?2:mode==6?-EINVAL:1,.fg=(mode==1||mode>=4)?0:mode==2?1:2,.scheme={left,right}};
 struct device d={.of_node=&n};struct mca_bmd_dev info={.dev=&d,.fg_type=-123};requests=0;
 gpio_error=err==1?-EPROBE_DEFER:0;adc_error=err==2?-EPROBE_DEFER:0;
 int ret=mca_bmd_parse_dt(&info);
 if(mode>=3)assert(ret==-EINVAL&&requests==0);
 else {
  int expected=0;
  for(int i=0;i<2;i++){
   if(n.scheme[i]==INT_SCHEME){expected=-EOPNOTSUPP;break;}
   if((n.scheme[i]==GPIO_SCHEME&&gpio_error)||(n.scheme[i]==ADC_SCHEME&&adc_error)){expected=-EPROBE_DEFER;break;}
  }
  assert(ret==expected);assert(info.fg_type==(mode==2?1:0));
  if(!ret)assert(requests==((left==GPIO_SCHEME||right==GPIO_SCHEME)?1:0));
 }
 count++;
 }
 struct mca_bmd_dev empty={0};struct device d={0};empty.dev=&d;assert(mca_bmd_parse_dt(&empty)==-EINVAL);
 printf("PASS: %u optional/valid/invalid/malformed type, scheme, shared-GPIO and provider-defer cases\n",count);
}
'''
results={}
for variant in ['candidate','original','ignore_malformed_property']:
 source=(H/('before.c' if variant=='original' else 'candidate.c')).read_text();parse=fn(source,'static int mca_bmd_parse_dt(')
 if variant=='ignore_malformed_property':parse=parse.replace('\t\tif (of_property_count_u32_elems(node, "fg_type") != 1)\n\t\t\treturn -EINVAL;','')
 p=H/(variant+'-test.c');p.write_text(a+parse+t);exe=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);results[variant]={'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr[:2000]};assert (r.returncode==0)==(variant=='candidate'),(variant,results)
# Validate the exact production DT inputs, preserving the original scheme/GPIO.
boards={}
for name in ['tuna','tuna7']:
 p=W/'artifacts/20261102T014419982366Z/devicetrees/checked'/(name+'.dtb')
 get=lambda prop:subprocess.run(['fdtget','-t','x',str(p),'/soc/mca_bmd',prop],capture_output=True,text=True)
 fg=get('fg_type');schemes=get('btb_bmd_scheme');gpio=get('btb_gpio');assert fg.returncode!=0;assert schemes.returncode==0 and schemes.stdout.strip()=='3 1';assert gpio.returncode==0
 boards[name]={'dt_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'fg_type':'absent','scheme':schemes.stdout.strip(),'gpio':gpio.stdout.strip()}
report={'status':'PASS','cases':336,'scope':'Actual mca_bmd_parse_dt with host OF/GPIO/IIO adapters; both actual board properties verified; no hardware binding proven','results':results,'boards':boards,'candidate_sha256':hashlib.sha256((H/'candidate.c').read_bytes()).hexdigest()}
(H/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(results['candidate']['stdout']);print('PASS: mandatory-property and malformed-property controls rejected; exact board absence verified')
