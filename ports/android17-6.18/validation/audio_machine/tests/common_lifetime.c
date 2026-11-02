#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define MAX_USR_INPUT 10
#define DEVICE_ENABLE 1
#define TDM_MAX_SLOTS 8
#define MI2S_TDM_AUXPCM_MAX 7
#define PRI_MI2S_TDM_AUXPCM 0
#define SEC_MI2S_TDM_AUXPCM 1
#define TER_MI2S_TDM_AUXPCM 2
#define QUAT_MI2S_TDM_AUXPCM 3
#define QUIN_MI2S_TDM_AUXPCM 4
#define SEN_MI2S_TDM_AUXPCM 5
#define SEP_MI2S_TDM_AUXPCM 6
#define CLOCK_ATTRIBUTE_COUPLE_NO 1
#define CLOCK_ROOT_DEFAULT 0
#define TDM_SLOT_WIDTH_BITS 32
#define MI2S_NUM_CHANNELS 2
#define SNDRV_PCM_FORMAT_S16_LE 1
#define SNDRV_PCM_FORMAT_S24_LE 2
#define SNDRV_PCM_FORMAT_S24_3LE 3
#define SNDRV_PCM_FORMAT_S32_LE 4
#define DEV_PM_QOS_RESUME_LATENCY 1
#define PM_QOS_CPU_LATENCY_DEFAULT_VALUE 2000
#define READ_ONCE(v) (v)
#define WRITE_ONCE(v, x) ((v) = (x))
#define DEFINE_MUTEX(name) struct mutex name = { PTHREAD_MUTEX_INITIALIZER }
#define container_of(p, t, field) ((t *)((char *)(p) - offsetof(t, field)))
#define to_asoc_mach_common_pdata(kobj) container_of(kobj, struct msm_common_pdata, aud_dev_kobj)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define ERR_PTR(err) ((void *)(intptr_t)(err))
#define PTR_ERR(ptr) ((int)(intptr_t)(ptr))
#define IS_ERR(ptr) ((uintptr_t)(ptr) >= (uintptr_t)-4095)
typedef uint32_t u32;
typedef struct { int value; } atomic_t;
static int atomic_read(atomic_t *v) { return v->value; }
static void atomic_set(atomic_t *v, int value) { v->value = value; }
static void atomic_inc(atomic_t *v) { v->value++; }
static void atomic_dec(atomic_t *v) { v->value--; }
static bool atomic_dec_and_test(atomic_t *v) { return --v->value == 0; }
struct mutex { pthread_mutex_t value; };
static void mutex_init(struct mutex *lock) { assert(!pthread_mutex_init(&lock->value, NULL)); }
static void mutex_destroy(struct mutex *lock) { assert(!pthread_mutex_destroy(&lock->value)); }
static void mutex_lock(struct mutex *lock) { assert(!pthread_mutex_lock(&lock->value)); }
static void mutex_unlock(struct mutex *lock) { assert(!pthread_mutex_unlock(&lock->value)); }
struct kobject;
struct attribute { const char *name; unsigned int mode; };
struct sysfs_ops {
	ssize_t (*show)(struct kobject *, struct attribute *, char *);
	ssize_t (*store)(struct kobject *, struct attribute *, const char *, size_t);
};
struct kobj_type { const struct sysfs_ops *sysfs_ops; void (*release)(struct kobject *); };
struct kobject { int refs; bool added, attr; const struct kobj_type *type; };
struct device_node { int refs; };
struct clk { int present; };
struct device { struct device_node *of_node; int cpu; };
struct platform_device { struct device dev; };
struct msm_common_pdata;
struct snd_soc_card { unsigned int num_links; struct msm_common_pdata *pdata; };
struct snd_soc_dai_link { const char *stream_name; };
struct snd_soc_pcm_runtime { struct snd_soc_card *card; struct snd_soc_dai_link *dai_link; };
struct snd_pcm_runtime { unsigned int rate; };
struct snd_pcm_substream { struct snd_soc_pcm_runtime *private_data; struct snd_pcm_runtime *runtime; const char *name; int stream; };
struct snd_pcm_hw_params { unsigned int rate, format; };
struct clk_cfg { int clk_id; unsigned int clk_freq_in_hz, clk_attri, clk_root; };
typedef enum { SND_CARD_STATUS_OFFLINE, SND_CARD_STATUS_ONLINE } snd_card_status_t;
/* PRODUCTION_COMMON_TYPE */
struct snd_card_pdata { struct kobject snd_card_kobj; int card_status; };
struct dev_pm_qos_request { bool active; int cpu, value; };
typedef struct { uint64_t bits; } cpumask_t;
struct cpumask { uint64_t bits; };
static const struct cpumask topology_mask = { .bits = (1u << 2) | (1u << 6) };
static const cpumask_t possible = { .bits = (1u << 2) | (1u << 6) };
static const cpumask_t *cpu_possible_mask = &possible;
static unsigned int nr_cpu_ids = 8;
#define for_each_cpu(cpu, mask) for ((cpu) = 0; (cpu) < nr_cpu_ids; (cpu)++) if ((mask)->bits & (1ull << (cpu)))
static void cpumask_clear(cpumask_t *mask) { mask->bits = 0; }
static unsigned int cpumask_first(const cpumask_t *mask) { return mask->bits ? (unsigned int)__builtin_ctzll(mask->bits) : nr_cpu_ids; }
static void cpumask_copy(cpumask_t *dst, const struct cpumask *src) { dst->bits = src->bits; }
static const struct cpumask *topology_core_cpumask(unsigned int cpu) { assert(cpu == 2); return &topology_mask; }

