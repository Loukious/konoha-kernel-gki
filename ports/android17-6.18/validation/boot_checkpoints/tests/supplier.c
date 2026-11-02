#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#define EPROBE_DEFER 517
#define DL_FLAG_AUTOREMOVE_CONSUMER 1
#define DL_FLAG_STATELESS 2

enum link_state { NONE, DORMANT, CONSUMER_PROBE, ACTIVE };
struct device { bool bound, locked; };
struct device_link { enum link_state status; };
static struct device_link link;
static bool link_allocation_fails;
static unsigned int unlocks;

static struct device_link *device_link_add(struct device *consumer,
					  struct device *supplier, unsigned int flags)
{
	assert(consumer != supplier);
	assert(flags == DL_FLAG_AUTOREMOVE_CONSUMER);
	if (link_allocation_fails)
		return NULL;
	link.status = supplier->bound ? CONSUMER_PROBE : DORMANT;
	return &link;
}

static bool device_trylock(struct device *dev) __attribute__((unused));
static bool device_trylock(struct device *dev)
{
	if (dev->locked)
		return false;
	dev->locked = true;
	return true;
}

static bool device_is_bound(struct device *dev) __attribute__((unused));
static bool device_is_bound(struct device *dev)
{
	assert(dev->locked);
	return dev->bound;
}

static void device_unlock(struct device *dev) __attribute__((unused));
static void device_unlock(struct device *dev)
{
	assert(dev->locked);
	dev->locked = false;
	unlocks++;
}

#include "supplier.inc"

/* The core's success contract rejects a dormant managed supplier link. */
static int probe(struct device *consumer, struct device *supplier)
{
	int ret = devfreq_cdev_link_supplier(consumer, supplier);

	if (ret) {
		link.status = NONE;
	} else {
		assert(link.status == CONSUMER_PROBE);
		link.status = ACTIVE;
	}
	return ret;
}

int main(void)
{
	struct device consumer = {}, supplier = {};

	/* Registered but unbound GPU must defer without a successful core bind. */
	assert(probe(&consumer, &supplier) == -EPROBE_DEFER);
	assert(link.status == NONE && !supplier.locked && unlocks == 1);
	/* Supplier completion triggers a later probe that can succeed. */
	supplier.bound = true;
	assert(probe(&consumer, &supplier) == 0);
	assert(link.status == ACTIVE && !supplier.locked && unlocks == 2);
	/* Do not block or unlock somebody else's supplier probe/removal lock. */
	supplier.locked = true;
	assert(probe(&consumer, &supplier) == -EPROBE_DEFER);
	assert(link.status == NONE && supplier.locked && unlocks == 2);
	supplier.locked = false;
	/* Failed link allocation must not leave a lock held. */
	link_allocation_fails = true;
	assert(probe(&consumer, &supplier) == -EINVAL);
	assert(link.status == NONE && !supplier.locked && unlocks == 2);
	link_allocation_fails = false;
	assert(probe(&consumer, &supplier) == 0);
	assert(link.status == ACTIVE && !supplier.locked && unlocks == 3);
	puts("Five supplier lifecycle cases pass");
}
