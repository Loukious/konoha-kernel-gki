#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define SND_EVENT_UP 1
#define SND_CARD_STATUS_OFFLINE 0
#define WCD9378_DEV_INDEX 2
#define WCD939X_DEV_INDEX 1
#define ERR_PTR(err) ((void *)(intptr_t)(err))
#define PTR_ERR(ptr) ((int)(intptr_t)(ptr))
#define IS_ERR(ptr) ((uintptr_t)(ptr) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))
#define EXPORT_SYMBOL(name)
#define dev_err(...) ((void)0)
typedef uint32_t u32;
struct snd_soc_pcm_runtime;
static void msm_rx_tx_codec_exit(struct snd_soc_pcm_runtime *rtd);
struct device_node { int refs; };
struct action { void (*fn)(void *); void *arg; };
struct device { struct device_node *of_node; void *data; struct action actions[16]; unsigned int action_count; };
struct snd_soc_component { int unused; };
struct snd_soc_dai_link { unsigned int id; };
struct msm_common_pdata { bool alive; };
struct wcd_mbhc_config { void *calibration; };
struct prm_earpa_hw_intf_config { const char *backend_used; };
/* PRODUCTION_MACHINE_TYPE */
struct snd_soc_card { struct device *dev; struct msm_asoc_mach_data *pdata; };
struct snd_soc_pcm_runtime { struct snd_soc_card *card; };
struct snd_event_clients { void *cl_arr; unsigned int count; };
void snd_event_clients_free(struct snd_event_clients *clients);
static const int sun_ssr_ops;
static struct device_node nodes[3];
static unsigned int node_count, actions_seen, fail_action;
static unsigned int live_allocations, registered_cards, card_unregisters, master_unregisters;
static unsigned int common_releases, headset_stops, reset_controls;
static bool card_users, component_live, headset_live, pins_active;
static struct snd_event_clients *provider_clients;
static int master_error, notify_error;
static bool client_alloc_error;

static void *allocate(size_t size) { void *p = calloc(1, size); assert(p); live_allocations++; return p; }
static void kfree(void *ptr) { if (ptr) { assert(live_allocations); live_allocations--; free(ptr); } }
static void *dev_get_drvdata(struct device *dev) { return dev->data; }
static struct msm_asoc_mach_data *snd_soc_card_get_drvdata(struct snd_soc_card *card) { return card->pdata; }
static struct device_node *of_parse_phandle(struct device_node *node, const char *name, unsigned int index)
{
	assert(node && name);
	if (index >= node_count) return NULL;
	nodes[index].refs++; return &nodes[index];
}
static void of_node_put(struct device_node *node) { if (node) assert(node->refs-- > 0); }
static int devm_add_action_or_reset(struct device *dev, void (*fn)(void *), void *arg)
{
	if (++actions_seen == fail_action) { fn(arg); return -ENOMEM; }
	assert(dev->action_count < 16);
	dev->actions[dev->action_count++] = (struct action){fn, arg}; return 0;
}
static void devres_release(struct device *dev)
{
	while (dev->action_count) {
		struct action *a = &dev->actions[--dev->action_count]; a->fn(a->arg);
	}
}
static int msm_audio_ssr_compare(struct device *dev, void *data) { return dev->of_node == data; }
static void snd_event_mstr_add_client(struct snd_event_clients **clients, int (*compare)(struct device *, void *), void *data)
{
	assert(compare == msm_audio_ssr_compare && data);
	if (client_alloc_error) { snd_event_clients_free(*clients); *clients = ERR_PTR(-ENOMEM); return; }
	if (!*clients) { *clients = allocate(sizeof(**clients)); (*clients)->cl_arr = allocate(16); }
	(*clients)->count++;
}
static int snd_event_master_register(struct device *dev, const int *ops, struct snd_event_clients *clients, void *data)
{
	assert(dev && ops == &sun_ssr_ops && !data && !provider_clients);
	if (master_error) return master_error;
	if (!clients || !clients->count) return -EINVAL;
	provider_clients = clients; return 0;
}
static int snd_event_notify(struct device *dev, unsigned int state) { assert(dev && state == SND_EVENT_UP && provider_clients); return notify_error; }
static int snd_event_master_deregister(struct device *dev)
{
	assert(dev && provider_clients && registered_cards);
	snd_event_clients_free(provider_clients); provider_clients = NULL; master_unregisters++; return 0;
}
static void wcd9378_mbhc_hs_detect_exit(struct snd_soc_component *component)
{ assert(component && component_live && headset_live && !card_users); headset_live = false; headset_stops++; }
static void wcd939x_mbhc_hs_detect_exit(struct snd_soc_component *component)
{ wcd9378_mbhc_hs_detect_exit(component); }
static void msm_common_snd_card_reset(void) { reset_controls++; }
static void snd_soc_unregister_card(struct snd_soc_card *card)
{
	assert(!provider_clients && registered_cards && card_users);
	card_users = false;
	struct snd_soc_pcm_runtime rtd = {card}; msm_rx_tx_codec_exit(&rtd);
	component_live = false; registered_cards--; card_unregisters++;
}
static void msm_common_snd_deinit(struct msm_common_pdata *pdata)
{
	if (!pdata) return;
	assert(pdata->alive && !card_users && !provider_clients && !headset_live);
	pdata->alive = false; common_releases++; kfree(pdata);
}
static int msm_cdc_pinctrl_select_sleep_state(struct device_node *node)
{ assert(node && pins_active && !card_users); pins_active = false; return 0; }
static int snd_card_set_card_status(int status) { assert(status == SND_CARD_STATUS_OFFLINE); return 0; }

