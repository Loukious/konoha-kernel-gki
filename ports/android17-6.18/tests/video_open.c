#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
#define MSM_VIDC_DECODER 0
#define MSM_VIDC_ENCODER 1
#define MSM_VIDC_OPEN 2
#define MSM_VIDC_SUB_STATE_NONE 0
#define DEFAULT_FPS 30
#define MAX_SIGNAL 4
#define d_vpr_h(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define i_vpr_h(...) ((void)0)
#define i_vpr_e(...) ((void)0)
#define strscpy(d,s,n) snprintf(d,n,"%s",s)
struct list { bool initialized; };
struct completion { bool initialized; };
struct work { bool initialized; };
struct workqueue { bool initialized; };
struct buffer { struct list list; };
struct buffer_group { struct buffer input, input_meta, output, output_meta, read_only, bin, arp, comv, non_comv, line, dpb, persist, vpss, partial_data; };
struct msm_vidc_core { void *debugfs_root; int live; bool deinit; };
struct msm_vidc_inst {
    struct msm_vidc_core *core;
    u32 domain, session_id, sub_state;
    char sub_state_name[32];
    bool active, request, ipsc_properties_set, opsc_properties_set, caps_list_prepared, has_bframe, iframe;
    unsigned long auto_framerate, initial_time_us;
    int lock, ctx_q_lock, client_lock;
    struct list caps_list, children_list, firmware_list, enc_input_crs, dmabuf_tracker, input_timer_list, pending_pkts, fence_list, buffer_stats_list;
    struct buffer timestamps, ts_reorder;
    struct buffer_group buffers, mem_info;
    struct completion completions[MAX_SIGNAL];
    struct workqueue *workq;
    struct work stats_work, stability_work;
    void *debugfs_root;
    bool pools, fh, vb2, codec, fence, published, drained;
};
static int fail, allocations, queues;
static struct msm_vidc_inst *current;
static int msm_vidc_core_init(struct msm_vidc_core *c) { assert(!c->live); return fail==1 ? -1 : 0; }
static int msm_vidc_core_init_wait(struct msm_vidc_core *c) { return fail==2 ? -1 : 0; }
static void *vzalloc(size_t n) { if (fail==3) return NULL; allocations++; return calloc(1,n); }
static unsigned int hash32_ptr(void *p) { return 1; }
static unsigned long ktime_get_ns(void) { return 10000; }
static void msm_vidc_update_state(struct msm_vidc_inst *i,int state,const char *s) { assert(state==MSM_VIDC_OPEN); }
static void msm_vidc_init_inst_ref(struct msm_vidc_inst *i) { i->core->live++; current=i; }
static void mutex_init(int *lock) { assert(!*lock); *lock=1; }
static void mutex_destroy(int *lock) { assert(*lock); *lock=0; }
static void msm_vidc_update_debug_str(struct msm_vidc_inst *i) { }
static void msm_vidc_remove_session(struct msm_vidc_inst *i) { i->published=false; }
static void msm_vidc_remove_dangling_session(struct msm_vidc_inst *i) { }
static void msm_vidc_drain_inst_refs(struct msm_vidc_inst *i) { i->published=false; i->drained=true; }
static int msm_vidc_pools_init(struct msm_vidc_inst *i) { assert(!i->published); if (fail==4) return -1; i->pools=true; return 0; }
static void msm_vidc_pools_deinit(struct msm_vidc_inst *i) { assert(i->drained && i->pools && !queues && !i->fh && !i->vb2 && !i->codec && !i->fence); i->pools=false; }
#define INIT_LIST_HEAD(p) ((p)->initialized=true)
static void init_completion(struct completion *c) { c->initialized=true; }
static void check_lists(struct msm_vidc_inst *i)
{
    assert(i->caps_list.initialized && i->timestamps.list.initialized && i->ts_reorder.list.initialized);
    struct buffer *groups[2]={(struct buffer *)&i->buffers,(struct buffer *)&i->mem_info};
    assert(i->buffers.input.list.initialized && i->buffers.input_meta.list.initialized && i->buffers.output.list.initialized && i->buffers.output_meta.list.initialized && i->buffers.read_only.list.initialized);
    for (int n=0;n<2;n++) for (int j=5;j<14;j++) assert(groups[n][j].list.initialized);
    struct list *list[]={&i->children_list,&i->firmware_list,&i->enc_input_crs,&i->dmabuf_tracker,&i->input_timer_list,&i->pending_pkts,&i->fence_list,&i->buffer_stats_list};
    for (int n=0;n<8;n++) assert(list[n]->initialized);
    for (int n=0;n<MAX_SIGNAL;n++) assert(i->completions[n].initialized);
}
static struct workqueue *create_singlethread_workqueue(const char *name) { check_lists(current); if (fail==5) return NULL; queues++; return calloc(1,sizeof(struct workqueue)); }
static void destroy_workqueue(struct workqueue *q) { assert(current->drained && !current->codec && !current->vb2 && !current->fh); assert(q && queues==1); queues--; free(q); }
#define INIT_DELAYED_WORK(w,f) ((w)->initialized=true)
#define INIT_WORK(w,f) ((w)->initialized=true)
static int msm_vidc_v4l2_fh_init(struct msm_vidc_inst *i) { assert(!i->published && i->workq && i->stats_work.initialized && i->stability_work.initialized); if (fail==6) return -1; i->fh=true; return 0; }
static void msm_vidc_v4l2_fh_deinit(struct msm_vidc_inst *i) { assert(i->drained && i->fh && !i->vb2); i->fh=false; }
static int msm_vidc_vb2_queue_init(struct msm_vidc_inst *i) { assert(!i->published && i->fh); if (fail==7) return -1; i->vb2=true; return 0; }
static void msm_vidc_vb2_queue_deinit(struct msm_vidc_inst *i) { assert(i->drained && i->vb2 && !i->codec); i->vb2=false; }
static bool is_decode_session(struct msm_vidc_inst *i) { return i->domain==MSM_VIDC_DECODER; }
static bool is_encode_session(struct msm_vidc_inst *i) { return i->domain==MSM_VIDC_ENCODER; }
static int codec_init(struct msm_vidc_inst *i) { assert(!i->published && i->vb2); i->codec=true; return fail==8 ? -1 : 0; }
static int msm_vdec_inst_init(struct msm_vidc_inst *i) { assert(is_decode_session(i)); return codec_init(i); }
static int msm_venc_inst_init(struct msm_vidc_inst *i) { assert(is_encode_session(i)); return codec_init(i); }
static void codec_deinit(struct msm_vidc_inst *i) { assert(i->drained && i->codec && !i->fence); i->codec=false; }
static void msm_vdec_inst_deinit(struct msm_vidc_inst *i) { assert(is_decode_session(i)); codec_deinit(i); }
static void msm_venc_inst_deinit(struct msm_vidc_inst *i) { assert(is_encode_session(i)); codec_deinit(i); }
static int msm_vidc_fence_init(struct msm_vidc_inst *i) { assert(!i->published && i->codec); if (fail==9) return -1; i->fence=true; return 0; }
static void msm_vidc_fence_deinit(struct msm_vidc_inst *i) { assert(i->drained && i->fence); i->fence=false; }
static int msm_vidc_add_session(struct msm_vidc_inst *i) { check_lists(i); assert(i->pools && i->workq && i->fh && i->vb2 && i->codec && i->fence && !i->published); if (fail==10) return -1; i->published=true; return 0; }
static int msm_vidc_set_crc(struct msm_vidc_core *c) { assert(current->published); return fail==11 ? -1 : 0; }
static void msm_vidc_reset_residency_stats(struct msm_vidc_core *c) { assert(current->published); }
static void msm_vidc_scale_power(struct msm_vidc_inst *i,bool enable) { assert(i->published && enable); }
static int msm_vidc_session_open(struct msm_vidc_inst *i) { assert(i->published); return fail==12 ? -1 : 0; }
static void msm_vidc_core_deinit(struct msm_vidc_core *c,bool force) { assert(force); c->deinit=true; }
static void *msm_vidc_debugfs_init_inst(struct msm_vidc_inst *i,void *root) { assert(i->published); return fail==13 ? NULL : i; }
static void msm_vidc_free_inst(struct msm_vidc_inst *i) { assert(i->drained && !i->published && !i->pools && !i->fh && !i->vb2 && !i->codec && !i->fence && !queues); assert(!i->lock && !i->ctx_q_lock && !i->client_lock); i->core->live--; allocations--; free(i); current=NULL; }
/* SOURCE */
int main(void)
{
    struct msm_vidc_core core={0};
    assert(!msm_vidc_open(&core,99));
    for (u32 domain=MSM_VIDC_DECODER;domain<=MSM_VIDC_ENCODER;domain++) {
        for (fail=1;fail<=12;fail++) {
            core.deinit=false;
            assert(!msm_vidc_open(&core,domain));
            assert(!core.live && !allocations && !queues);
            if (fail==12) assert(core.deinit);
        }
        for (int optional=0;optional<2;optional++) {
            fail=optional ? 13 : 0;
            struct msm_vidc_inst *i=msm_vidc_open(&core,domain);
            assert(i && i->published && i->core==&core && core.live==1);
            assert((i->debugfs_root==NULL)==optional);
            assert(i->active && i->auto_framerate==(DEFAULT_FPS<<16));
            msm_vidc_drain_inst_refs(i); msm_vidc_fence_deinit(i); codec_deinit(i); msm_vidc_vb2_queue_deinit(i); msm_vidc_v4l2_fh_deinit(i); destroy_workqueue(i->workq); msm_vidc_pools_deinit(i);
            mutex_destroy(&i->client_lock); mutex_destroy(&i->ctx_q_lock); mutex_destroy(&i->lock); msm_vidc_free_inst(i);
            assert(!core.live && !allocations && !queues);
        }
    }
    puts("29 full-open acquisition, publication and partial-unwind cases passed");
    return 0;
}
