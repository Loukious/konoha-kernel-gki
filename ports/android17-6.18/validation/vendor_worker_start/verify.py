#!/usr/bin/env python3
"""Exercise extracted vendor worker creation/setup and failure paths."""
from pathlib import Path
import json,hashlib,subprocess
H=Path(__file__).resolve().parent;W=Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
def fn(s,sig):
 start=s.index(sig);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
a=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define ARRAY_SIZE(x) ((int)(sizeof(x)/sizeof((x)[0])))
#define KMEM_CACHE(...) ((void *)1)
#define WARN_ON(x) ((void)(x))
#define DISP_ERROR(...) ((void)0)
#define DISP_INFO(...) ((void)0)
#define dev_err(...) ((void)0)
#define HFI_MAX_MSG_SIZE 128
#define GFP_KERNEL 0
#define ADRENO_DISPATCHER_INIT 0
#define ADRENO_LSR 1
#define KGSL_DEVICE(x) (&(x)->dev)
#define KGSL_GOVERNOR_CALL_INTERVAL 10
#define init_llist_head(x) (*(x)=1)
#define INIT_LIST_HEAD(x) (*(x)=1)
#define mutex_init(x) (*(x)=1)
#define init_waitqueue_head(x) (*(x)=1)
#define atomic_set(x,v) (*(x)=((v)+1))
#define timer_setup(x,func,flags) do {*(x)=1;(void)(func);(void)(flags);}while(0)
#define INIT_WORK(x,func) do {*(x)=1;(void)(func);}while(0)
#define init_completion(x) (*(x)=1)
#define complete_all(x) (*(x)=2)
static int jobs_cache_storage,obj_cache_storage;
static void *jobs_cache,*obj_cache;
static void *_dispatch_attr_list,*_hwsched_attr_list;static int swsched_ops,hwsched_ops;
struct task_struct { bool started,fifo; unsigned long ready; };
struct kthread_worker { struct task_struct *task; };
struct disp_display { struct kthread_worker *worker; int pending_wq,pending_doze_cnt; };
struct disp_feature { struct disp_display d_display[2]; };
struct adreno_dispatcher { int mutex,timer,idle_gate; unsigned long priv; int jobs[2],requeue[2]; };
struct adreno_hwsched { void *ctxt_bad; int mutex,cmd_list,jobs[2],requeue[2],idle_gate,lsr_check_ws,lsr_timer; const void *hwsched_ops; };
struct kgsl_device { struct { int kobj; } *dev;struct kthread_worker *events_worker; };
struct adreno_device { struct kgsl_device dev; struct adreno_dispatcher dispatcher;struct adreno_hwsched hwsched;struct kthread_worker *scheduler_worker;int scheduler_work; };
struct adreno_hwsched_ops { int unused; };
struct kgsl_pwrscale { struct kthread_worker *devfreq_notify_worker;int devfreq_notify_work,devfreq_suspend_ws,devfreq_resume_ws; unsigned long next_governor_call; };
static struct task_struct task;
static struct kthread_worker worker={.task=&task};
static bool create_fail,lsr,complete_setup;
static unsigned wakes;
static struct kthread_worker *kthread_create_worker(unsigned f,const char *name,...) {
 (void)f;(void)name;memset(&task,0,sizeof(task));return create_fail?ERR_PTR(-ENOMEM):&worker;
}
static void sched_set_fifo(struct task_struct *t) {assert(!t->started);t->fifo=true;}
static int wake_up_process(struct task_struct *t) {assert(!t->started && t->fifo);assert(complete_setup);t->started=true;wakes++;return 1;}
static bool is_support_disp_id(int id) {return id>=0 && id<2;}
static void *kzalloc(size_t s,int f) {(void)f;return calloc(1,s);}
static void kfree(void *p) {free(p);}
static int sysfs_create_files(void *k,void *l) {(void)k;(void)l;return 0;}
static void kobject_put(void *p) {(void)p;}
static bool test_bit(int bit,unsigned long *p) {return (*p>>bit)&1;}
static void set_bit(int bit,unsigned long *p) {*p|=1UL<<bit;complete_setup=true;}
static void kthread_init_work(int *w,int func) {*w=1;(void)func;}
static void adreno_set_dispatch_ops(struct adreno_device *d,void *ops) {(void)d;(void)ops;}
static bool ADRENO_FEATURE(struct adreno_device *d,int f) {(void)d;(void)f;return lsr;}
static int adreno_dispatcher_timer,adreno_dispatcher_work,adreno_hwsched_work,hwsched_lsr_check,hwsched_lsr_timer;
static int do_devfreq_suspend,do_devfreq_resume,do_devfreq_notify;
static unsigned long ktime_get(void) {return 123;}
static unsigned long ktime_add_us(unsigned long a,unsigned long b) {return a+b;}
'''
# Inject setup checks into the host API boundary, not the driver decisions.
# A wake must occur only after all callback-visible fields and priority exist.
b=r'''
static void check_feature(struct disp_feature *p){complete_setup=p->d_display[0].pending_wq && p->d_display[0].pending_doze_cnt;}
static void check_hwsched(struct adreno_device *p){complete_setup=p->hwsched.mutex && p->scheduler_work && p->hwsched.cmd_list && p->hwsched.idle_gate==2 && p->hwsched.hwsched_ops && (!lsr || (p->hwsched.lsr_check_ws && p->hwsched.lsr_timer));}
static void check_scale(struct kgsl_pwrscale *p){complete_setup=p->devfreq_notify_work && p->devfreq_suspend_ws && p->devfreq_resume_ws;}
'''
# Retain the actual complete feature function and actual contiguous GPU setup
# excerpts. Earlier/later device probing is covered by the full kernel build.
paths={
 'feature':('qcom/opensource/display-drivers/msm/mi_disp/mi_disp_feature.c','static int mi_disp_feature_thread_create('),
 'dispatch':('qcom/opensource/graphics-kernel/adreno_dispatch.c','int adreno_dispatcher_init('),
 'hwsched':('qcom/opensource/graphics-kernel/adreno_hwsched.c','int adreno_hwsched_init('),
 'events':('qcom/opensource/graphics-kernel/kgsl.c','int kgsl_device_platform_probe('),
 'scale':('qcom/opensource/graphics-kernel/kgsl_pwrscale.c','int kgsl_pwrscale_init(')}
results={}
for variant,suffix in [('candidate',''),('original','.before')]:
 funcs=[]
 for name,(path,sig) in paths.items():
  source=(H/(path+suffix)).read_text();text=fn(source,sig)
  if name=='feature':
   text=text.replace('\twake_up_process(dd_ptr->worker->task);','\tcheck_feature(df);\n\twake_up_process(dd_ptr->worker->task);')
  elif name=='dispatch':
   start=text.index('\tadreno_dev->scheduler_worker = kthread_create_worker(')
   end=text.rindex('\treturn 0;')
   body=text[start:end].replace('\t\tkobject_put(&dispatcher->kobj);','')
   text='static int dispatch(struct adreno_device *adreno_dev) { struct kgsl_device *device=&adreno_dev->dev; struct adreno_dispatcher *dispatcher=&adreno_dev->dispatcher;int i;'+body+'return 0;}';
  elif name=='hwsched':
   text=text.replace('\twake_up_process(adreno_dev->scheduler_worker->task);','\tcheck_hwsched(adreno_dev);\n\twake_up_process(adreno_dev->scheduler_worker->task);')
  elif name=='events':
   start=text.index('\tdevice->events_worker = kthread_create_worker(');end=text.index('\tstatus = kgsl_reclaim_init();')
   body=text[start:end].replace('goto error_pwrctrl_close;','return status;')
   text='static int events(struct kgsl_device *device) { int status;complete_setup=true;'+body+'return 0;}'
  else:
   start=text.index('\tINIT_WORK(&pwrscale->devfreq_suspend_ws');end=text.rindex('\treturn 0;')
   body=text[start:end].replace('\twake_up_process(pwrscale->devfreq_notify_worker->task);','\tcheck_scale(pwrscale);\n\twake_up_process(pwrscale->devfreq_notify_worker->task);')
   text='static int scale(struct kgsl_pwrscale *pwrscale) {'+body+'return 0;}'
  funcs.append(text)
 tests=r'''
