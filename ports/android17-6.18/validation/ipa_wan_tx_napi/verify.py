#!/usr/bin/env python3
"""Exercise the production pipe teardown with separately owned TX and RX NAPI."""
import ast
import hashlib
import json
from pathlib import Path
import subprocess

H = Path(__file__).resolve().parent
previous = ast.parse((H.parent / 'ipa_wan_napi_lifetime/verify.py').read_text())
values = {}
for node in previous.body:
    if isinstance(node, ast.FunctionDef) and node.name == 'function':
        exec(compile(ast.Module(body=[node], type_ignores=[]), '<extract>', 'exec'), values)
    if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
        for name in node.targets:
            if isinstance(name, ast.Name) and name.id == 'adapter':
                values['adapter'] = ast.literal_eval(node.value)
function = values['function']
adapter = values['adapter'].replace('struct ipa3_ep_context ep[9]', 'struct ipa3_ep_context ep[10]')
adapter = adapter.replace('((c)==6||(c)==7)', '((c)==6||(c)==7||(c)==9)')
r = (H / 'candidate-rmnet_ipa.c').read_text()
before = (H / 'before-ipa_dp.c').read_text()
candidate = (H / 'candidate-ipa_dp.c').read_text()
outer = 'int ipa_teardown_sys_pipe(u32 clnt_hdl)'
inner = 'static int ipa3_teardown_pipe(u32 clnt_hdl)'
tests = r'''
static struct ipa3_sys_context pipes[10];
int main(void) {
 unsigned cases=0;
 for(unsigned bits=0;bits<64;bits++) {
  memset(&context,0,sizeof(context));memset(&device,0,sizeof(device));
  memset(pipes,0,sizeof(pipes));strcpy(device.name,"rmnet_ipa0");
  device.published=true;device.priv.napi.dev=&device;
  device.priv.napi.listed=true;device.priv.napi.enabled=true;
  device.priv.device_status=WWAN_DEVICE_ACTIVE;
  ipa3_rmnet_res.ipa_napi_enable=true;ipa3_ctx->rmnet_napi_enable=true;
  ipa3_ctx->ipa_num_pipes=10;require_owner=false;
  votes=enable_calls=disable_calls=delete_calls=sync_calls=completions=0;
  for(unsigned i=0;i<10;i++) {
   context.ep[i].client=i;context.ep[i].sys=&pipes[i];
   context.ep[i].keep_ipa_awake=bits&1;context.ep[i].gsi_evt_ring_hdl=-1;
   pipes[i].wq=&pipes[i];pipes[i].producer_running=true;
  }
  unsigned clients[3]={IPA_CLIENT_APPS_WAN_PROD,IPA_CLIENT_OTHER_PROD,
                       IPA_CLIENT_APPS_WAN_LOW_LAT_DATA_PROD};
  for(unsigned i=0;i<3;i++) {
   unsigned c=clients[i];context.ep[c].valid=true;
   pipes[c].napi_tx_enable=bits&(2<<i);
   pipes[c].napi_tx.dev=&device;
   pipes[c].napi_tx.listed=pipes[c].napi_tx.enabled=pipes[c].napi_tx_enable;
  }
  for(unsigned i=0;i<3;i++) {
   unsigned c=clients[(bits&16)?2-i:i];
   assert(ipa_teardown_sys_pipe(c)==0);
   assert(!pipes[c].napi_tx.listed&&!pipes[c].napi_tx.enabled);
   assert(!context.ep[c].valid);
   assert(device.priv.napi.listed&&device.priv.napi.enabled);
   assert(context.rmnet_napi_enable);
   if(bits&32)assert(ipa_teardown_sys_pipe(c)==-EINVAL);
  }
  device.published=false;require_owner=true;ipa3_wwan_napi_del(&device);
  assert(!device.priv.napi.listed&&!device.priv.napi.enabled);
  /* free_netdev must have no enabled/listed pipe-owned NAPI left to delete. */
  for(unsigned i=0;i<10;i++)assert(!pipes[i].napi_tx.listed);
  cases++;
 }
 printf("PASS: %u TX owner, disabled feature, shared RX, teardown order and repeated teardown cases\n",cases);
}
'''
variants = {
    'candidate': candidate,
    'original-wan-skip': before,
    'skip-tx-disable': candidate.replace('napi_disable(&ep->sys->napi_tx);', '(void)ep;'),
    'skip-tx-delete': candidate.replace('netif_napi_del(&ep->sys->napi_tx);', '(void)ep;'),
    'delete-shared-rx': candidate.replace('napi_disable(&ep->sys->napi_tx);',
                                        'napi_disable(&device.priv.napi);'),
}
results = {}
for name, source in variants.items():
    code = adapter + function(r, 'static void ipa3_wwan_napi_del(struct net_device *dev)')
    code += function(source, outer) + function(source, inner) + tests
    path = H / (name + '-test.c'); path.write_text(code)
    executable = path.with_suffix('')
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                    '-fsanitize=address,undefined', str(path), '-o', str(executable)], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    results[name] = {'returncode': result.returncode, 'stdout': result.stdout,
                     'stderr': result.stderr[:1800]}
    assert (result.returncode == 0) == (name == 'candidate'), (name, results[name])
assert function(before, inner) == function(candidate, inner)
report = {'status': 'PASS', 'cases': 64, 'results': results,
          'candidate_source_sha256': hashlib.sha256(candidate.encode()).hexdigest(),
          'scope': 'Complete production outer/inner pipe teardown with existing host netdev/GSI/workqueue adapters. Three independent producer TX objects share the rmnet RX owner.',
          'limits': 'No SMP, firmware or GSI failure timing coverage. Existing producer drain and failure behavior unchanged. Hardware warning absence remains pending.'}
(H / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(results['candidate']['stdout'], end='')
print('Original WAN skip and three cleanup regressions rejected.')