static void *allocations[2048];
static size_t sizes[2048];
static unsigned int allocation_count, live_count;
static int fail_allocation, fail_kobject, fail_attr, clk_error, qos_error_cpu = -1;
static unsigned int active_attrs, active_qos, qos_removes, qos_updates, releases, nodes_parsed;
static int hw_vote_error, hw_votes, hw_enables, hw_disables;
static int prm_error, interface_clocks;
static unsigned int programmed_frequency;
static struct device_node gpio_nodes[7], root_node;
static struct clk audio_clock = {1};
static struct device cpu_devices[8];
static struct kobject kernel_root;
static struct kobject *kernel_kobj = &kernel_root;
static struct snd_card_pdata *snd_card_pdata;
static DEFINE_MUTEX(snd_card_lock);
static DEFINE_MUTEX(vote_against_sleep_lock);
static unsigned int vote_against_sleep_cnt;
static bool kregister_pm_qos_latency_controls;
static struct dev_pm_qos_request latency_pm_qos_req;
static unsigned int qos_client_active_cnt;
static int qos_vote_status;
static cpumask_t audio_qos_mask;
static struct dev_pm_qos_request *msm_audio_req;
static struct attribute device_state_attr = {"state", 0660};
static struct attribute card_state_attr = {"card_state", 0660};

