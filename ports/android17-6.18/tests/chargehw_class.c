/* Actual CP sysfs creator exercises role mapping and partial-failure rollback. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define MCA_CP_MAX_NUM 3
#define CP_SYSFS_ATTRS_SIZE 1
struct device { int role; void *data; };
struct platform_cp_dev { int cp_num; const char *cp_dir_list[3]; struct device *sysfs_dev[3]; int cp_dev_index[3]; };
static int cp_sysfs_attrs, cp_sysfs_field_tbl, cp_sysfs_attr_group;
static const char *const cp_dev_list[] = { "master", "slave", "third" };
static int create_attempts, create_fail, live_devices, remove_count;
static void mca_sysfs_init_attrs(void *attrs, void *table, int size) { (void)attrs; (void)table; (void)size; }
#define mca_sysfs_init_attrs(a,b,c) mca_sysfs_init_attrs(&(a), &(b), (c))
static struct device *mca_sysfs_create_group(const char *cls, const char *name, void *group)
{
	(void)group;
	assert(!strcmp(cls, "xm_power") && name);
	if (++create_attempts == create_fail) return NULL;
	live_devices++;
	return calloc(1, sizeof(struct device));
}
static void mca_sysfs_remove_group(const char *cls, struct device *dev, void *group)
{
	(void)cls; (void)group;
	assert(dev);
	live_devices--; remove_count++;
	free(dev);
}
static void dev_set_drvdata(struct device *dev, void *data) { assert(dev); dev->data = data; }
/* STAGED_DEFINITIONS */
int main(void)
{
	struct platform_cp_dev cp = { .cp_num = 3, .cp_dir_list = {"cp_slave", "cp_master", "cp_third"} };
	for (int fail = 1; fail <= 3; fail++) {
		create_fail = fail; create_attempts = remove_count = 0;
		assert(cp_sysfs_create_group(&cp) == -ENOMEM);
		assert(!live_devices && remove_count == fail - 1);
		for (int i = 0; i < 3; i++) assert(!cp.sysfs_dev[i]);
	}
	create_fail = create_attempts = remove_count = 0;
	assert(!cp_sysfs_create_group(&cp) && live_devices == 3);
	assert(*(int *)cp.sysfs_dev[0]->data == 1);
	assert(*(int *)cp.sysfs_dev[1]->data == 0);
	assert(*(int *)cp.sysfs_dev[2]->data == 2);
	cp_sysfs_remove_groups(&cp);
	assert(!live_devices && remove_count == 3);
	cp.cp_dir_list[1] = "bad";
	assert(cp_sysfs_create_group(&cp) == -EINVAL && !live_devices);
	cp.cp_num = 4;
	assert(cp_sysfs_create_group(&cp) == -EINVAL && !live_devices);
	cp.cp_num = 0;
	assert(cp_sysfs_create_group(&cp) == -EINVAL && !live_devices);
	puts("{\"passed\":true,\"sysfs_creation_failure_points\":3,\"role_mapping\":true,\"count_and_unknown_role_bounds\":true}");
	return 0;
}
