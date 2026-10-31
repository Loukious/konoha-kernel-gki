#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef uint32_t u32;
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define d_vpr_h(...) ((void)0)
#define d_vpr_e(...) ((void)0)
#define i_vpr_h(...) ((void)0)
#define i_vpr_e(...) ((void)0)
#define trace_msm_v4l2_vidc_open(...) ((void)0)
#define trace_msm_v4l2_vidc_close(...) ((void)0)
#define smp_load_acquire(p) (*(p))
#define MSM_VIDC_DECODER 1
#define MSM_VIDC_ENCODER 2
#define PD_FLAG_REQUIRED_OPP 4
#define PD_FLAG_DEV_LINK_ON 2
struct device { int usage; };
struct dev_pm_domain_list { int unused; };
struct dev_pm_domain_attach_data {
	const char * const *pd_names;
	u32 num_pd_names, pd_flags;
};
struct video_device { void *core; };
struct msm_video_device { struct video_device vdev; int type; };
struct msm_vidc_core { struct msm_video_device vdev[2]; bool ready; };
struct v4l2_fh { struct video_device *vdev; void *ctrl_handler; bool added; };
struct msm_vidc_inst { struct msm_vidc_core *core; int domain, ctrl_handler; struct v4l2_fh fh; };
struct file { void *private_data; struct video_device *vdev; bool dead; };
static int add_count, del_count, close_count, open_failure;
static struct msm_vidc_inst instance;
static bool defer_destructor;
static int action_error, add_actions, detach_count, attach_result, opp_result;
static int resume_error;
static struct device supplier;
static void (*action)(void *);
static void *action_data;
static bool is_decode_session(struct msm_vidc_inst *i) { return i->domain == MSM_VIDC_DECODER; }
static bool is_encode_session(struct msm_vidc_inst *i) { return i->domain == MSM_VIDC_ENCODER; }
static struct video_device *video_devdata(struct file *f) { assert(!f->dead); return f->vdev; }
static void *video_drvdata(struct file *f) { return video_devdata(f)->core; }
static void v4l2_fh_init(struct v4l2_fh *fh, struct video_device *vdev) { assert(!fh->vdev); fh->vdev = vdev; }
static void v4l2_fh_add(struct v4l2_fh *fh, struct file *f) { assert(!f->dead && fh->vdev && !fh->added); fh->added = true; f->private_data = fh; add_count++; }
static void v4l2_fh_del(struct v4l2_fh *fh, struct file *f) { assert(!f->dead && fh->added && f->private_data == fh); fh->added = false; f->private_data = NULL; del_count++; }
static void v4l2_fh_exit(struct v4l2_fh *fh) { assert(!fh->added); fh->vdev = NULL; }
static struct msm_vidc_inst *get_vidc_inst(struct file *f, void *unused) { (void)unused; return f->private_data ? container_of(f->private_data, struct msm_vidc_inst, fh) : NULL; }
int msm_vidc_v4l2_fh_init(struct msm_vidc_inst *inst);
int msm_vidc_v4l2_fh_deinit(struct msm_vidc_inst *inst);
static void *msm_vidc_open(struct msm_vidc_core *core, u32 type)
{
	if (open_failure == 1) return NULL;
	memset(&instance, 0, sizeof(instance));
	instance.core = core; instance.domain = type;
	assert(!msm_vidc_v4l2_fh_init(&instance));
	if (open_failure == 2) {
		assert(!msm_vidc_v4l2_fh_deinit(&instance));
		return NULL;
	}
	return &instance;
}
static int msm_vidc_close(struct msm_vidc_inst *inst)
{
	assert(inst == &instance && !inst->fh.added);
	close_count++;
	if (!defer_destructor) assert(!msm_vidc_v4l2_fh_deinit(inst));
	return 0;
}
static const char *dev_name(struct device *d) { assert(!IS_ERR_OR_NULL(d)); return "mock"; }
static struct device *dev_pm_domain_attach_by_name(struct device *d, const char *name)
{ assert(d && name); return attach_result < 0 ? ERR_PTR(attach_result) : attach_result ? &supplier : NULL; }
static void dev_pm_domain_detach(struct device *d, bool off)
{ assert(d == &supplier && off); detach_count++; }
static int devm_add_action_or_reset(struct device *d, void (*fn)(void *), void *data)
{
	assert(d && fn && !IS_ERR_OR_NULL(data)); add_actions++;
	if (action_error) { fn(data); return action_error; }
	assert(!action); action = fn; action_data = data; return 0;
}
static int devm_pm_domain_attach_list(struct device *d, const struct dev_pm_domain_attach_data *data, struct dev_pm_domain_list **list)
{
	static struct dev_pm_domain_list domains;
	assert(d && list && data->pd_names && data->num_pd_names == 2);
	assert(data->pd_flags == (PD_FLAG_REQUIRED_OPP | PD_FLAG_DEV_LINK_ON));
	assert(!strcmp(data->pd_names[0], "mx") && !strcmp(data->pd_names[1], "mmcx"));
	if (opp_result > 0) *list = &domains;
	return opp_result;
}
static int pm_runtime_resume_and_get(struct device *d)
{ if (resume_error) return resume_error; d->usage++; return 0; }
static int pm_runtime_get_sync(struct device *d)
{ d->usage++; return resume_error; }
static int pm_runtime_put_sync(struct device *d)
{ assert(d->usage > 0); d->usage--; return 0; }