static void *kcalloc(size_t count, size_t size, int flags)
{
	(void)flags;
	if (fail_allocation && !--fail_allocation) return NULL;
	assert(count && size <= SIZE_MAX / count);
	void *ptr = calloc(count, size); assert(ptr);
	assert(allocation_count < 2048);
	allocations[allocation_count] = ptr; sizes[allocation_count++] = count * size;
	live_count++; return ptr;
}
static void *kzalloc(size_t size, int flags) { return kcalloc(1, size, flags); }
static void kfree(void *ptr)
{
	if (!ptr) return;
	unsigned int i;
	for (i = 0; i < allocation_count; i++) if (allocations[i] == ptr) {
		allocations[i] = NULL; live_count--; break;
	}
	assert(i < allocation_count); free(ptr);
}
static void kobject_init(struct kobject *obj, const struct kobj_type *type) { obj->refs = 1; obj->type = type; }
static int kobject_add(struct kobject *obj, struct kobject *parent, const char *name)
{
	assert(parent == kernel_kobj && name && obj->refs == 1);
	if (fail_kobject) return -ENOMEM;
	obj->added = true; return 0;
}
static int kobject_init_and_add(struct kobject *obj, const struct kobj_type *type, struct kobject *parent, const char *name)
{ kobject_init(obj, type); return kobject_add(obj, parent, name); }
static void kobject_del(struct kobject *obj) { assert(obj->added && !obj->attr); obj->added = false; }
static void kobject_put(struct kobject *obj)
{
	assert(obj->refs > 0);
	if (!--obj->refs) { assert(!obj->attr); obj->added = false; releases++; obj->type->release(obj); }
}
static int sysfs_create_file(struct kobject *obj, struct attribute *attr)
{
	assert(obj->added && !obj->attr && attr->name);
	if (fail_attr) return -ENOMEM;
	obj->attr = true; active_attrs++; return 0;
}
static void sysfs_remove_file(struct kobject *obj, struct attribute *attr)
{ assert(obj->attr && attr->name); obj->attr = false; active_attrs--; }
static void sysfs_notify(struct kobject *obj, const char *dir, const char *name)
{ assert(obj->attr && !dir && !strcmp(name, "card_state")); }
static int sysfs_emit(char *buf, const char *format, ...)
{ va_list args; va_start(args, format); int ret = vsnprintf(buf, 4096, format, args); va_end(args); return ret; }
static int kstrtoint(const char *buf, unsigned int base, int *out)
{
	char *end; errno = 0; long value = strtol(buf, &end, base);
	if (errno || end == buf || (*end && strcmp(end, "\n")) || value < INT32_MIN || value > INT32_MAX) return -EINVAL;
	*out = value; return 0;
}
static int of_property_read_u32(struct device_node *node, const char *name, u32 *value)
{ assert(node && !strcmp(name, "qcom,tdm-max-slots")); (void)value; return -ENOENT; }
static int of_property_read_u32_array(struct device_node *node, const char *name, u32 *values, size_t count)
{ assert(node && name && values && count == 7); return -ENOENT; }
static struct device_node *of_parse_phandle(struct device_node *node, const char *name, unsigned int index)
{
	assert(node && name && index == 0 && nodes_parsed < 7);
	struct device_node *result = &gpio_nodes[nodes_parsed++]; result->refs++; return result;
}
static void of_node_put(struct device_node *node) { if (node) assert(node->refs-- == 1); }
static struct clk *devm_clk_get(struct device *dev, const char *name)
{ assert(dev && !strcmp(name, "lpass_audio_hw_vote")); return clk_error ? ERR_PTR(clk_error) : &audio_clock; }
static struct device *get_cpu_device(unsigned int cpu) { assert(cpu < nr_cpu_ids); cpu_devices[cpu].cpu = cpu; return &cpu_devices[cpu]; }
static bool dev_pm_qos_request_active(struct dev_pm_qos_request *req) { return req->active; }
static int dev_pm_qos_add_request(struct device *dev, struct dev_pm_qos_request *req, int type, int value)
{
	assert(type == DEV_PM_QOS_RESUME_LATENCY && !req->active);
	assert(dev->cpu == 2 || dev->cpu == 6);
	if (dev->cpu == qos_error_cpu) return -EIO;
	req->active = true; req->cpu = dev->cpu; req->value = value; active_qos++; return 0;
}
static int dev_pm_qos_remove_request(struct dev_pm_qos_request *req)
{ assert(req->active); req->active = false; active_qos--; qos_removes++; return 0; }
static int dev_pm_qos_update_request(struct dev_pm_qos_request *req, int value)
{ assert(req->active); req->value = value; qos_updates++; return 1; }
static int digital_cdc_rsc_mgr_hw_vote_enable(struct clk *clk, void *unused)
{ assert(clk == &audio_clock && !unused); if (hw_vote_error) return hw_vote_error; assert(!hw_votes); hw_votes++; hw_enables++; return 0; }
static void digital_cdc_rsc_mgr_hw_vote_disable(struct clk *clk, void *unused)
{ assert(clk == &audio_clock && !unused && hw_votes == 1); hw_votes--; hw_disables++; }
static void msm_common_set_pdata(struct snd_soc_card *card, struct msm_common_pdata *pdata) { card->pdata = pdata; }
static struct msm_common_pdata *msm_common_get_pdata(struct snd_soc_card *card) { return card->pdata; }
static unsigned int params_rate(struct snd_pcm_hw_params *params) { return params->rate; }
static unsigned int params_format(struct snd_pcm_hw_params *params) { return params->format; }
static int get_mi2s_tdm_auxpcm_intf_index(const char *name) { (void)name; return 0; }
static int get_tdm_clk_id(int index) { assert(index == 0); return 10; }
static int get_mi2s_clk_id(int index) { assert(index == 0); return 11; }
static bool is_fractional_sample_rate(unsigned int rate) { return rate == 44100; }
static const char *strnstr(const char *haystack, const char *needle, size_t len) { assert(len == strlen(haystack)); return strstr(haystack, needle); }
static int audio_prm_set_lpass_clk_cfg(struct clk_cfg *cfg, int enable)
{
	assert(cfg->clk_id == 10 || cfg->clk_id == 11);
	if (prm_error) return prm_error;
	if (enable) { interface_clocks++; programmed_frequency = cfg->clk_freq_in_hz; }
	else { assert(interface_clocks > 0); interface_clocks--; }
	return 0;
}
static int msm_cdc_pinctrl_select_sleep_state(struct device_node *node) { assert(node); return 0; }
void msm_common_snd_deinit(struct msm_common_pdata *pdata);

