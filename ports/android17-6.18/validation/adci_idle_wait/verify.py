#!/usr/bin/env python3
"""Exercise the extracted Si/SCM WAITQ path and ADCI-only classification."""
from pathlib import Path
import json,hashlib,subprocess
H=Path(__file__).resolve().parent
def fn(s,sig):
 start=s.index(sig);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
def read(p,before=False):return (H/(p+('.before' if before else ''))).read_text()
a=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define EPROBE_DEFER 517
#define TASK_RUNNING 0U
#define TASK_UNINTERRUPTIBLE 2U
#define TASK_NOLOAD 1024U
#define TASK_IDLE (TASK_UNINTERRUPTIBLE|TASK_NOLOAD)
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR_OR_NULL(x) (!(x) || (uintptr_t)(x)>=(uintptr_t)-4095)
#define QCOM_SCM_WAITQ_SLEEP 1U
#define QCOM_SCM_WAITQ_WAKE 2U
#define QCOM_SCM_V2_EBUSY 3U
#define IS_WAITQ_SLEEP_OR_WAKE(r) ((r)->a0==QCOM_SCM_WAITQ_SLEEP || (r)->a0==QCOM_SCM_WAITQ_WAKE)
#define QCOM_SCM_EBUSY_MAX_RETRY 2
#define QCOM_SCM_EBUSY_WAIT_MS 30
#define QCOM_SCM_SVC_SMCINVOKE 10
#define QCOM_SCM_SMCINVOKE_INVOKE 1
#define QCOM_SCM_SMCINVOKE_CB_RSP 2
#define ARM_SMCCC_OWNER_TRUSTED_OS 48
#define QCOM_SCM_ARGS(...) 0
#define QCOM_SCM_RW 1
#define QCOM_SCM_VAL 0
#define SCM_EBUSY_MAX_RETRY 3
#define SCM_EBUSY_WAIT_MS 30
#define OIC_FLAG_BUSY 1
#define OIC_FLAG_NOTIFY 2
#define OIC_FLAG_QTEE 4
#define OIC_FLAG_ADCI_ACCEPT 8
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define SI_OT_USER 1
#define SI_OT_ROOT 2
#define ROOT_SI_OBJECT (&root_object)
typedef uint64_t u64;
typedef uint32_t u32;
typedef uintptr_t phys_addr_t;
struct completion { unsigned done; };
struct qcom_scm { struct completion wait; };
struct device { struct qcom_scm *scm; };
struct arm_smccc_args { unsigned long a0,a1,a2,a3; };
struct arm_smccc_res { unsigned long a0,a1,a2,a3; };
struct qcom_scm_desc { unsigned svc,cmd,arginfo;u64 args[10];unsigned owner;bool multicall_allowed,waitq_idle; };
struct qcom_scm_res { u64 result[3]; };
enum qcom_scm_call_type { QCOM_SCM_CALL_ATOMIC, QCOM_SCM_CALL_NORETRY,QCOM_SCM_CALL_NORMAL };
struct si_object {int type;};
static struct si_object root_object={SI_OT_ROOT};
struct si_arg { int unused; };
struct si_object_invoke_ctx { unsigned flags;struct {phys_addr_t paddr;struct {size_t size;}msg;} in,out; };
static struct qcom_scm qscm;
static struct device device={.scm=&qscm};
static struct {struct device *dev;} scm_global={.dev=&device};
static typeof(scm_global) *__scm=&scm_global;
static unsigned sequence[16],seq_len,seq_pos,waits,wakes,lock_depth,sem_depth,retries,last_cmd;
static unsigned expected_state,current_state,schedules;
static bool expected_idle,invalid_wq,allow_multi;
static int qcom_scm_lock,qcom_scm_sem_lock;
static void __scm_smc_do_quirk(struct arm_smccc_args *args,struct arm_smccc_res *r) {
 (void)args;assert(seq_pos<seq_len);r->a0=sequence[seq_pos++];r->a1=9;r->a2=10;r->a3=11;
}
static struct qcom_scm *dev_get_drvdata(struct device *d){return d->scm;}
static struct completion *qcom_scm_lookup_wq(struct qcom_scm *s,unsigned ctx){assert(ctx==9);return invalid_wq?ERR_PTR(-EINVAL):&s->wait;}
static int wait_for_completion_state(struct completion *c,unsigned state){assert(c==&qscm.wait && state==expected_state);assert(sem_depth==1);waits++;return 0;}
static void wait_for_completion(struct completion *c){wait_for_completion_state(c,TASK_UNINTERRUPTIBLE);}
static void fill_wq_resume_args(struct arm_smccc_args *a,unsigned ctx){assert(ctx==10);a->a0=100;}
static void fill_wq_wake_ack_args(struct arm_smccc_args *a,unsigned ctx){assert(ctx==10);a->a0=101;}
static void scm_waitq_flag_handler(struct completion *c,unsigned f){assert(c==&qscm.wait && f==11);wakes++;}
static bool qcom_scm_multi_call_allow(struct device *d,bool m){(void)d;return allow_multi && m;}
static void mutex_lock(int *p){(void)p;assert(lock_depth==0);lock_depth++;}
static void mutex_unlock(int *p){(void)p;assert(lock_depth==1);lock_depth--;}
static void down(int *p){(void)p;assert(sem_depth==0);sem_depth++;}
static void up(int *p){(void)p;assert(sem_depth==1);sem_depth--;}
static void msleep(unsigned ms){assert(ms==30);retries++;}
static int qcom_scm_call_noretry(struct device *d,const struct qcom_scm_desc *desc,struct qcom_scm_res *res);
static int typeof_si_object(struct si_object *o){return o?o->type:0;}
static int si_object_invoke_ctx_init(struct si_object_invoke_ctx *o,struct si_arg *args){(void)args;memset(o,0,sizeof(*o));return 0;}
static int prepare_msg(struct si_object_invoke_ctx *o,struct si_object *object,unsigned long op,struct si_arg *u){(void)o;(void)object;(void)op;(void)u;return 0;}
static void set_current_state(unsigned state){current_state=state;}
static void __set_current_state(unsigned state){current_state=state;}
static bool kthread_should_stop(void){return schedules==3;}
static void schedule(void){assert(current_state==TASK_IDLE);schedules++;}
'''
results={}
for variant in ['candidate','original','blanket_idle','uninterruptible_adci','tag_before_context_reset','callback_loses_idle']:
 before=variant=='original';smc=read('drivers/firmware/qcom_scm-smc.c',before)
 quirk=fn(smc,'static int scm_smc_do_quirk(');loop=fn(smc,'static int __scm_smc_do(')
 if before:
  # Adapt only the old function signatures to the new host transport ABI;
  # leave the original unconditional WAITQ sleep unchanged.
  quirk=quirk.replace('struct arm_smccc_res *res)\n{','struct arm_smccc_res *res, bool waitq_idle)\n{')
  loop=loop.replace('bool multicall_allowed)\n{','bool multicall_allowed, bool waitq_idle)\n{').replace('scm_smc_do_quirk(dev, smc, res)','scm_smc_do_quirk(dev, smc, res, waitq_idle)')
 if variant=='uninterruptible_adci':quirk=quirk.replace('waitq_idle ?\n\t\t\t\t\tTASK_IDLE : TASK_UNINTERRUPTIBLE','TASK_UNINTERRUPTIBLE')
 if variant=='blanket_idle':quirk=quirk.replace('waitq_idle ?\n\t\t\t\t\tTASK_IDLE : TASK_UNINTERRUPTIBLE','TASK_IDLE')
 code=a+quirk+'\n'+loop+r'''
