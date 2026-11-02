#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_ENABLED(option) (option)
#define CONFIG_QCOM_WCD_USBSS_I2C 1
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(error) ((void *)(intptr_t)(error))
#define SWR_NUM_PORTS 4
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
typedef uint8_t u8;
struct device_node { int id; };
struct device { void *data, *pdata; int refs; };
struct swr_device { struct device dev; u8 dev_num; bool paging_support; };
struct regmap { bool alive; };
struct irq_domain { bool alive; };
struct regmap_config { bool (*readable_reg)(struct device *, unsigned int); };
struct regmap_irq_chip { int num_regs; void *irq_drv_data; };
struct wcd_irq_info { struct regmap_irq_chip *wcd_regmap_irq_chip; const char *codec_name; struct regmap *regmap; struct device *dev; };
struct codec_pdata { struct device_node *rx_slave, *tx_slave; };
struct codec_priv {
	struct swr_device *rx_swr_dev, *tx_swr_dev;
	struct regmap *regmap;
	struct irq_domain *virq;
	struct wcd_irq_info irq_info;
	struct regmap_irq_chip irq_chip_config;
	struct regmap_config regmap_config;
	int swr_tx_port_params[4];
	unsigned int wd_irqs_requested;
	bool irq_published, component_registered, slave_notifier_registered;
	bool micbias_regulators_registered, dev_up;
	int version;
};
struct context {
	struct codec_priv priv;
	struct codec_pdata pdata;
	struct device dev;
	struct device_node rx_node, tx_node;
	struct swr_device rx, tx;
	struct regmap map;
	struct irq_domain domain;
	int checkpoints, fail_at, wd_count, reads, id1, status1, withdraw_error;
	int wd_ids[3];
	bool bound, published, registered, notify, regulators;
};
static struct context *ctx;
struct regmap_irq_chip codec_regmap_irq_chip = { .num_regs = 3 };
struct regmap_config codec_regmap_config = {};
const int soc_codec_dev_codec = 1;
const int codec_dai[] = { 1 };

int checkpoint(void) { return ++ctx->checkpoints == ctx->fail_at ? -EIO : 0; }
void *dev_get_drvdata(struct device *dev) { return dev->data; }
void *dev_get_platdata(struct device *dev) { return dev->pdata; }
void usleep_range(int low, int high) {}
int component_bind_all(struct device *dev, void *priv)
{
	if (checkpoint())
		return -EIO;
	assert(!ctx->bound);
	ctx->bound = true;
	return 0;
}
void component_unbind_all(struct device *dev, void *priv)
{
	assert(ctx->bound && !ctx->map.alive && !ctx->domain.alive);
	assert(!ctx->registered && !ctx->published && !ctx->wd_count);
	ctx->bound = false;
}
struct swr_device *get_matching_swr_slave_device(struct device_node *node)
{
	if (checkpoint())
		return NULL;
	struct swr_device *swr = node == &ctx->rx_node ? &ctx->rx : &ctx->tx;
	assert(node == &ctx->rx_node || node == &ctx->tx_node);
	swr->dev.refs++;
	return swr;
}
void put_device(struct device *dev) { assert(dev->refs > 1); dev->refs--; }
int swr_init_port_params(struct swr_device *dev, unsigned int count, void *params)
{
	assert(count == SWR_NUM_PORTS && dev == &ctx->tx);
	return checkpoint();
}
struct regmap *regmap_init_swr(struct swr_device *dev, const struct regmap_config *config)
{
	assert(dev == &ctx->tx && ctx->rx.dev.refs == 2 && ctx->tx.dev.refs == 2);
#if CODEC_KIND == 9378
	assert(dev->paging_support && ctx->rx.paging_support);
#endif
	if (checkpoint())
		return ERR_PTR(-EIO);
	assert(!ctx->map.alive);
	ctx->map.alive = true;
	return &ctx->map;
}
void regmap_exit(struct regmap *map)
{
	assert(map == &ctx->map && map->alive && !ctx->domain.alive && !ctx->registered);
	map->alive = false;
}
int regmap_write(struct regmap *map, unsigned int reg, unsigned int value) { assert(map->alive); return checkpoint(); }
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *value) { assert(map->alive); *value = 0; return checkpoint(); }
int swr_read(struct swr_device *swr, u8 num, unsigned int reg, void *value, unsigned int count)
{
	if (checkpoint())
		return -EIO;
	*(u8 *)value = ctx->reads++ == 0 ? ctx->id1 : ctx->status1;
	return 0;
}
const char *version_to_str(int version) { return "fixture"; }
bool codec_readable_register(struct device *dev, unsigned int reg) { return true; }
bool codec_readable_register_v1(struct device *dev, unsigned int reg) { return true; }
void wcd_usbss_update_default_trim(void) {}
void codec_update_regmap_cache(struct codec_priv *priv) {}
int wcd_irq_init(struct wcd_irq_info *info, struct irq_domain **domain)
{
	assert(info->wcd_regmap_irq_chip == &ctx->priv.irq_chip_config);
	assert(info->wcd_regmap_irq_chip->irq_drv_data == &ctx->priv);
	assert(codec_regmap_irq_chip.irq_drv_data == NULL);
	if (checkpoint())
		return -EIO;
	assert(!ctx->domain.alive && ctx->map.alive);
	ctx->domain.alive = true;
	*domain = &ctx->domain;
	return 0;
}
int wcd_irq_exit(struct wcd_irq_info *info, struct irq_domain *domain)
{
	assert(domain == &ctx->domain && domain->alive && !ctx->published);
	assert(ctx->wd_count == 0 && !ctx->registered && ctx->map.alive);
	domain->alive = false;
	return 0;
}
int codec_set_micbias_data(struct codec_priv *priv, struct codec_pdata *pdata) { return checkpoint(); }
int codec_wd_handle_irq(int irq, void *data) { return 0; }
int wcd_request_irq(struct wcd_irq_info *info, int irq, const char *name, int (*handler)(int, void *), void *data)
{
	assert(ctx->domain.alive && ctx->wd_count < 3);
	if (checkpoint())
		return -EIO;
	ctx->wd_ids[ctx->wd_count++] = irq;
	return 0;
}
void wcd_disable_irq(struct wcd_irq_info *info, int irq)
{
	assert(ctx->wd_count && ctx->wd_ids[ctx->wd_count - 1] == irq);
}
void wcd_free_irq(struct wcd_irq_info *info, int irq, void *data)
{
	assert(!ctx->published && !ctx->registered && ctx->domain.alive);
	assert(ctx->wd_count && ctx->wd_ids[--ctx->wd_count] == irq);
}
int swr_set_slave_irq(struct swr_device *swr, struct irq_domain *domain)
{
	assert(swr == &ctx->tx);
	if (domain) {
		assert(!ctx->published && ctx->domain.alive && ctx->wd_count == 3);
		if (checkpoint())
			return -EIO;
		ctx->published = true;
		swr->dev.refs++;
	} else {
		assert(ctx->published && !ctx->registered);
		if (ctx->withdraw_error)
			return ctx->withdraw_error;
		ctx->published = false;
		swr->dev.refs--;
	}
	return 0;
}
int snd_soc_register_component(struct device *dev, const void *driver, const void *dais, unsigned int count)
{
	assert(ctx->published && ctx->map.alive && ctx->wd_count == 3);
	if (checkpoint())
		return -EIO;
	ctx->registered = true;
	return 0;
}
void snd_soc_unregister_component(struct device *dev)
{
	assert(ctx->registered && ctx->published && ctx->map.alive && ctx->domain.alive);
	assert(!ctx->notify && !ctx->regulators);
	ctx->registered = false;
}
int codec_handle_swrslv_reset(void *data) { return 0; }
int codec_slave_register_notify(struct swr_device *swr, int (*notify)(void *), void *data)
{
	if (notify) {
		if (checkpoint())
			return 0;
		assert(ctx->registered);
		ctx->notify = true;
	} else {
		assert(ctx->notify && ctx->registered);
		ctx->notify = false;
	}
	return 1;
}
int wcd_init_mb_regulator(struct device *dev)
{
	if (checkpoint())
		return -EIO;
	assert(ctx->registered && !ctx->regulators);
	ctx->regulators = true;
	return 0;
}
void wcd_deinit_mb_regulator(struct device *dev)
{
	assert(ctx->regulators && ctx->registered);
	ctx->regulators = false;
}

