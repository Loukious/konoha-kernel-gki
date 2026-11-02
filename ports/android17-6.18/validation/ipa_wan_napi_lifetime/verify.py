#!/usr/bin/env python3
"""Exercise production WAN open/close, final NAPI release and pipe teardown."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

H = Path(__file__).resolve().parent


def function(source, signature):
    start = source.index(signature + '\n{')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


adapter = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define IPA_WWAN_DEV_NAME "rmnet_ipa0"
#define WWAN_DEVICE_ACTIVE 1
#define WWAN_DEVICE_INACTIVE 0
#define IPA_CLIENT_APPS_WAN_CONS 0
#define IPA_CLIENT_APPS_WAN_COAL_CONS 1
#define IPA_CLIENT_APPS_LAN_CONS 2
#define IPA_CLIENT_APPS_LAN_COAL_CONS 3
#define IPA_CLIENT_APPS_WAN_LOW_LAT_CONS 4
#define IPA_CLIENT_APPS_WAN_LOW_LAT_DATA_CONS 5
#define IPA_CLIENT_APPS_WAN_PROD 6
#define IPA_CLIENT_APPS_WAN_LOW_LAT_DATA_PROD 9
#define IPA_CLIENT_OTHER_PROD 7
#define IPA_CLIENT_WLAN_CONS 8
#define IPA_CLIENT_IS_WAN_CONS(c) ((c)==0||(c)==1)
#define IPA_CLIENT_IS_PROD(c) ((c)==6||(c)==7)
#define IPA_CLIENT_IS_CONS(c) (!IPA_CLIENT_IS_PROD(c))
#define IPA_CLIENT_IS_WLAN_CONS(c) ((c)==8)
#define IPA_CLIENT_IS_MAPPED(c,i) (((i)=(c)),true)
#define IPA_GSI_CHANNEL_STOP_MAX_RETRY 3
#define GSI_STATUS_SUCCESS 0
#define GSI_STATUS_AGAIN 1
#define GSI_STATUS_TIMED_OUT 2
#define POLLING_MIN_SLEEP_RX 10
#define POLLING_MAX_SLEEP_RX 20
#define IPA_GSI_EVENT_RP_SIZE 8
#define IPAERR(...) ((void)0)
#define IPADBG(...) ((void)0)
#define IPAWANDBG(...) ((void)0)
#define IPAWANERR(...) ((void)0)
#define IPA_ACTIVE_CLIENTS_INC_EP(c) do {(void)(c); votes++;} while(0)
#define IPA_ACTIVE_CLIENTS_DEC_EP(c) do {(void)(c); votes--;} while(0)
struct net_device;
struct napi_struct {struct net_device *dev;bool listed,enabled,scheduled;};
struct ipa3_wwan_private {int device_status,resource_granted_completion;struct napi_struct napi;};
struct net_device {char name[16];bool published;struct ipa3_wwan_private priv;};
struct ipa3_sys_context {
 struct napi_struct *napi_obj;struct napi_struct napi_rx,napi_tx;
 int curr_polling_state,spinlock,head_desc_list,workqueue_flushed;
 bool napi_tx_enable,use_comm_evt_ring,common_buff_pool;
 void *wq,*repl_wq,*freepage_wq;
 int replenish_rx_work,tasklet;
 struct {int freepage_work,tasklet_find_freepage;} *common_sys;
 void (*repl_hdlr)(void);
 bool queued_rx,producer_running;
};
struct ipa3_wlan_stats {int value;};
struct ipa3_ep_context {
 bool valid,keep_ipa_awake,skip_ep_cfg;
 int client,disconnect_in_progress;
 struct ipa3_sys_context *sys;
 struct {int chan_ring_len,evt_ring_len;void *chan_ring_base_vaddr,*evt_ring_base_vaddr,*evt_ring_rp_vaddr;
 uintptr_t chan_ring_base_addr,evt_ring_base_addr,evt_ring_rp_addr;} gsi_mem_info;
 int gsi_chan_hdl,gsi_evt_ring_hdl;
 struct ipa3_wlan_stats wstats;
};
struct ipa_context {
 unsigned ipa_num_pipes;bool rmnet_napi_enable,modem_cfg_emb_pipe_flt;
 struct ipa3_ep_context ep[9];void *pdev;
 int gsi_evt_comm_ring_rem;
 struct {int active_clnt_cnt;} wc_memb;
};
static struct ipa_context context,*ipa3_ctx=&context;
static struct {bool ipa_napi_enable;} ipa3_rmnet_res;
static struct net_device device;
static int votes,enable_calls,disable_calls,delete_calls,sync_calls,ring_resets,stop_failure;
static int close_error,completions,posted_drains;
static bool require_owner;
#define atomic_read(p) (*(p))
#define atomic_set(p,v) (*(p)=(v))
#define atomic_dec(p) (--*(p))
#define spin_lock_bh(p) ((void)(p))
#define spin_unlock_bh(p) ((void)(p))
#define list_empty(p) (*(p)==0)
#define reinit_completion(p) do {(void)(p);completions++;} while(0)
static inline void *netdev_priv(struct net_device *d) {return &d->priv;}
static inline int ipa_deregister_intf(const char *name) {(void)name;return close_error;}
static inline void napi_enable(struct napi_struct *n) {
 assert(n->listed&&!n->enabled);n->enabled=true;n->scheduled=false;enable_calls++;
}
static inline void drain_poll(void) {
 if(device.priv.napi.enabled&&device.priv.napi.scheduled){
  assert(device.priv.napi.listed);device.priv.napi.scheduled=false;posted_drains++;
 }
 for(unsigned i=0;i<ipa3_ctx->ipa_num_pipes;i++)
  if(ipa3_ctx->ep[i].sys)ipa3_ctx->ep[i].sys->curr_polling_state=0;
}
static inline void napi_disable(struct napi_struct *n) {
 assert(n->listed&&n->enabled);drain_poll();n->enabled=false;disable_calls++;
}
static inline void netif_napi_del(struct napi_struct *n) {
 assert(n->listed&&!n->enabled);
 if(require_owner)assert(!n->dev->published);
 n->listed=false;delete_calls++;
}
static inline void napi_synchronize(struct napi_struct *n) {
 assert(n->listed&&n->enabled);drain_poll();sync_calls++;
}
static inline void usleep_range(int min,int max) {(void)min;(void)max;drain_poll();}
static inline int ipa3_get_client_mapping(u32 h) {return (int)h;}
static inline void ipa3_disable_data_path(u32 h) {(void)h;}
static inline void delete_avail_tx_wrapper_list(struct ipa3_ep_context *e) {(void)e;}
static inline void stop_coalescing(void) {}
static inline void start_coalescing(void) {}
static inline void ipa3_force_close_coal(bool a,bool b) {(void)a;(void)b;}
static inline void ipa_assert(void) {}
static inline int ipa_stop_gsi_channel(u32 h) {
 if(stop_failure==((int)h)+1)return -GSI_STATUS_TIMED_OUT;
 ipa3_ctx->ep[h].sys->producer_running=false;return 0;
}
static inline void cancel_delayed_work_sync(int *w) {(void)w;}
static inline void flush_workqueue(void *w) {
 struct ipa3_sys_context *s=w;
 if(s&&s->queued_rx){
  s->queued_rx=false;
  if(s->napi_obj){assert(s->napi_obj->listed&&s->napi_obj->enabled);s->napi_obj->scheduled=true;}
  else s->curr_polling_state=0;
 }
}
static inline void tasklet_kill(int *t) {(void)t;}
static inline int ipa3_reset_gsi_channel(u32 h) {
 struct ipa3_sys_context *s=ipa3_ctx->ep[h].sys;
 assert(!s->producer_running&&!s->queued_rx);
 if(s->napi_obj)assert(!s->napi_obj->scheduled||!s->napi_obj->enabled);
 ring_resets++;return 0;
}
static inline void dma_free_coherent(void *d,int n,void *v,uintptr_t a) {(void)d;(void)n;(void)v;(void)a;}
static inline int gsi_dealloc_channel(int h) {(void)h;return 0;}
static inline int gsi_reset_evt_ring(int h) {(void)h;return 0;}
static inline int gsi_dealloc_evt_ring(int h) {(void)h;return 0;}
#define WARN(c,...) (c)
static inline void ipa3_replenish_rx_page_recycle(void) {}
static inline void ipa3_cleanup_rx(struct ipa3_sys_context *s) {(void)s;}
static inline void ipa3_delete_dflt_flt_rules(u32 h) {(void)h;}
static inline void ipa3_cleanup_wlan_rx_common_cache(void) {}
static int ipa3_teardown_pipe(u32 clnt_hdl);
'''

tests = r'''
static struct ipa3_sys_context sys[9];
static void reset(bool configured,bool opened,unsigned opens,bool pending,bool queued,bool keep) {
 memset(&device,0,sizeof(device));memset(&context,0,sizeof(context));memset(sys,0,sizeof(sys));
 strcpy(device.name,"rmnet_ipa0");device.published=true;device.priv.napi.dev=&device;
 device.priv.napi.listed=configured;
 ipa3_rmnet_res.ipa_napi_enable=configured;
 ipa3_ctx->ipa_num_pipes=9;
 votes=enable_calls=disable_calls=delete_calls=sync_calls=ring_resets=stop_failure=0;
 close_error=completions=posted_drains=0;require_owner=true;
 for(unsigned i=0;i<9;i++){
  ipa3_ctx->ep[i].client=(int)i;ipa3_ctx->ep[i].sys=&sys[i];
  ipa3_ctx->ep[i].keep_ipa_awake=keep;
  ipa3_ctx->ep[i].gsi_evt_ring_hdl=-1;
  sys[i].wq=&sys[i];sys[i].producer_running=true;
  if(i<2&&configured)sys[i].napi_obj=&device.priv.napi;
 }
 if(opened){
  for(unsigned i=0;i<opens;i++){
   assert(__ipa_wwan_open(&device)==0);
   assert(__ipa_wwan_close(&device)==0);
   assert(device.priv.napi.enabled==configured);
  }
  assert(__ipa_wwan_open(&device)==0);
 }
 if(pending&&opened){device.priv.napi.scheduled=configured;sys[0].curr_polling_state=sys[1].curr_polling_state=1;}
 if(queued&&(opened||!configured))sys[0].queued_rx=sys[1].queued_rx=true;
}
static void final_release(void) {
 device.published=false;
 ipa3_wwan_napi_del(&device);
 assert(!device.priv.napi.listed&&!device.priv.napi.enabled&&!ipa3_ctx->rmnet_napi_enable);
}
int main(int argc,char **argv) {
 unsigned cases=0;
 if(argc>1&&strcmp(argv[1],"coal")==0){
  reset(true,true,0,false,false,false);
  ipa3_ctx->ep[0].valid=true;ipa3_ctx->ep[1].valid=true;
  assert(ipa_teardown_sys_pipe(1)==0);
  assert(delete_calls==0&&disable_calls==0&&device.priv.napi.enabled);
  final_release();return 0;
 }
 if(argc>1&&strcmp(argv[1],"shutdown")==0){
  reset(true,true,0,false,false,false);ipa3_ctx->ep[0].valid=true;
  assert(ipa_teardown_sys_pipe(0)==0);final_release();return 0;
 }
 for(unsigned bits=0;bits<256;bits++){
  bool configured=bits&1,opened=bits&2,pending=bits&4,queued=bits&8,keep=bits&16;
  unsigned shape=(bits>>5)&3,opens=(bits&128)?5:0;
  reset(configured,opened,opens,pending,queued,keep);
  if(shape==0){ipa3_ctx->ep[0].valid=true;assert(ipa_teardown_sys_pipe(0)==0);}
  else if(shape==1){ipa3_ctx->ep[0].valid=ipa3_ctx->ep[1].valid=true;assert(ipa_teardown_sys_pipe(1)==0);assert(!ipa3_ctx->ep[0].valid);}
  else if(shape==2){ipa3_ctx->ep[1].valid=true;sys[0].queued_rx=false;assert(ipa_teardown_sys_pipe(1)==0);}
  else {
   ipa3_ctx->ep[0].valid=true;assert(ipa_teardown_sys_pipe(0)==0);
   assert(device.priv.napi.listed==configured&&device.priv.napi.enabled==(configured&&opened));
   ipa3_ctx->ep[0].valid=true;sys[0].napi_obj=configured?&device.priv.napi:NULL;sys[0].producer_running=true;
   assert(ipa_teardown_sys_pipe(0)==0);
  }
  assert(enable_calls==(configured&&opened));assert(disable_calls==0&&delete_calls==0);
  assert(ipa3_ctx->rmnet_napi_enable==(configured&&opened));
  final_release();assert(disable_calls==(configured&&opened)&&delete_calls==configured);
  cases++;
 }
 // A failed GSI stop must leave the netdev's live NAPI and borrowed reference intact.
 for(unsigned shape=0;shape<3;shape++){
  reset(true,true,0,false,true,false);
  ipa3_ctx->ep[0].valid=true;ipa3_ctx->ep[1].valid=shape!=0;
  stop_failure=shape==2?1:(shape==1?2:1);
  assert(ipa_teardown_sys_pipe(shape?1:0)<0);
  assert(device.priv.napi.enabled&&device.priv.napi.listed&&delete_calls==0&&disable_calls==0);
  assert(ipa3_ctx->ep[0].sys->napi_obj==&device.priv.napi);
  cases++;
 }
 // Registration failure: no open or RX producers, but added NAPI must be deleted.
 reset(true,false,0,false,false,false);device.published=false;ipa3_wwan_napi_del(&device);
 assert(delete_calls==1&&disable_calls==0);cases++;
 printf("PASS: %u WAN down/up, shared/default/coal teardown, queued RX, GSI failure and final-release cases\n",cases);
 return 0;
}
'''

r = (H / 'candidate-rmnet_ipa.c').read_text()
d = (H / 'candidate-ipa_dp.c').read_text()
original_r = (H / 'before-rmnet_ipa.c').read_text()
original_d = (H / 'before-ipa_dp.c').read_text()
signatures = ['static void ipa3_wwan_napi_del(struct net_device *dev)',
              'static int __ipa_wwan_open(struct net_device *dev)',
              'static int __ipa_wwan_close(struct net_device *dev)']
outer_sig = 'int ipa_teardown_sys_pipe(u32 clnt_hdl)'
inner_sig = 'static int ipa3_teardown_pipe(u32 clnt_hdl)'
variants = {
    'candidate': (r, d),
    'duplicate-enable': (r.replace(' && !ipa3_ctx->rmnet_napi_enable', ''), d),
    'delete-enabled': (r.replace('napi_disable(&wwan_ptr->napi);', '/* missing disable */'), d),
    'early-coal-owner-delete': (r, original_d),
    'skip-posted-rx-drain': (r, d.replace('napi_synchronize(ep->sys->napi_obj);', '(void)ep;')),
    'delete-after-regular-stop': (r.replace('\n\tif (wwan_ptr->device_status == WWAN_DEVICE_ACTIVE) {',
                                          '\n\tipa3_wwan_napi_del(dev);\n\tif (wwan_ptr->device_status == WWAN_DEVICE_ACTIVE) {'), d),
}
results = {}
for variant, (vr, vd) in variants.items():
    code = adapter + '\n'.join(function(vr, sig) for sig in signatures)
    code += '\n' + function(vd, outer_sig) + '\n' + function(vd, inner_sig) + tests
    p = H / (variant + '-test.c')
    p.write_text(code)
    executable = p.with_suffix('')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', str(p), '-o', str(executable)], check=True)
    run = subprocess.run([str(executable)], capture_output=True, text=True)
    results[variant] = {'returncode': run.returncode, 'stdout': run.stdout,
                        'stderr': run.stderr[:2500]}
    assert (run.returncode == 0) == (variant == 'candidate'), (variant, results[variant])

# Check real publication/unpublication and error labels, not just helper behavior.
probe = function(r, 'static int ipa3_wwan_probe(struct platform_device *pdev)')
assert probe.index('ipa3_ctx->rmnet_napi_enable = false;') < probe.index('register_netdev(dev)')
assert probe.count('ipa3_ctx->rmnet_napi_enable = false;') == 1
assert probe.index('config_err:') < probe.index('unregister_netdev(dev);') < probe.index('set_perf_err:') < probe.index('ipa3_wwan_napi_del(dev);') < probe.index('free_netdev(dev);')
for sig in ['static void ipa3_wwan_remove(struct platform_device *pdev)',
            'static int ipa3_lcl_mdm_ssr_notifier_cb(struct notifier_block *this,']:
    if 'notifier' in sig:
        start = re.search(r'static int ipa3_lcl_mdm_ssr_notifier_cb\([^;{]+\n\{', r).start()
        end = r.index('\nstatic ', start + len(sig))
        body = r[start:end]
    else:
        body = function(r, sig)
    assert body.index('unregister_netdev(IPA_NETDEV());') < body.index('ipa3_wwan_napi_del(IPA_NETDEV());') < body.index('free_netdev(IPA_NETDEV());')
for sig in [outer_sig, inner_sig]:
    body = function(d, sig)
    assert 'napi_disable(ep->sys->napi_obj)' not in body
    assert 'netif_napi_del(ep->sys->napi_obj)' not in body
    assert body.index('ipa_stop_gsi_channel') < body.index('flush_workqueue(ep->sys->wq)') < body.index('napi_synchronize(ep->sys->napi_obj)') < body.index('ipa3_reset_gsi_channel') < body.index('ep->sys->napi_obj = NULL')
    assert body.index('ep->sys->napi_obj = NULL') < body.index('ep->valid = 0')
assert function(r, signatures[2]) == function(original_r, signatures[2])
report = {
    'status': 'PASS', 'cases': 260, 'results': results,
    'source_hashes': {name: hashlib.sha256((H / name).read_bytes()).hexdigest()
                      for name in ['candidate-rmnet_ipa.c', 'candidate-ipa_dp.c']},
    'scope': 'Production WAN open/close, final NAPI helper and complete outer/inner pipe teardown with host netdev/GSI/workqueue adapters; actual publication, error and final-owner callsites audited.',
    'limits': 'No modem firmware, SMP interrupt race, GSI stop convergence or hardware validation. Current RX setup protocol opens the WAN device before enabling ingress producers. Driver-wide pre-existing error unwinding and post-napi_complete accesses need separate audit.',
}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(results['candidate']['stdout'], end='')
print('PASS: five lifecycle/ownership/drain negative controls rejected; source ordering audited')