static int qcom_scm_call_noretry(struct device *d,const struct qcom_scm_desc *desc,struct qcom_scm_res *res){
 assert(desc->waitq_idle==expected_idle);assert(desc->svc==QCOM_SCM_SVC_SMCINVOKE && desc->multicall_allowed);
 last_cmd=desc->cmd;struct arm_smccc_args args={0};struct arm_smccc_res r={0};
 int ret=__scm_smc_do(d,&args,&r,QCOM_SCM_CALL_NORETRY,desc->multicall_allowed,desc->waitq_idle);
 if(!ret && r.a0==QCOM_SCM_V2_EBUSY)ret=-EBUSY;
 if(!ret && (long)r.a0<0)ret=(int)r.a0;
 res->result[0]=17;res->result[1]=18;res->result[2]=19;
 return ret;
}
'''
 transport=read('drivers/firmware/qcom_scm.c')
 for sig in ['static int __qcom_scm_invoke_smc(','int qcom_scm_invoke_smc(','int qcom_scm_invoke_smc_adci(',
 'static int __qcom_scm_invoke_callback_response(','int qcom_scm_invoke_callback_response(','int qcom_scm_invoke_callback_response_adci(']:code+=fn(transport,sig)+'\n'
 invoke=fn(read('drivers/firmware/qcom/si_core/qcom_scm_invoke.c',before),'int si_object_invoke_ctx_invoke(')
 if variant=='callback_loses_idle':invoke=invoke.replace('qcom_scm_invoke_callback_response_adci :','qcom_scm_invoke_callback_response :')
 code+=invoke+'\n'
 core=fn(read('drivers/firmware/qcom/si_core/si_core.c',before),'int si_object_do_invoke(')
 prefix=core[:core.index('\t/* INVOKE remote object!! */')]
 # Exact context-init/classification/preparation sequence; later marshaling is
 # compiled in the full build, exercised via actual transport below.
 prefix=prefix.replace('\t\tgoto out;','\t\treturn ret;')
 if variant=='tag_before_context_reset':
  mark='\t/* IClientEnv_OP_adciAccept is an indefinite asynchronous service. */\n\tif (object == ROOT_SI_OBJECT && op == 8)\n\t\toic->flags |= OIC_FLAG_ADCI_ACCEPT;\n\n'
  prefix=prefix.replace(mark,'').replace('\tret = si_object_invoke_ctx_init(oic, u);',mark+'\tret = si_object_invoke_ctx_init(oic, u);')
 code+=prefix+'\treturn 0;\n}\n'+fn(read('drivers/firmware/qcom/si_core/si_core_adci.c',before),'static void wait_to_die(')
 tests=r'''