/* PRODUCTION_SOURCE */

static void clean(struct device *dev)
{
	devres_release(dev);
	assert(!live_allocations && !provider_clients && !registered_cards && !card_users && !component_live && !headset_live && !pins_active);
	for (unsigned int i = 0; i < 3; i++) assert(!nodes[i].refs);
	master_error = notify_error = 0; client_alloc_error = false; actions_seen = fail_action = 0;
}
int main(void)
{
	unsigned int cases = 0;
	struct device_node root = {0};
	struct device dev = {.of_node = &root};
	struct msm_asoc_mach_data pdata = {0};
	struct snd_soc_card card = {&dev, &pdata}; dev.data = &card;
	struct snd_soc_pcm_runtime rtd = {&card};
	struct snd_soc_component component = {0};
	struct device_node *node = (void *)1;
	node_count = 0;
	assert(!msm_machine_get_node(&dev, "optional", &node) && !node); cases++;
	node_count = 1; fail_action = 1;
	assert(msm_machine_get_node(&dev, "required", &node) == -ENOMEM && !node);
	clean(&dev); cases++;
	assert(!msm_machine_get_node(&dev, "required", &node) && node == &nodes[0] && node->refs == 1);
	clean(&dev); cases++;
	for (unsigned int failure = 0; failure < 5; failure++) {
		node_count = 2;
		if (failure < 2) fail_action = failure + 1;
		if (failure == 2) client_alloc_error = true;
		if (failure == 3) master_error = -ENOMEM;
		if (failure == 4) master_error = -EALREADY;
		assert(msm_audio_ssr_register(&dev) < 0 && !pdata.ssr_registered);
		clean(&dev); cases++;
	}
	node_count = 0;
	assert(msm_audio_ssr_register(&dev) == -EINVAL); clean(&dev); cases++;
	for (unsigned int variant = 0; variant < 2; variant++) {
		pdata.common_pdata = allocate(sizeof(*pdata.common_pdata)); pdata.common_pdata->alive = true;
		pdata.card_registered = true; card_users = component_live = headset_live = true; registered_cards = 1;
		pdata.mbhc_component = &component; pdata.wcd_used = variant ? WCD939X_DEV_INDEX : WCD9378_DEV_INDEX;
		pdata.mbhc_cfg.calibration = allocate(32);
		pdata.codec_root = (void *)1;
		pdata.dmic_enable_gpio = &nodes[2]; pdata.dmic_enabled = pins_active = true;
		node_count = 2;
		assert(!msm_audio_ssr_register(&dev) && pdata.ssr_registered);
		msm_asoc_machine_cleanup(&card);
		assert(!pdata.common_pdata && !pdata.mbhc_component && !pdata.mbhc_cfg.calibration && !pdata.codec_root);
		assert(!pdata.ssr_registered && !pdata.card_registered && !pdata.dmic_enabled);
		msm_asoc_machine_cleanup(&card); msm_rx_tx_codec_exit(&rtd);
		clean(&dev); cases += 2;
	}
	pdata.common_pdata = allocate(sizeof(*pdata.common_pdata)); pdata.common_pdata->alive = true;
	msm_asoc_machine_cleanup(&card); clean(&dev); cases++;
	pdata.common_pdata = allocate(sizeof(*pdata.common_pdata)); pdata.common_pdata->alive = true;
	pdata.card_registered = true; registered_cards = 1; card_users = component_live = true;
	node_count = 2; notify_error = -EIO;
	assert(msm_audio_ssr_register(&dev) == -EIO && pdata.ssr_registered);
	msm_asoc_machine_cleanup(&card); clean(&dev); cases++;
	struct snd_soc_dai_link dest[5] = {{0}, {0}, {0}, {0}, {12345}};
	struct snd_soc_dai_link src[4] = {{1}, {2}, {3}, {4}};
	unsigned int count = 0;
	assert(!msm_append_dai_links(dest, 4, &count, src, 4) && count == 4);
	assert(dest[0].id == 1 && dest[3].id == 4 && dest[4].id == 12345);
	assert(msm_append_dai_links(dest, 4, &count, src, 1) == -E2BIG && count == 4 && dest[4].id == 12345);
	count = 5; assert(msm_append_dai_links(dest, 4, &count, src, 0) == -E2BIG);
	count = 0; assert(msm_append_dai_links(dest, 4, &count, src, SIZE_MAX) == -E2BIG && !count);
	cases += 4;
	snd_event_clients_free(NULL); snd_event_clients_free(ERR_PTR(-ENOMEM)); clean(&dev);
	assert(master_unregisters == 3 && card_unregisters == 3 && headset_stops == 2 && common_releases == 4);
	printf("%u card/MBHC teardown, deferred-node, SND event ownership and DAI bound cases passed\n", cases);
	return 0;
}
