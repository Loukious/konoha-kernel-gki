#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIC_BIAS_1 1
#define MIC_BIAS_2 2
#define MIC_BIAS_3 3
#define MIC_BIAS_4 4
#define GFP_KERNEL 0
#define THIS_MODULE 1
#define REGULATOR_VOLTAGE 1
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(error) ((void *)(intptr_t)(error))
#define dev_err(...) ((void)0)
struct regulator_dev { void *data; int index; };
struct regulator_ops {
	int (*enable)(struct regulator_dev *);
	int (*disable)(struct regulator_dev *);
	int (*is_enabled)(struct regulator_dev *);
};
struct regulator_desc { int owner; const char *of_match, *name; int type; const struct regulator_ops *ops; };
struct regulator_config { struct device *dev; void *driver_data; };
struct resource { void (*release)(void *); void *data; };
struct device {
	bool group, closed;
	int count;
	struct resource resources[6];
};
static int checkpoints, fail_at, allocations, registrations, votes[4], fail_vote;
static bool codec_alive, withdrawing;
static struct regulator_dev *latest;
static const struct regulator_ops *latest_ops;
static void *priv_allocation;
void *rdev_get_drvdata(struct regulator_dev *rdev) { return rdev->data; }
int checkpoint(void) { return ++checkpoints == fail_at ? -EIO : 0; }
int codec_micb_external_event(struct device *dev, int micb, bool enable)
{
	assert(codec_alive && micb >= 1 && micb <= 4);
	if (withdrawing)
		assert(!registrations);
	if (fail_vote == micb)
		return -EIO;
	if (enable) {
		assert(votes[micb - 1] == 0);
		votes[micb - 1]++;
	} else {
		assert(votes[micb - 1] == 1);
		votes[micb - 1]--;
	}
	return 0;
}
void add_resource(struct device *dev, void (*release)(void *), void *data)
{
	assert(dev->group && !dev->closed && dev->count < 6);
	dev->resources[dev->count++] = (struct resource) { release, data };
}
void *devres_open_group(struct device *dev, void *identity, int flags)
{
	assert(!dev->group && dev->count == 0);
	if (checkpoint())
		return NULL;
	dev->group = true;
	return dev;
}
void release_allocation(void *data)
{
	assert(!registrations);
	for (int i = 0; i < 4; i++)
		assert(!votes[i]);
	free(data);
	allocations--;
	priv_allocation = NULL;
}
void *devm_kzalloc(struct device *dev, size_t size, int flags)
{
	if (checkpoint())
		return NULL;
	void *data = calloc(1, size);
	assert(data);
	allocations++;
	priv_allocation = data;
	add_resource(dev, release_allocation, data);
	return data;
}
int devm_add_action_or_reset(struct device *dev, void (*release)(void *), void *data)
{
	if (checkpoint()) {
		release(data);
		return -EIO;
	}
	add_resource(dev, release, data);
	return 0;
}
void release_regulator(void *data) { registrations--; free(data); }
struct regulator_dev *devm_regulator_register(struct device *dev, struct regulator_desc *desc,
					      struct regulator_config *cfg)
{
	/* A previous published regulator may already have an owned enable vote. */
	if (latest && !latest_ops->is_enabled(latest))
		assert(latest_ops->enable(latest) == 0);
	if (checkpoint())
		return ERR_PTR(-EIO);
	struct regulator_dev *rdev = calloc(1, sizeof(*rdev));
	assert(rdev && cfg->dev == dev);
	rdev->data = cfg->driver_data;
	rdev->index = registrations++;
	add_resource(dev, release_regulator, rdev);
	latest = rdev;
	latest_ops = desc->ops;
	return rdev;
}
void devres_close_group(struct device *dev, void *identity)
{
	assert(dev->group && registrations == 4);
	dev->closed = true;
}
void devres_release_group(struct device *dev, void *identity)
{
	assert(dev->group);
	withdrawing = true;
	while (dev->count) {
		struct resource item = dev->resources[--dev->count];
		item.release(item.data);
	}
	dev->group = dev->closed = false;
	withdrawing = false;
	latest = NULL;
}

#include "regulator_production.inc"

static void clean(struct device *dev)
{
	assert(!dev->group && !dev->count && !allocations && !registrations && !priv_allocation);
	for (int i = 0; i < 4; i++)
		assert(!votes[i]);
}
int main(void)
{
	struct device dev = {};
	codec_alive = true;
	assert(wcd_init_mb_regulator(&dev) == 0);
	int failures = checkpoints;
	assert(dev.closed && allocations == 1 && registrations == 4);
	struct wcd_reg_priv *priv = priv_allocation;
	assert(wcd_reg_enable_mb(latest) == 0);
	for (int i = 0; i < 4; i++)
		assert(priv->mb_enabled_count[i] && votes[i] == 1);
	fail_vote = 2;
	assert(wcd_reg_disable_mb(priv->wcd_reg_rdev[1]) == -EIO);
	assert(priv->mb_enabled_count[1] && votes[1] == 1);
	fail_vote = 0;
	assert(wcd_reg_disable_mb(priv->wcd_reg_rdev[1]) == 0);
	assert(!priv->mb_enabled_count[1] && !votes[1]);
	struct regulator_dev invalid = { .data = priv };
	assert(wcd_reg_disable_mb(&invalid) == -ENODEV);
	wcd_deinit_mb_regulator(&dev);
	clean(&dev);
	for (int failure = 1; failure <= failures; failure++) {
		checkpoints = 0;
		fail_at = failure;
		assert(wcd_init_mb_regulator(&dev) < 0);
		clean(&dev);
	}
	codec_alive = false;
	printf("PASS: WCD939x micbias enable/disable accounting, %d acquisition failures and withdrawal before owned-vote release\n", failures);
	return 0;
}
