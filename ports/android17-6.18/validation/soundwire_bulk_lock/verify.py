#!/usr/bin/env python3
"""Execute production bulk paths with nonrecursive mutex and transport adapters."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

H = Path(__file__).resolve().parent
W = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
LIVE = W / 'onyx/qcom/opensource/audio-kernel/soc/swr-mstr-ctrl.c'
candidate = LIVE.read_text()
assert candidate == (H / 'candidate.c').read_text()

def function(source, name):
    start = re.search(r'^static (?:int|u32) ' + name + r'\([^;]+?\n\{', source, re.M).start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

adapter = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define SWR_BROADCAST_CMD_ID 0x0f
#define SWR_REG_VAL_PACK(data,dev,id,reg) ((u32)(reg)|((u32)(id)<<16)|((u32)(dev)<<20)|((u32)(data)<<24))
#define GFP_KERNEL 0
#define SWRM_WR_CHECK_AVAIL 0
#define SWRM_CMD_FIFO_WR_CMD(ee) (0x4030+0x1000*(ee))
struct mutex { pthread_mutex_t native; pthread_t owner; bool held; };
static void mutex_init(struct mutex *m) {
    pthread_mutexattr_t attr;
    assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK));
    assert(!pthread_mutex_init(&m->native,&attr));
    assert(!pthread_mutexattr_destroy(&attr)); m->held=false;
}
static void mutex_lock(struct mutex *m) {
    int rc=pthread_mutex_lock(&m->native);
    assert(rc==0); m->owner=pthread_self(); m->held=true;
}
static void mutex_unlock(struct mutex *m) {
    assert(m->held&&pthread_equal(m->owner,pthread_self()));
    m->held=false; assert(!pthread_mutex_unlock(&m->native));
}
static void assert_held(struct mutex *m) {
    assert(m->held&&pthread_equal(m->owner,pthread_self()));
}
#define lockdep_assert_held(m) assert_held(m)
struct device { int unused; };
struct swr_mstr_ctrl;
struct swr_master { struct device dev; struct swr_mstr_ctrl *data; };
struct swr_mstr_ctrl {
    void *handle; struct device *dev; struct mutex iolock,devlock;
    bool dev_up; u8 wcmd_id; unsigned ee_val;
    int (*bulk_write)(void *,u32 *,u32 *,size_t);
};
static atomic_int allocations,alloc_calls,pm_refs,pm_gets,pm_puts,errors;
static int fail_alloc,resume_error,backend_error;
static unsigned writes,waits,bulk_calls,trace_count;
static u32 trace[50000];
#define dev_err_ratelimited(...) ((void)atomic_fetch_add(&errors,1))
static void *kcalloc(size_t n,size_t size,int flags) {
    (void)flags;
    int call=atomic_fetch_add(&alloc_calls,1)+1;
    if(call==fail_alloc)return NULL;
    void *p=calloc(n,size);assert(p);atomic_fetch_add(&allocations,1);return p;
}
static void kfree(void *p) {if(p){free(p);atomic_fetch_sub(&allocations,1);}}
static struct swr_mstr_ctrl *swr_get_ctrl_data(struct swr_master *m){return m->data;}
static int pm_runtime_resume_and_get(struct device *d) {
    assert(d);atomic_fetch_add(&pm_gets,1);
    if(resume_error)return resume_error;
    atomic_fetch_add(&pm_refs,1);return 0;
}
static void pm_runtime_put_autosuspend(struct device *d) {
    assert(d);assert(atomic_fetch_sub(&pm_refs,1)>0);atomic_fetch_add(&pm_puts,1);
}
static void pm_runtime_mark_last_busy(struct device *d){assert(d);}
static void swrm_wait_for_fifo_avail(struct swr_mstr_ctrl *s,int mode) {
    assert(mode==SWRM_WR_CHECK_AVAIL);assert_held(&s->iolock);waits++;
}
static void usleep_range(unsigned min,unsigned max) {assert(min<=max);sched_yield();}
static void swr_master_write(struct swr_mstr_ctrl *s,u16 reg,u32 val) {
    assert_held(&s->iolock);assert(reg==SWRM_CMD_FIFO_WR_CMD(s->ee_val));
    assert(trace_count<sizeof(trace)/sizeof(trace[0]));trace[trace_count++]=val;writes++;
}
static int backend(void *handle,u32 *regs,u32 *vals,size_t len) {
    struct swr_mstr_ctrl *s=handle;assert_held(&s->iolock);bulk_calls++;
    if(backend_error)return backend_error;
    for(size_t i=0;i<len;i++)swr_master_write(s,(u16)regs[i],vals[i]);
    return 0;
}
static void reset(void) {
    assert(!atomic_load(&allocations)&&!atomic_load(&pm_refs));
    alloc_calls=pm_gets=pm_puts=errors=0;
    fail_alloc=resume_error=backend_error=0;
    writes=waits=bulk_calls=trace_count=0;
}
static void init(struct swr_mstr_ctrl *s,struct device *d) {
    *s=(struct swr_mstr_ctrl){.dev=d,.dev_up=true,.ee_val=1};s->handle=s;
    mutex_init(&s->iolock);mutex_init(&s->devlock);
}
static void destroy(struct swr_mstr_ctrl *s) {
    assert(!s->iolock.held&&!s->devlock.held);
    assert(!pthread_mutex_destroy(&s->iolock.native));
    assert(!pthread_mutex_destroy(&s->devlock.native));
}
'''

main = r'''
struct worker { struct swr_master *m; unsigned seed; };
static void *worker(void *arg) {
    struct worker *w=arg;u16 regs[8];u8 data[8];
    for(unsigned i=0;i<8;i++){regs[i]=0x120+i;data[i]=(u8)(w->seed+i);}
    for(unsigned n=0;n<100;n++)assert(!swrm_bulk_write(w->m,3,regs,data,8));
    return NULL;
}
int main(void) {
    unsigned cases=0;
    for(unsigned backend_mode=0;backend_mode<2;backend_mode++)
    for(unsigned count=1;count<=32;count++)
    for(unsigned failure=0;failure<5;failure++) {
        struct device d={0};struct swr_mstr_ctrl s;init(&s,&d);
        struct swr_master m={.data=&s};u16 regs[32];u8 data[32];
        for(unsigned i=0;i<count;i++){regs[i]=0x80+i;data[i]=(u8)(0xe0+i);}
        reset();s.bulk_write=backend_mode?backend:NULL;
        if(failure==1)resume_error=-EHOSTDOWN;
        if(failure==2)fail_alloc=1;
        if(failure==3)fail_alloc=2;
        if(failure==4)backend_error=-EIO;
        int expected=failure==1?-EHOSTDOWN:((failure==2||failure==3)?-ENOMEM:((failure==4&&backend_mode)?-EIO:0));
        int rc=swrm_bulk_write(&m,3,regs,data,count);assert(rc==expected);
        assert(!atomic_load(&allocations)&&!atomic_load(&pm_refs));
        assert(atomic_load(&pm_gets)==1&&atomic_load(&pm_puts)==(failure!=1));
        assert(!s.iolock.held&&!s.devlock.held);
        if(!expected) {
            assert(writes==count&&trace_count==count);
            assert(bulk_calls==backend_mode&&waits==(backend_mode?0:count));
            for(unsigned i=0;i<count;i++) {
                unsigned id=(i+1)%15;
                assert(trace[i]==SWR_REG_VAL_PACK(data[i],3,id,regs[i]));
            }
        } else assert(!writes);
        destroy(&s);cases++;
    }
    for(unsigned backend_mode=0;backend_mode<2;backend_mode++) {
        struct device d={0};struct swr_mstr_ctrl s;init(&s,&d);
        struct swr_master m={.data=&s};u16 reg=0x55;u8 data=0x99;
        for(unsigned invalid=0;invalid<8;invalid++) {
            reset();s.bulk_write=backend_mode?backend:NULL;s.dev_up=invalid!=7;
            m.data=invalid==0?NULL:&s;s.handle=invalid==1?NULL:&s;
            int rc=swrm_bulk_write(&m,invalid==2?0:invalid==3?16:2,
                    invalid==4?NULL:&reg,invalid==5?NULL:&data,invalid==6?0:1);
            assert(rc==(invalid==7?-ENODEV:-EINVAL));
            assert(!atomic_load(&pm_gets)&&!writes&&!atomic_load(&allocations));cases++;
        }
        s.handle=&s;s.dev_up=true;m.data=&s;
        for(unsigned failure=0;failure<2;failure++) {
            reset();s.bulk_write=backend_mode?backend:NULL;backend_error=failure?-EREMOTEIO:0;
            u32 regs[]={SWRM_CMD_FIFO_WR_CMD(s.ee_val)},vals[]={0xabcdef01};
            int rc=swr_master_bulk_write(&s,regs,vals,1);
            assert(rc==((backend_mode&&failure)?-EREMOTEIO:0));
            assert(!s.iolock.held);cases++;
        }
        destroy(&s);
    }
    for(unsigned backend_mode=0;backend_mode<2;backend_mode++) {
        struct device d={0};struct swr_mstr_ctrl s;init(&s,&d);
        struct swr_master m={.data=&s};reset();s.bulk_write=backend_mode?backend:NULL;
        pthread_t threads[4];struct worker workers[4];
        for(unsigned i=0;i<4;i++){workers[i]=(struct worker){&m,0x50+i*0x10};assert(!pthread_create(&threads[i],NULL,worker,&workers[i]));}
        for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
        assert(trace_count==3200&&writes==3200&&!atomic_load(&pm_refs)&&!atomic_load(&allocations));
        for(unsigned batch=0;batch<400;batch++) {
            unsigned first=trace[batch*8]>>24;
            for(unsigned j=0;j<8;j++) {
                u32 val=trace[batch*8+j];
                assert((val>>24)==first+j);
                assert(((val>>16)&0xf)==(batch*8+j+1)%15);
                assert((val&0xffff)==0x120+j&&((val>>20)&0xf)==3);
            }
        }
        destroy(&s);cases++;
    }
    printf("PASS: %u transfer/error/invalid/internal/concurrent scenarios; 6400 concurrent MMIO/callback writes\n",cases);
    return 0;
}
'''

variants = {'candidate': candidate, 'original': (H / 'before.c').read_text()}
variants['recursive-public-call'] = candidate.replace(
    'ret = swr_master_bulk_write_locked(swrm, swr_fifo_reg, val, len);',
    'ret = swr_master_bulk_write(swrm, swr_fifo_reg, val, len);')
variants['discard-backend-error'] = candidate.replace(
    'return swrm->bulk_write(swrm->handle, reg_addr, val, length);',
    'return swrm->bulk_write(swrm->handle, reg_addr, val, length), 0;')
variants['unlocked-internal-path'] = candidate.replace(
    '\tmutex_lock(&swrm->iolock);\n\tret = swr_master_bulk_write_locked(swrm, reg_addr, val, length);\n\tmutex_unlock(&swrm->iolock);',
    '\tret = swr_master_bulk_write_locked(swrm, reg_addr, val, length);')
results = {}
for name, source in variants.items():
    names = ['swrm_get_packed_reg_val']
    if 'static int swr_master_bulk_write_locked(' in source:
        names += ['swr_master_bulk_write_locked']
    names += ['swr_master_bulk_write', 'swrm_bulk_write']
    code = adapter + '\n'.join(function(source, n) for n in names) + main
    path, exe = H / (name + '-test.c'), H / (name + '-test')
    path.write_text(code)
    subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Werror',
                    '-Wno-unused-function','-pthread','-fsanitize=address,undefined',
                    str(path),'-o',str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
    assert (result.returncode == 0) == (name == 'candidate'), (name, result.stderr)
    results[name] = {'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr}
report = {
    'status':'PASS','production_source':str(LIVE),
    'source_sha256':hashlib.sha256(LIVE.read_bytes()).hexdigest(),
    'results':results,'scenarios':342,'concurrent_writes':6400,
    'scope':'Complete production bulk helper, public callback, ID packer. Error-checking pthread mutex exposes recursive acquisition; callbacks assert lock ownership. Allocation, PM and callback failures; 4 concurrent producers on both backends validate contiguous command batches and IDs.',
    'limits':'No physical audio, SoundWire protocol/IRQ timing, real PM or netlink validation. FIFO and MMIO backend operations are host adapters. CMD Ignored origin remains unresolved.'
}
(H / 'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(results['candidate']['stdout'],end='')
print('Original and three regression controls rejected.')
