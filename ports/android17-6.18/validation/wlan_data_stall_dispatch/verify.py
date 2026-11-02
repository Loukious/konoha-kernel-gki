#!/usr/bin/env python3
"""Exercise actual HDD data-stall handler and callback table against the API."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
live = W / 'onyx/qcacld-3.0/core/hdd/src/wlan_hdd_cfg80211.c'
source = live.read_text()
assert source == (H / 'candidate.c').read_text()

def function(text, name):
    start = re.search(r'^static int\s+' + name + r'\([^;]+?\n\{', text, re.M).start()
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def table(text):
    return re.search(r'static const interdependent_setter_fn interdependent_setters\[\] = \{.*?\n\};', text, re.S).group()

header = W / 'onyx/qca-wifi-host-cmn/os_if/linux/qca_vendor.h'
for name, expected in [('DURATION', 118), ('THRESHOLD', 119)]:
    assert re.search(r'QCA_WLAN_VENDOR_ATTR_CONFIG_CONSECUTIVE_TX_NO_ACK_' + name + r' = ' + str(expected), header.read_text())
    assert source.count('[QCA_WLAN_VENDOR_ATTR_CONFIG_CONSECUTIVE_TX_NO_ACK_' + name + '] =') == 1
    assert re.search(r'\[QCA_WLAN_VENDOR_ATTR_CONFIG_CONSECUTIVE_TX_NO_ACK_' + name + r'\] = \{\s*\.type = NLA_U16', source)

adapter = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#undef errno
typedef int QDF_STATUS;
typedef uint16_t u16;
typedef uint32_t u32;
#define QDF_IS_STATUS_ERROR(s) ((s)!=0)
#define QDF_ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define QCA_WLAN_VENDOR_ATTR_CONFIG_CONSECUTIVE_TX_NO_ACK_DURATION 118
#define QCA_WLAN_VENDOR_ATTR_CONFIG_CONSECUTIVE_TX_NO_ACK_THRESHOLD 119
#define TX_NO_ACK_DURATION_MAX 10000
#define TX_NO_ACK_DURATION_MIN 10
#define TX_NO_ACK_THRESHOLD_MIN 10
#define MAX_PDEV_DATA_STALL_PARAMS 2
#define wmi_pdev_param_dstall_consecutive_tx_no_ack_interval 1
#define wmi_pdev_param_dstall_consecutive_tx_no_ack_threshold 2
#define MLME_PDEV_SETPARAM 3
#define WMI_PDEV_ID_SOC 4
struct nlattr {uint16_t value;};
struct wlan_hdd_link_info {int unused;};
struct dev_set_param {unsigned id,value;};
static int prepares,sends,errors,failure;
static unsigned sent_count;
static struct dev_set_param sent[2];
#define hdd_err(...) (errors++)
#define hdd_debug(...) ((void)0)
static inline uint16_t nla_get_u16(struct nlattr *a){assert(a);return a->value;}
static inline int mlme_check_index_setparam(struct dev_set_param *p,unsigned id,unsigned v,unsigned i,unsigned capacity){
 assert(i<capacity&&capacity==2);prepares++;if(failure==prepares)return -1;p[i]=(struct dev_set_param){id,v};return 0;
}
static inline int wma_send_multi_pdev_vdev_set_params(unsigned type,unsigned dev,struct dev_set_param *p,unsigned count){
 assert(type==3&&dev==4&&count>=1&&count<=2);sends++;sent_count=count;
 for(unsigned i=0;i<count;i++)sent[i]=p[i];return failure==3?-1:0;
}
typedef int (*interdependent_setter_fn)(struct wlan_hdd_link_info *,struct nlattr **);
'''
other_names = sorted(set(re.findall(r'^\s*(\w+),', table(source), re.M)) - {'hdd_config_data_stall_param'})
stubs = '\n'.join('static int ' + n + '(struct wlan_hdd_link_info *l,struct nlattr **t){(void)l;(void)t;return 0;}' for n in other_names)
main = r'''
int main(void){
 const unsigned values[]={0,1,9,10,11,9999,10000,10001,65535};unsigned cases=0;
 for(unsigned mask=0;mask<4;mask++)for(unsigned i=0;i<9;i++)for(unsigned k=0;k<9;k++)for(int fail=0;fail<4;fail++){
  struct nlattr duration={values[i]},threshold={values[k]},unrelated={5},*tb[256]={0};
  struct wlan_hdd_link_info link={0};
  tb[5]=&unrelated;if(mask&1)tb[118]=&duration;if(mask&2)tb[119]=&threshold;
  prepares=sends=errors=sent_count=0;failure=fail;
  int rc=hdd_set_interdependent_configuration(&link,tb);
  bool valid=(!(mask&1)||values[i]==0||(values[i]>=10&&values[i]<=10000))&&(!(mask&2)||values[k]==0||values[k]>=10);
  unsigned expected_count=!!(mask&1)+!!(mask&2);
  if(!mask){assert(rc==0&&prepares==0&&sends==0&&errors==0);}
  else if(!valid){assert(rc==-EINVAL&&prepares==0&&sends==0);}
  else if(fail==1||(fail==2&&expected_count==2)){assert(rc==-EINVAL&&prepares==fail&&sends==0&&errors>0);}
  else{
   assert(sends==1&&sent_count==expected_count&&prepares==(int)expected_count);
   unsigned pos=0;if(mask&1){assert(sent[pos].id==1&&sent[pos].value==values[i]);pos++;}
   if(mask&2){assert(sent[pos].id==2&&sent[pos].value==values[k]);}
   assert(rc==(fail==3?-EINVAL:0));assert(errors==(fail==3?1:0));
  }
  cases++;
 }
 printf("PASS: %u absent/unrelated, individual/paired, zero-clear, bounds, preparation and firmware-failure cases\n",cases);
 return 0;
}
'''
variants = {'candidate': source, 'original': (H / 'before.c').read_text()}
variants['duplicate-dispatch'] = source.replace('\thdd_config_vdev_chains_per_band,\n', '\thdd_config_vdev_chains_per_band,\n\thdd_config_data_stall_param,\n', 1)
variants['ignore-single-attribute'] = source.replace('if (!tx_no_ack_duration_attr && !tx_no_ack_threshold_attr)', 'if (!tx_no_ack_duration_attr || !tx_no_ack_threshold_attr)', 1)
variants['accept-invalid-duration'] = source.replace('if (tx_no_ack_duration > TX_NO_ACK_DURATION_MAX ||', 'if (false ||', 1)
results = {}
for name, text in variants.items():
    code = adapter + '\n' + stubs + '\n' + function(text, 'hdd_config_data_stall_param') + '\n' + table(text) + '\n' + function(text, 'hdd_set_interdependent_configuration') + main
    path, exe = H / (name + '-test.c'), H / (name + '-test')
    path.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Werror', '-Wno-misleading-indentation', '-fsanitize=address,undefined', str(path), '-o', str(exe)], check=True)
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert (r.returncode == 0) == (name == 'candidate'), (name, r.stderr)
    results[name] = {'returncode': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr}
report = {'status': 'PASS', 'cases': 1296, 'results': results,
          'production_source': str(live), 'source_sha256': hashlib.sha256(live.read_bytes()).hexdigest(),
          'api_header_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
          'scope': 'Actual complete HDD callback and dispatch table. Optional attributes118/119, independent application and zero clearing follow shipped vendor API; numeric ranges retain existing driver behavior. Other table callbacks are no-op host adapters.',
          'limits': 'No actual netlink parsing, modem/firmware submission, Wi-Fi traffic or phone validation; backend calls use checked host adapters.'}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(results['candidate']['stdout'], end='')
print('Original and three regressions rejected; U16 policy declarations unique.')