int main(void){unsigned cases=0;
 for(unsigned mode=0;mode<5;mode++)for(unsigned failed=0;failed<2;failed++)for(unsigned option=0;option<2;option++){
 create_fail=failed;lsr=option;complete_setup=false;wakes=0;
 struct disp_feature df={0};struct adreno_device ad={0};struct kgsl_pwrscale ps={0};int ret=0;
 struct adreno_hwsched_ops target={0};typeof(*ad.dev.dev) devnode={0};ad.dev.dev=&devnode;
 if(mode==0)ret=mi_disp_feature_thread_create(&df,0);
 if(mode==1)ret=dispatch(&ad);
 if(mode==2)ret=adreno_hwsched_init(&ad,&target);
 if(mode==3)ret=events(&ad.dev);
 if(mode==4){ps.devfreq_notify_worker=kthread_create_worker(0,"scale");if(failed)ret=-ENOMEM;else ret=scale(&ps);}
 assert(ret==(failed?-ENOMEM:0));assert(wakes==(failed?0:1));
 if(!failed){assert(task.started && task.fifo);/* Worker enters interruptible idle after startup. */task.started=false;}
 if(!failed)free(ad.hwsched.ctxt_bad);cases++;
 }
 create_fail=false;assert(mi_disp_feature_thread_create(NULL,0)==-EINVAL);struct disp_feature df={0};assert(mi_disp_feature_thread_create(&df,2)==-EINVAL);
 printf("PASS: %u worker startup, setup/priority, create-error, LSR cases plus invalid-display checks\n",cases);
}
'''
 source=a+b+'\n'.join(funcs)+tests
 p=H/(variant+'-test.c');p.write_text(source);exe=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-misleading-indentation','-g','-O1','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True);results[variant]={'returncode':run.returncode,'stdout':run.stdout,'stderr':run.stderr[:3000]}
 assert (run.returncode==0)==(variant=='candidate'),results
# Verify no additional imported worker users are missed.
all_calls=[]
for p in (W/'onyx').rglob('*.c'):
 for line in p.read_text(errors='replace').splitlines():
  if 'kthread_create_worker(' in line:all_calls.append(str(p.relative_to(W/'onyx')))
assert sorted(all_calls)==sorted(v[0] for v in paths.values()),all_calls
report={'status':'PASS','cases':20,'scope':'Actual feature/hwsched functions and actual contiguous dispatcher/events/devfreq setup excerpts with lifecycle API adapters; no hardware result inferred','results':results,'worker_callsite_audit':all_calls,'source_sha256':{p:hashlib.sha256((H/p).read_bytes()).hexdigest() for p,_ in paths.values()}}
(H/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(results['candidate']['stdout']);print('PASS: original unstarted worker rejected; all five worker creation callers audited')