int main(void){unsigned count=0;
 for(unsigned object_id=0;object_id<3;object_id++)for(unsigned op=0;op<11;op++)
 for(unsigned busy=0;busy<2;busy++)for(unsigned multi=0;multi<2;multi++)for(unsigned scenario=0;scenario<4;scenario++){
 struct si_object user={SI_OT_USER},other_root={SI_OT_ROOT};
 struct si_object *object=object_id==0?ROOT_SI_OBJECT:object_id==1?&user:&other_root;
 struct si_arg u[1]={{0}};struct si_object_invoke_ctx o;memset(&o,255,sizeof(o));int result=0;u64 response=0;unsigned data=0;
 assert(si_object_do_invoke(&o,object,op,u,&result)==0);
 expected_idle=object==ROOT_SI_OBJECT && op==8;expected_state=expected_idle?TASK_IDLE:TASK_UNINTERRUPTIBLE;
 assert(!!(o.flags&OIC_FLAG_ADCI_ACCEPT)==expected_idle);
 if(busy)o.flags|=OIC_FLAG_BUSY;
 allow_multi=multi;invalid_wq=scenario==3;seq_pos=waits=wakes=retries=0;lock_depth=sem_depth=0;
 sequence[0]=QCOM_SCM_WAITQ_SLEEP;sequence[1]=QCOM_SCM_WAITQ_WAKE;sequence[2]=QCOM_SCM_WAITQ_SLEEP;sequence[3]=0;seq_len=4;
 if(scenario==1){sequence[0]=QCOM_SCM_V2_EBUSY;sequence[1]=QCOM_SCM_WAITQ_SLEEP;sequence[2]=0;seq_len=3;}
 if(scenario==2){sequence[0]=(unsigned)-EINVAL;seq_len=1;}
 int ret=si_object_invoke_ctx_invoke(&o,&result,&response,&data);
 assert(ret==((scenario==2||scenario==3)?-EINVAL:0));assert(lock_depth==0 && sem_depth==0);
 assert(last_cmd==(busy?QCOM_SCM_SMCINVOKE_CB_RSP:QCOM_SCM_SMCINVOKE_INVOKE));
 assert(waits==(scenario==0?2:scenario==1?1:0));assert(wakes==(scenario==0?1:0));assert(retries==(scenario==1?1:0));
 assert(result==18 && response==17 && data==19);count++;
 }
 schedules=0;wait_to_die();assert(schedules==3 && current_state==TASK_RUNNING);
 printf("PASS: %u root/user/op/continuation/multicall/WAITQ/error/retry cases and ADCI stop wait\n",count);
}
'''
 # Production converts negative unsigned firmware register back to signed long
 # on arm64; use explicit sign extension in the host SMC register adapter.
 code=code.replace('r->a0=sequence[seq_pos++];','r->a0=(unsigned long)(long)(int)sequence[seq_pos++];')
 p=H/(variant+'-test.c');p.write_text(code+tests);exe=p.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wno-unused-parameter','-O1','-g','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True);results[variant]={'returncode':run.returncode,'stdout':run.stdout,'stderr':run.stderr[:3000]}
 assert (run.returncode==0)==(variant=='candidate'),(variant,results)
report={'status':'PASS','cases':528,'scope':'Actual SCM WAITQ and retry functions, exported invocation wrappers, Si transport, context setup prefix and ADCI stop function, with host SMC/completion adapters; hardware pending','negative_controls':['original','blanket_idle','tag_before_context_reset','callback_loses_idle'],'results':results,'source_sha256':{str(p.relative_to(H)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(H.rglob('*')) if p.suffix in ['.c','.h'] and '-test' not in p.name}}
(H/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(results['candidate']['stdout']);print('PASS: original, blanket idle, erased classification and lost continuation classification rejected')