#include "bind_production.inc"

static void initialize(struct context *state, int failure)
{
	memset(state, 0, sizeof(*state));
	ctx = state;
	state->dev.data = &state->priv;
	state->dev.pdata = &state->pdata;
	state->rx.dev.refs = state->tx.dev.refs = 1;
	state->pdata.rx_slave = &state->rx_node;
	state->pdata.tx_slave = &state->tx_node;
	state->fail_at = failure;
}
static void clean(struct context *state)
{
	assert(!state->bound && !state->published && !state->registered);
	assert(!state->notify && !state->regulators && !state->wd_count);
	assert(!state->map.alive && !state->domain.alive);
	assert(state->rx.dev.refs == 1 && state->tx.dev.refs == 1);
	assert(!state->priv.rx_swr_dev && !state->priv.tx_swr_dev);
	assert(!state->priv.regmap && !state->priv.virq);
}
int main(void)
{
	struct context state, other;
	initialize(&state, 0);
	assert(codec_bind(&state.dev) == 0);
	int failures = state.checkpoints;
	assert(state.priv.wd_irqs_requested == 3 && state.priv.irq_published);
	codec_unbind(&state.dev);
	clean(&state);
	for (int failure = 1; failure <= failures; failure++) {
		initialize(&state, failure);
		assert(codec_bind(&state.dev) < 0);
		clean(&state);
	}
	initialize(&state, 0);
	assert(codec_bind(&state.dev) == 0);
	initialize(&other, 0);
	other.id1 = 1;
	assert(codec_bind(&other.dev) == 0);
	assert(state.priv.irq_info.wcd_regmap_irq_chip != other.priv.irq_info.wcd_regmap_irq_chip);
	assert(state.priv.irq_chip_config.irq_drv_data == &state.priv);
	assert(other.priv.irq_chip_config.irq_drv_data == &other.priv);
	codec_unbind(&other.dev);
	clean(&other);
	ctx = &state;
	state.withdraw_error = -EINVAL;
	assert(codec_release_bind_resources(&state.dev) == -EINVAL);
	assert(state.domain.alive && state.map.alive && state.wd_count == 3 && state.published);
	assert(!state.registered);
	state.withdraw_error = 0;
	assert(codec_release_bind_resources(&state.dev) == 0);
	clean(&state);
#if CODEC_KIND == 9390
	initialize(&state, 0);
	state.id1 = 2;
	assert(codec_bind(&state.dev) == -ENODEV);
	clean(&state);
#endif
	printf("PASS: codec %d bind, %d acquisition failures, reverse teardown, independent instances and failed withdrawal\\n", CODEC_KIND, failures);
	return 0;
}