/* PRODUCTION_SOURCE */

static void clean(void)
{
	assert(!live_count && !active_attrs && !active_qos && !msm_audio_req && !snd_card_pdata && !hw_votes && !interface_clocks);
	for (unsigned int i = 0; i < 7; i++) assert(!gpio_nodes[i].refs);
	nodes_parsed = 0; fail_allocation = fail_kobject = fail_attr = clk_error = 0;
	qos_error_cpu = -1;
}
int main(void)
{
	unsigned int cases = 0;
	for (unsigned int fault = 0; fault < 3; fault++) {
		if (fault == 0) fail_allocation = 1;
		if (fault == 1) fail_kobject = 1;
		if (fault == 2) fail_attr = 1;
		assert(snd_card_sysfs_init() == -ENOMEM);
		assert(snd_card_notify_user(SND_CARD_STATUS_ONLINE) == -ENODEV);
		snd_card_sysfs_deinit(); clean(); cases++;
	}
	assert(!snd_card_sysfs_init());
	char buf[4096];
	assert(snd_card_sysfs_store(&snd_card_pdata->snd_card_kobj, &card_state_attr, "bad", 3) == -EINVAL);
	assert(snd_card_sysfs_store(&snd_card_pdata->snd_card_kobj, &card_state_attr, "2", 1) == -EINVAL);
	assert(snd_card_sysfs_store(&snd_card_pdata->snd_card_kobj, &card_state_attr, "1\n", 2) == 2);
	assert(snd_card_sysfs_show(&snd_card_pdata->snd_card_kobj, &card_state_attr, buf) == 1 && !strcmp(buf, "1"));
	vote_against_sleep_cnt = 7;
	assert(!snd_card_notify_user(SND_CARD_STATUS_OFFLINE) && !vote_against_sleep_cnt);
	struct snd_card_pdata *held = snd_card_pdata; held->snd_card_kobj.refs++;
	snd_card_sysfs_deinit(); assert(live_count == 1 && !held->snd_card_kobj.added);
	assert(snd_card_set_card_status(SND_CARD_STATUS_ONLINE) == -ENODEV);
	kobject_put(&held->snd_card_kobj); clean(); cases += 7;
	struct platform_device pdev = { .dev.of_node = &root_node };
	struct snd_soc_card card = { .num_links = 6 };
	for (unsigned int fault = 0; fault < 5; fault++) {
		if (fault == 0) fail_allocation = 1;
		if (fault == 1) fail_allocation = 2;
		if (fault == 2) fail_kobject = 1;
		if (fault == 3) fail_attr = 1;
		if (fault == 4) clk_error = -EPROBE_DEFER;
		assert(msm_common_snd_init(&pdev, &card) == (fault == 4 ? -EPROBE_DEFER : -ENOMEM));
		assert(!card.pdata); clean(); cases++;
	}
	assert(!msm_common_snd_init(&pdev, &card));
	struct msm_common_pdata *pdata = card.pdata;
	assert(pdata->tdm_max_slots == 8 && active_qos == 2);
	assert(msm_audio_req[2].active && msm_audio_req[6].active);
	const char *invalid[] = {"", "1", "-1 0", "6 0", "1 -1", "1 2", "1 1junk", "1 1 extra"};
	for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		assert(aud_dev_sysfs_store(&pdata->aud_dev_kobj, &device_state_attr, invalid[i], strlen(invalid[i])) == -EINVAL); cases++;
	}
	assert(aud_dev_sysfs_store(&pdata->aud_dev_kobj, &device_state_attr, "5 1\n", 4) == 4 && pdata->aud_dev_state[5] == 1);
	assert(aud_dev_sysfs_store(&pdata->aud_dev_kobj, &device_state_attr, "5 0", 3) == 3 && pdata->aud_dev_state[5] == 0);
	msm_audio_update_qos_request(300);
	assert(msm_audio_req[2].value == 300 && msm_audio_req[6].value == 300);
	hw_vote_error = -EIO;
	assert(mi2s_tdm_hw_vote_req(pdata, 1) == -EIO && !atomic_read(&pdata->lpass_audio_hw_vote_ref_cnt));
	hw_vote_error = 0;
	assert(!mi2s_tdm_hw_vote_req(pdata, 1) && !mi2s_tdm_hw_vote_req(pdata, 1));
	assert(hw_enables == 1 && hw_votes == 1 && atomic_read(&pdata->lpass_audio_hw_vote_ref_cnt) == 2);
	assert(!mi2s_tdm_hw_vote_req(pdata, 0) && hw_votes == 1);
	assert(!mi2s_tdm_hw_vote_req(pdata, 0) && !hw_votes);
	assert(!mi2s_tdm_hw_vote_req(pdata, 0) && !atomic_read(&pdata->lpass_audio_hw_vote_ref_cnt));
	struct snd_soc_dai_link link = {.stream_name = "PRI-TDM"};
	struct snd_soc_pcm_runtime rtd = {&card, &link};
	struct snd_pcm_runtime runtime = {.rate = 44100};
	struct snd_pcm_substream substream = {&rtd, &runtime, "playback", 0};
	struct snd_pcm_hw_params params = {44100, SNDRV_PCM_FORMAT_S24_LE};
	pdata->is_audio_hw_vote_required[0] = 1;
	for (unsigned int mode = 0; mode < 2; mode++) {
		link.stream_name = mode ? "PRI-MI2S" : "PRI-TDM";
		prm_error = -EIO;
		assert(msm_common_snd_hw_params(&substream, &params) == -EIO);
		assert(!hw_votes && !interface_clocks && !atomic_read(&pdata->lpass_intf_clk_ref_cnt[0]));
		prm_error = 0;
		assert(!msm_common_snd_hw_params(&substream, &params));
		assert(programmed_frequency == 44100u * 32 * (mode ? 2 : 8));
		assert(hw_votes == 1 && interface_clocks == 1);
		msm_common_snd_shutdown(&substream);
		assert(!hw_votes && !interface_clocks);
		cases += 2;
	}
	pdata->aud_dev_kobj.refs++;
	msm_common_snd_deinit(pdata); card.pdata = NULL;
	assert(live_count == 2 && !pdata->aud_dev_kobj.added && !active_qos);
	kobject_put(&pdata->aud_dev_kobj); clean(); cases += 9;
	qos_error_cpu = 6;
	assert(!msm_common_snd_init(&pdev, &card) && active_qos == 1);
	msm_audio_update_qos_request(300);
	msm_common_snd_deinit(card.pdata); card.pdata = NULL; clean(); cases++;
	fail_allocation = 3;
	assert(!msm_common_snd_init(&pdev, &card) && !msm_audio_req && !active_qos);
	msm_audio_update_qos_request(300);
	msm_common_snd_deinit(card.pdata); card.pdata = NULL; clean(); cases++;
	assert(!msm_common_snd_init(&pdev, &card));
	assert(!mi2s_tdm_hw_vote_req(card.pdata, 1));
	msm_common_snd_deinit(card.pdata); card.pdata = NULL; clean(); cases++;
	printf("%u common sysfs, sparse-CPU QoS, ownership, allocation and hardware-vote cases passed\n", cases);
	return 0;
}
