#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define CONFIG_AUDIO_PRM 1
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define U32_MAX UINT32_MAX
#define EXPORT_SYMBOL(x)
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p)>=(uintptr_t)-4095)
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define pr_err_ratelimited(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_entry(p,t,m) container_of(p,t,m)
#define LIST_HEAD(name) struct list_head name={&(name),&(name)}
#define list_for_each_entry(p,h,m) for((p)=list_entry((h)->next,__typeof__(*(p)),m); &(p)->m!=(h); (p)=list_entry((p)->m.next,__typeof__(*(p)),m))
#define list_for_each_entry_safe(p,n,h,m) for((p)=list_entry((h)->next,__typeof__(*(p)),m),(n)=list_entry((p)->m.next,__typeof__(*(p)),m); &(p)->m!=(h); (p)=(n),(n)=list_entry((n)->m.next,__typeof__(*(n)),m))
typedef uint32_t u32;
struct list_head {struct list_head *next,*prev;};
static void list_add_tail(struct list_head *n,struct list_head *h)
{struct list_head *prev=h->prev;h->prev=n;n->next=h;n->prev=prev;prev->next=n;}
static void list_del(struct list_head *n) {n->prev->next=n->next;n->next->prev=n->prev;}
struct mutex {int depth;};
#define DEFINE_MUTEX(name) struct mutex name={0}
static void mutex_lock(struct mutex *m){assert(!m->depth);m->depth=1;}
static void mutex_unlock(struct mutex *m){assert(m->depth==1);m->depth=0;}
static unsigned int live_allocations;static bool allocation_failure;
static void *kzalloc(size_t size,int flags)
{(void)flags;if(allocation_failure)return NULL;void *p=calloc(1,size);assert(p);live_allocations++;return p;}
static void kfree(void *p){assert(live_allocations);live_allocations--;free(p);}
struct device {int dummy;};
struct clk_cfg {u32 clk_id,clk_freq_in_hz,clk_attri,clk_root;};
struct clk_hw {int dummy;};
struct audio_ext_clk_priv {struct clk_hw hw;int enable,clk_src;struct clk_cfg prm_clk_cfg;};
#define to_audio_clk(hw) container_of(hw,struct audio_ext_clk_priv,hw)
#define HW_CORE_ID_LPASS 1
#define HW_CORE_ID_DCODEC 2
static unsigned int core_votes,audio_votes,request_calls,release_calls;
static int firmware_error,ccf_error,fail_after_prepare=-1;
static int audio_prm_set_lpass_hw_core_req(struct clk_cfg *cfg,int core,int enable)
{
    (void)cfg;assert(core==HW_CORE_ID_LPASS || core==HW_CORE_ID_DCODEC);
    if(enable)request_calls++;else release_calls++;
    if(enable && firmware_error)return firmware_error;
    unsigned int *v=core==HW_CORE_ID_LPASS?&core_votes:&audio_votes;
    if(enable)(*v)++;else{assert(*v);(*v)--;}
    assert(*v<=1);return 0;
}
#include "provider.inc"
struct clk_core {struct audio_ext_clk_priv provider;unsigned int users;};
struct clk {struct clk_core *core;unsigned int users;};
static unsigned int ccf_prepare_calls,ccf_unprepare_calls;
static int clk_prepare_enable(struct clk *c)
{
    assert(!IS_ERR_OR_NULL(c));ccf_prepare_calls++;
    if(ccf_error)return ccf_error;
    if(fail_after_prepare==0)return -EIO;
    if(fail_after_prepare>0)fail_after_prepare--;
    if(!c->core->users){int rc=lpass_hw_vote_prepare(&c->core->provider.hw);if(rc)return rc;}
    c->users++;c->core->users++;return 0;
}
static void clk_disable_unprepare(struct clk *c)
{
    assert(!IS_ERR_OR_NULL(c) && c->users && c->core->users);ccf_unprepare_calls++;
    c->users--;if(!--c->core->users)lpass_hw_vote_unprepare(&c->core->provider.hw);
}
#include "manager.inc"
typedef struct {int count;} atomic_t;
static int atomic_read(atomic_t *p){return p->count;}
static void atomic_inc(atomic_t *p){p->count++;}
static bool atomic_dec_and_test(atomic_t *p){return --p->count==0;}
struct msm_common_pdata {struct clk *lpass_audio_hw_vote;struct mutex hw_vote_lock;atomic_t lpass_audio_hw_vote_ref_cnt;};
#include "common.inc"

static struct clk_core audiocore={.provider.clk_src=AUDIO_EXT_CLK_LPASS_AUDIO_HW_VOTE};
static struct clk_core lpasscore={.provider.clk_src=AUDIO_EXT_CLK_LPASS_CORE_HW_VOTE};
static struct clk audio={.core=&audiocore},other={.core=&audiocore},core={.core=&lpasscore};
static struct device dev;
static unsigned int checks;
static void balanced(void)
{
    assert(!live_allocations && hw_votes.next==&hw_votes && hw_votes.prev==&hw_votes);
    assert(!audio.users && !other.users && !core.users && !audiocore.users && !lpasscore.users);
    assert(!audio_votes && !core_votes && !audiocore.provider.enable && !lpasscore.provider.enable);
    assert(!hw_vote_lock.depth);checks++;
}
static void reset(void)
{
    digital_cdc_rsc_mgr_exit();balanced();digital_cdc_rsc_mgr_init();
    allocation_failure=false;firmware_error=ccf_error=0;fail_after_prepare=-1;
    request_calls=release_calls=ccf_prepare_calls=ccf_unprepare_calls=0;
}
int main(void)
{
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==-ENODEV);
    assert(!ccf_prepare_calls && !live_allocations);digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);balanced();
    digital_cdc_rsc_mgr_init();
    assert(digital_cdc_rsc_mgr_hw_vote_enable(NULL,NULL)==-EINVAL);
    assert(digital_cdc_rsc_mgr_hw_vote_enable(ERR_PTR(-ENOENT),NULL)==-EINVAL);
    digital_cdc_rsc_mgr_hw_vote_disable(NULL,NULL);digital_cdc_rsc_mgr_hw_vote_disable(ERR_PTR(-ENOENT),NULL);balanced();
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==0);
    assert(audio.users==1 && audio_votes==1 && request_calls==1);
    struct hw_vote_entry *v=hw_vote_find(&audio);assert(v && v->requested==1 && v->enabled==1);
    digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);assert(release_calls==1);balanced();
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&core,&dev)==0);
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,&dev)==0);
    assert(core_votes==1 && audio_votes==1);digital_cdc_rsc_mgr_hw_vote_disable(&core,&dev);digital_cdc_rsc_mgr_hw_vote_disable(&audio,&dev);balanced();
    for(unsigned int nested=1;nested<=128;nested++){
        reset();struct msm_common_pdata p={.lpass_audio_hw_vote=&audio};
        for(unsigned int i=0;i<nested;i++)assert(mi2s_tdm_hw_vote_req(&p,1)==0);
        assert(p.lpass_audio_hw_vote_ref_cnt.count==(int)nested && ccf_prepare_calls==1 && audio_votes==1);
        for(unsigned int i=0;i<nested;i++)assert(mi2s_tdm_hw_vote_req(&p,0)==0);
        assert(!p.lpass_audio_hw_vote_ref_cnt.count && ccf_unprepare_calls==1);
        assert(mi2s_tdm_hw_vote_req(&p,0)==0);balanced();
    }
    reset();struct msm_common_pdata p={.lpass_audio_hw_vote=&audio};
    allocation_failure=true;assert(mi2s_tdm_hw_vote_req(&p,1)==-ENOMEM);assert(!p.lpass_audio_hw_vote_ref_cnt.count);allocation_failure=false;balanced();
    for(unsigned int i=0;i<3;i++){
        reset();int error=i==0?-EIO:i==1?-ETIMEDOUT:-EPROBE_DEFER;
        if(i<2)firmware_error=error;else ccf_error=error;
        assert(mi2s_tdm_hw_vote_req(&p,1)==error);assert(!p.lpass_audio_hw_vote_ref_cnt.count);balanced();
        firmware_error=ccf_error=0;assert(mi2s_tdm_hw_vote_req(&p,1)==0);assert(mi2s_tdm_hw_vote_req(&p,0)==0);balanced();
    }
    reset();assert(clk_prepare_enable(&other)==0);
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==0);
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==0);
    assert(audiocore.users==3 && request_calls==1);
    digital_cdc_rsc_mgr_hw_vote_reset(&audio);
    assert(audio.users==2 && other.users==1 && audiocore.users==3 && audio_votes==1 && release_calls==0);
    digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);
    assert(other.users==1 && audiocore.users==1 && audio_votes==1 && release_calls==0);
    clk_disable_unprepare(&other);balanced();
    reset();for(unsigned int i=0;i<3;i++)assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==0);
    fail_after_prepare=1;digital_cdc_rsc_mgr_hw_vote_reset(&audio);v=hw_vote_find(&audio);
    assert(v && v->requested==3 && v->enabled==1 && audio.users==1 && audio_votes==1);
    digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);assert(v->requested==2 && v->enabled==1);
    digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);assert(v->requested==1 && v->enabled==1);
    digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);balanced();fail_after_prepare=-1;
    reset();assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==0);v=hw_vote_find(&audio);v->requested=UINT32_MAX;
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==-EOVERFLOW);assert(audio.users==1);v->requested=1;digital_cdc_rsc_mgr_hw_vote_disable(&audio,NULL);balanced();
    reset();assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==0);assert(digital_cdc_rsc_mgr_hw_vote_enable(&core,&dev)==0);digital_cdc_rsc_mgr_exit();balanced();
    assert(digital_cdc_rsc_mgr_hw_vote_enable(&audio,NULL)==-ENODEV);balanced();
    printf("PASS: %u lifecycle/accounting checks; NULL-device common caller reaches real LPASS provider/PRM; nested votes1..128; SSR/failure/refcount controls\n",checks);
}
