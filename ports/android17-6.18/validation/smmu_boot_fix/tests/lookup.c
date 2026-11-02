#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

struct fwnode_handle { int id; };
struct arm_smmu_device { int id; };
struct bus_type { int id; };
struct device_driver { int id; };
struct device {
	struct fwnode_handle *fwnode;
	struct arm_smmu_device *data;
	bool bound;
	unsigned int refs;
};

static struct bus_type platform_bus_type;
static struct { struct device_driver driver; } arm_smmu_driver __attribute__((unused));
static struct device *registered;

static struct device *bus_find_device_by_fwnode(struct bus_type *bus,
					       struct fwnode_handle *fwnode)
	__attribute__((unused));
static struct device *bus_find_device_by_fwnode(struct bus_type *bus,
					       struct fwnode_handle *fwnode)
{
	assert(bus == &platform_bus_type);
	if (!registered || registered->fwnode != fwnode)
		return NULL;
	registered->refs++;
	return registered;
}

static struct device *driver_find_device_by_fwnode(struct device_driver *driver,
						  struct fwnode_handle *fwnode)
	__attribute__((unused));
static struct device *driver_find_device_by_fwnode(struct device_driver *driver,
						  struct fwnode_handle *fwnode)
{
	assert(driver == &arm_smmu_driver.driver);
	if (!registered || !registered->bound)
		return NULL;
	return bus_find_device_by_fwnode(&platform_bus_type, fwnode);
}

static void put_device(struct device *dev) __attribute__((unused));
static void put_device(struct device *dev)
{
	if (dev) {
		assert(dev->refs);
		dev->refs--;
	}
}

static struct arm_smmu_device *dev_get_drvdata(struct device *dev)
{
	/* A borrowed result is valid only while the lookup holds its reference. */
	assert(dev && dev->refs);
	return dev->data;
}

#include "production.inc"

int main(void)
{
	struct fwnode_handle fwnode = { 1 }, unknown = { 2 };
	struct arm_smmu_device smmu = { 42 };
	struct device dev = { .fwnode = &fwnode, .data = &smmu, .bound = true };

	registered = &dev;
	assert(arm_smmu_get_by_fwnode(&fwnode) == &smmu);
	assert(dev.refs == 0);
	/* During iommu_device_register(), probe has not reached driver_bound(). */
	dev.bound = false;
	assert(arm_smmu_get_by_fwnode(&fwnode) == &smmu);
	assert(dev.refs == 0);
	assert(arm_smmu_get_by_fwnode(&unknown) == NULL);
	assert(dev.refs == 0);
	dev.data = NULL;
	assert(arm_smmu_get_by_fwnode(&fwnode) == NULL);
	assert(dev.refs == 0);
	registered = NULL;
	assert(arm_smmu_get_by_fwnode(&fwnode) == NULL);
	puts("Five lookup scenarios pass; eager registration and held-reference access verified");
	return 0;
}