/* SOURCE */

static void reset_power(void)
{
	assert(!action); action_data = NULL; action_error = 0;
	add_actions = detach_count = 0; resume_error = 0;
}
static void release_action(void)
{
	assert(action); void (*fn)(void *) = action; action = NULL; fn(action_data);
}
int main(void)
{
	unsigned cases = 0;
	struct msm_vidc_core core = {.ready = true};
	for (int i = 0; i < 2; i++) {
		core.vdev[i].type = i + 1; core.vdev[i].vdev.core = &core;
		for (int fail = 0; fail < 3; fail++) {
			struct file file = {.vdev = &core.vdev[i].vdev};
			open_failure = fail; add_count = del_count = close_count = 0;
			int rc = msm_v4l2_open(&file);
			if (fail) {
				assert(rc == -ENOMEM && !file.private_data && !add_count && !del_count);
			} else {
				assert(!rc && add_count == 1 && file.private_data == &instance.fh);
				assert(instance.fh.ctrl_handler == &instance.ctrl_handler);
				defer_destructor = true;
				assert(!msm_v4l2_close(&file));
				assert(!file.private_data && del_count == 1 && close_count == 1);
				file.dead = true;
				assert(!msm_vidc_v4l2_fh_deinit(&instance));
				assert(!instance.fh.vdev && del_count == 1);
				assert(!msm_vidc_v4l2_fh_deinit(&instance));
			}
			cases++;
		}
	}
	struct device dev = {0};
	const char *names[] = {"mx", "mmcx", NULL};
	int returns[] = {-EPROBE_DEFER, -ENOMEM, 0, 1, 2};
	for (unsigned i = 0; i < sizeof(returns) / sizeof(returns[0]); i++) {
		opp_result = returns[i];
		int expected = opp_result < 0 ? opp_result : opp_result == 2 ? 0 : -ENODEV;
		assert(msm_vidc_attach_opp_domains(&dev, names, 2) == expected); cases++;
	}
	for (unsigned i = 0; i < sizeof(returns) / sizeof(returns[0]); i++) {
		reset_power(); attach_result = returns[i];
		struct device *pd = devm_pd_get(&dev, "mx");
		if (attach_result <= 0) {
			assert(IS_ERR(pd) && PTR_ERR(pd) == (attach_result ? attach_result : -ENODEV));
			assert(!add_actions && !detach_count);
		} else {
			assert(pd == &supplier && add_actions == 1); release_action(); assert(detach_count == 1);
		}
		cases++;
	}
	reset_power(); attach_result = 1; action_error = -ENOMEM;
	assert(PTR_ERR(devm_pd_get(&dev, "mx")) == -ENOMEM && detach_count == 1); cases++;
	for (int phase = 0; phase < 3; phase++) {
		reset_power(); resume_error = phase == 1 ? -EIO : 0; action_error = phase == 2 ? -ENOMEM : 0;
		int rc = devm_pm_runtime_get_sync(&dev);
		assert(rc == (resume_error ? resume_error : action_error));
		if (!phase) { assert(dev.usage == 1); release_action(); }
		assert(dev.usage == 0); cases++;
	}
	printf("%u file-handle and power-ownership cases passed\n", cases);
	return 0;
}
