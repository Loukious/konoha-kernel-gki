from pathlib import Path
import json,sys,tempfile,subprocess,shutil
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'audio_machine'))
from verify_smartpa import function
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#define GFP_KERNEL 0
#define DRM_BRIDGE_ATTACH_NO_CONNECTOR 1
#define DSI_ERR(...) ((void)0)
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
struct device { int id; };
struct drm_device { struct device *dev; };
struct drm_encoder { struct drm_bridge *bridge; };
struct drm_bridge { int refs;void *container,*funcs;struct drm_encoder *encoder; };
struct dsi_display { struct device *pdev;int display_lock;struct dsi_bridge *bridge; };
struct dsi_bridge { struct drm_bridge base;struct dsi_display *display; };
static int dsi_bridge_ops;
static struct device master={1},component={2};
static int fail_alloc,fail_action,fail_attach,live,freed;
static struct dsi_bridge *owned;
static void *kzalloc(size_t n,int flags) { (void)flags;return calloc(1,n); }
static void kfree(void *p) { if(p) { struct dsi_bridge *b=p;assert(b->base.refs<=1);free(p); } }
static void mutex_lock(int *p) { assert(*p==0);*p=1; }
static void mutex_unlock(int *p) { assert(*p==1);*p=0; }
static struct dsi_bridge *managed_alloc(struct device *owner,void *ops) {
 assert(owner==&master && !owned);
 if(fail_alloc)return ERR_PTR(-ENOMEM);
 struct dsi_bridge *p=calloc(1,sizeof(*p));assert(p);p->base.refs=1;p->base.container=p;p->base.funcs=ops;live++;
 if(fail_action) { live--;freed++;free(p);return ERR_PTR(-ENOMEM); }
 owned=p;return p;
}
#define devm_drm_bridge_alloc(owner,type,member,ops) ((type *)managed_alloc(owner,(void *)(ops)))
static int drm_bridge_attach(struct drm_encoder *enc,struct drm_bridge *b,void *prev,int flags) {
 assert(!prev && flags==1 && b->refs==1 && b->container && b->funcs==&dsi_bridge_ops);b->refs++;
 if(fail_attach) { b->refs--;return -EIO; }
 enc->bridge=b;return 0;
}
static void encoder_cleanup(struct drm_encoder *enc) { assert(enc->bridge && enc->bridge->refs==2);enc->bridge->refs--;enc->bridge=NULL; }
static void group_release(void) { if(owned) { assert(owned->base.refs==1);owned->base.refs--;live--;freed++;free(owned);owned=NULL; } }
/* SOURCE */
int main(void) {
 struct drm_device dev={&master};struct drm_encoder enc={0};struct dsi_display display={.pdev=&component};
 assert(dsi_display_drm_bridge_deinit(NULL)==-EINVAL);
 for(int i=0;i<100;i++) {
  fail_alloc=1;struct dsi_bridge *p=dsi_drm_bridge_init(&display,&dev,&enc);assert(IS_ERR(p) && PTR_ERR(p)==-ENOMEM && !live && !owned);fail_alloc=0;
  fail_action=1;p=dsi_drm_bridge_init(&display,&dev,&enc);assert(IS_ERR(p) && PTR_ERR(p)==-ENOMEM && !live && !owned);fail_action=0;
  fail_attach=1;p=dsi_drm_bridge_init(&display,&dev,&enc);assert(IS_ERR(p) && PTR_ERR(p)==-EIO && live==1 && owned);group_release();assert(!live);fail_attach=0;
  p=dsi_drm_bridge_init(&display,&dev,&enc);assert(!IS_ERR(p) && p && live==1 && p->base.refs==2 && p->display==&display);
  display.bridge=p;assert(dsi_display_drm_bridge_deinit(&display)==0 && !display.bridge && p->base.refs==2);
  encoder_cleanup(&enc);group_release();assert(!live);
 }
 assert(freed==300);
}
"""
rel='qcom/opensource/display-drivers/msm/dsi/'
results=[]
for version in ['candidate','before','early-kfree']:
 root=HERE/('before' if version=='before' else 'candidate');init=(root/(rel+'dsi_drm.c')).read_text();cleanup=(root/(rel+'dsi_display.c')).read_text()
 code=function(init,'dsi_drm_bridge_init')+'\n'+function(cleanup,'dsi_display_drm_bridge_deinit')
 if version=='before':code=function(init,'dsi_drm_bridge_cleanup')+'\n'+code
 if version=='early-kfree':code=code.replace('display->bridge = NULL;','kfree(display->bridge);display->bridge = NULL;')
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp)/'bridge.c';p.write_text(fixture.replace('/* SOURCE */',code));exe=Path(tmp)/'bridge';r=subprocess.run([shutil.which('cc'),'-Wall','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True)
  if r.returncode:raise RuntimeError(r.stderr.decode())
  r=subprocess.run([str(exe)],capture_output=True)
 assert (r.returncode==0)==(version=='candidate'),(version,r.stderr)
 results.append({'version':version,'exit':r.returncode})
report={'status':'PASS','cycles':100,'results':results,'contract':'exact init/deinit preserve managed owner and encoder refs; allocation/action/attach failures and retry groups free once','limits':'API reference/devres ordering modeled from pinned DRM/component code; hardware validation pending'}
(HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
