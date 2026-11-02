#!/usr/bin/env python3
"""Exercise actual display lastclose teardown and framebuffer restoration."""
from pathlib import Path
import json,hashlib,subprocess
H=Path(__file__).resolve().parent
def fn(s,sig):
 start=s.index(sig);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
a=r'''
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define LASTCLOSE_TIMEOUT_MS 1000
#define DRM_INFO(...) ((void)0)
#define DRM_ERROR(...) (errors++)
#define msm_wait_event_timeout(wq,cond,timeout,rc) do{(void)(wq);(void)(cond);(void)(timeout);waits++;rc=1;}while(0)
struct timer_list { bool active;void (*function)(struct timer_list *); };
struct drm_vblank_crtc {struct timer_list disable_timer;};
struct msm_kms;
struct msm_kms_funcs { bool (*check_for_splash)(struct msm_kms *);int (*trigger_null_flush)(struct msm_kms *);void (*lastclose)(struct msm_kms *); };
struct msm_kms {const struct msm_kms_funcs *funcs;struct {void *dev;}client;};
struct msm_drm_private {struct msm_kms *kms;int pending_crtcs_event,pending_crtcs;void *wq,*fbdev;};
struct drm_device {void *dev_private;int num_crtcs;struct drm_vblank_crtc *vblank;};
static bool splash;
static int null_error,restore_error,waits,timers,work_flushes,thread_flushes,restores,invalid_commits,lastcloses,errors;
static bool check_splash(struct msm_kms *k){(void)k;return splash;}
static int trigger_null(struct msm_kms *k){(void)k;return null_error;}
static void close_kms(struct msm_kms *k){(void)k;lastcloses++;}
static void timer_callback(struct timer_list *t){(void)t;timers++;}
static bool timer_delete_sync(struct timer_list *t){return t->active;}
static void flush_workqueue(void *p){assert(p);work_flushes++;}
static void msm_atomic_flush_display_threads(struct msm_drm_private *p){assert(p);thread_flushes++;}
static int drm_fb_helper_restore_fbdev_mode_unlocked(void *p){assert(p);restores++;return restore_error;}
static int drm_client_modeset_commit_locked(void *p){assert(p);invalid_commits++;return -22;}
'''
t=r'''
int main(void){unsigned count=0;
 for(unsigned bits=0;bits<128;bits++){
 splash=bits&1;null_error=(bits&2)?-5:0;restore_error=(bits&4)?-12:0;
 waits=timers=work_flushes=thread_flushes=restores=invalid_commits=lastcloses=errors=0;
 struct msm_kms_funcs funcs={.check_for_splash=check_splash,.trigger_null_flush=trigger_null,.lastclose=(bits&8)?close_kms:0};
 struct msm_kms kms={.funcs=(bits&16)?&funcs:0,.client.dev=(bits&32)?(void *)1:0};
 struct msm_drm_private p={.kms=&kms,.wq=(void *)1,.fbdev=(bits&64)?(void *)1:0};
 struct drm_vblank_crtc vb[2]={{{true,timer_callback}},{{false,timer_callback}}};
 struct drm_device d={.dev_private=&p,.num_crtcs=2,.vblank=vb};
 msm_lastclose(&d);
 bool abort=kms.funcs&&splash&&null_error;
 assert(invalid_commits==0);
 if(abort)assert(work_flushes==0&&restores==0&&lastcloses==0&&timers==0&&waits==1);
 else {assert(work_flushes==1&&thread_flushes==1&&timers==1);assert(waits==2+!!(kms.funcs&&splash));assert(restores==!!p.fbdev);assert(lastcloses==!!(kms.funcs&&funcs.lastclose));assert(errors==!!(p.fbdev&&restore_error));}
 count++;
 }
 struct drm_device empty={0};msm_lastclose(&empty);struct msm_drm_private p={0};empty.dev_private=&p;msm_lastclose(&empty);
 printf("PASS: %u client/fbdev/splash/error/timer/worker/KMS cleanup cases and missing-device cases\n",count);
}
'''
results={}
for variant in ['candidate','original','skip_lastclose_cleanup']:
 source=(H/('before.c' if variant=='original' else 'candidate.c')).read_text();parse=fn(source,'static void msm_lastclose(')
 if variant=='skip_lastclose_cleanup':parse=parse.replace('\tif (priv->fbdev) {','\tif (!priv->fbdev)\n\t\treturn;\n\tif (priv->fbdev) {',1)
 p=H/(variant+'-test.c');p.write_text(a+parse+t);exe=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True);results[variant]={'returncode':run.returncode,'stdout':run.stdout,'stderr':run.stderr[:2000]};assert (run.returncode==0)==(variant=='candidate'),(variant,results)
source=(H/'candidate.c').read_text();assert 'drm_client_modeset_probe(client, 0, 0)' in source;assert 'drm_client_buffer_create(' not in source
report={'status':'PASS','cases':128,'scope':'Actual msm_lastclose with host timer/wait/client/fbdev/KMS adapters; client remains topology-only; hardware shutdown verification pending','results':results,'candidate_sha256':hashlib.sha256(source.encode()).hexdigest()}
(H/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(results['candidate']['stdout']);print('PASS: invalid framebuffer commit and skipped KMS cleanup controls rejected')
