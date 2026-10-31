/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t resource_size_t;
#define U8_MAX UINT8_MAX
#define BIT(n) (1U << (n))
#define PHY_HOST_MODE BIT(0)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define IS_ALIGNED(v, a) (!((v) & ((a) - 1)))
#define dev_dbg(...) ((void)0)
#define dev_info(...) ((void)0)
#define dev_err(...) ((void)0)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define spin_lock_irqsave(p, f) do { assert(!*(p)); *(p) = 1; (f) = 0; } while (0)
#define spin_unlock_irqrestore(p, f) do { assert(*(p)); *(p) = 0; (void)(f); } while (0)

struct device {};
struct module { unsigned int refs; };
static unsigned int devices_released;
static void put_device(struct device *dev) { free(dev); devices_released++; }
static void module_put(struct module *owner) { assert(owner->refs); owner->refs--; }
struct regmap {};
struct dentry;
struct regulator { unsigned int index; int enabled, voltage, load; };
struct clk { int enabled; };
struct work_struct { unsigned int queued; };
struct usb_repeater { struct device *dev; unsigned int flags; };
struct usb_phy { struct device *dev; unsigned int flags; };
struct msm_eusb2_phy {
	struct usb_phy phy;
	void *base;
	struct clk *ref_clk_src, *ref_clk;
	bool clocks_enabled, removing, cable_connected;
	unsigned int vbus_draw;
	int vbus_lock;
	struct work_struct vbus_draw_work;
};
static struct regulator regulators[3];
static unsigned int calls, fail1, fail2, rail_count;
static bool service_fails(void) { ++calls; return calls == fail1 || calls == fail2; }
static int regulator_set_load(struct regulator *r, int value)
{
	if (service_fails()) return -EIO;
	r->load = value;
	return value ? 1 : 0; /* Successful set_load may return a positive mode. */
}
static int regulator_set_voltage(struct regulator *r, int lo, int hi)
{
	assert(lo <= hi);
	if (service_fails()) return -EIO;
	r->voltage = lo;
	return 0;
}
static int regulator_enable(struct regulator *r)
{
	if (service_fails()) return -EIO;
	for (unsigned int i = 0; i < r->index; i++) assert(regulators[i].enabled == 1);
	assert(r->load && r->voltage && !r->enabled);
	r->enabled = 1;
	return 0;
}
static int regulator_disable(struct regulator *r)
{
	if (service_fails()) return -EIO;
	for (unsigned int i = r->index + 1; i < rail_count; i++) assert(!regulators[i].enabled);
	assert(r->enabled == 1);
	r->enabled = 0;
	return 0;
}
static int clk_prepare_enable(struct clk *c)
{
	if (service_fails()) return -EIO;
	assert(!c->enabled);
	c->enabled = 1;
	return 0;
}
static void clk_disable_unprepare(struct clk *c) { assert(c->enabled == 1); c->enabled = 0; }
static void schedule_work(struct work_struct *w) { w->queued++; }
static unsigned int reads, ready_after;
static int regmap_bulk_read(struct regmap *map, unsigned int address, void *value, size_t n)
{
	assert(n == 1);
	if (service_fails()) return -EIO;
	(void)map;
	*(u8 *)value = address == 0 ? 0x42 : (++reads >= ready_after ? BIT(7) : 0);
	return 0;
}
static int regmap_bulk_write(struct regmap *map, unsigned int a, const void *v, size_t n)
{
	(void)map; (void)a; (void)v; assert(n == 1);
	return service_fails() ? -EIO : 0;
}
static int regmap_update_bits(struct regmap *map, unsigned int a, u8 mask, u8 value)
{
	(void)map; (void)a; assert(!(value & ~mask));
	return service_fails() ? -EIO : 0;
}
static void usleep_range(unsigned int lo, unsigned int hi) { assert(lo <= hi); }
static unsigned int apb_reads, apb_ready, apb_resets;
static u8 apb_read(void *address) { (void)address; return ++apb_reads >= apb_ready ? 1 : 0; }
static void eusb2_phy_reset_seq(struct msm_eusb2_phy *phy) { (void)phy; apb_resets++; }
#define readl_poll_timeout(address, value, condition, delay, timeout) ({ \
	int poll_ret = -ETIMEDOUT; \
	for (unsigned int poll = 0; poll <= (timeout) / (delay); poll++) { \
		(value) = apb_read(address); \
		if (condition) { poll_ret = 0; break; } \
	} \
	poll_ret; \
})

/* STAGED_DEFINITIONS */

static unsigned long power_cases, sequence_cases, repeater_cases, clock_cases;

static void reset_rails(struct onyx_usb_supply *s, unsigned int n)
{
	memset(regulators, 0, sizeof(regulators));
	memset(s, 0, 3 * sizeof(*s));
	rail_count = n;
	for (unsigned int i = 0; i < n; i++) {
		regulators[i].index = i;
		s[i].regulator = &regulators[i];
		s[i].min_uv = 10 + i;
		s[i].max_uv = 20 + i;
		s[i].load_ua = 30 + i;
	}
	calls = fail1 = fail2 = 0;
}
static void assert_rail_states(struct onyx_usb_supply *s)
{
	for (unsigned int i = 0; i < rail_count; i++) {
		assert(s[i].enabled == !!regulators[i].enabled);
		assert(s[i].voltage_set == !!regulators[i].voltage);
		assert(s[i].load_set == !!regulators[i].load);
	}
}
static void test_power(void)
{
	struct onyx_usb_supply s[3];
	for (unsigned int n = 1; n <= 3; n++) {
		for (unsigned int first = 0; first <= 6 * n; first++)
			for (unsigned int second = 0; second <= 6 * n; second++) {
				reset_rails(s, n);
				fail1 = first; fail2 = second;
				int ret = onyx_usb_supplies_on(s, n);
				assert(ret == 0 || ret == -EIO);
				assert_rail_states(s);
				(void)onyx_usb_supplies_off(s, n);
				assert_rail_states(s);
				fail1 = fail2 = 0;
				assert(!onyx_usb_supplies_off(s, n));
				assert_rail_states(s);
				for (unsigned int i = 0; i < n; i++) assert(!regulators[i].enabled);
				assert(!onyx_usb_supplies_on(s, n));
				unsigned int before = calls;
				assert(!onyx_usb_supplies_on(s, n) && calls == before);
				assert(!onyx_usb_supplies_off(s, n));
				before = calls;
				assert(!onyx_usb_supplies_off(s, n) && calls == before);
				power_cases++;
			}
	}
}
static void test_sequences(void)
{
	u32 seq[4] = {0};
	for (u32 value = 0; value < 512; value++)
		for (u32 offset = 0; offset < 512; offset++) {
			seq[0] = value; seq[1] = offset;
			assert(!eusb2_repeater_validate_seq(seq, 2) == (value <= 255 && offset <= 255));
			sequence_cases++;
		}
	for (unsigned int size = 0; size <= 512; size++)
		for (unsigned int offset = 0; offset <= 1024; offset++) {
			seq[1] = offset;
			bool valid = size >= 4 && offset % 4 == 0 && offset <= size - 4;
			assert(!msm_eusb2_validate_seq(seq, 2, size) == valid);
			sequence_cases++;
		}
	assert(eusb2_repeater_validate_seq(seq, 3) == -EINVAL);
	assert(msm_eusb2_validate_seq(seq, 3, 512) == -EINVAL);
}
static void test_repeater(void)
{
	struct eusb2_repeater er = {0}; /* All zero tuning values are valid. */
	u32 seq[] = {7, 0x50};
	er.param_override_seq = er.host_param_override_seq = seq;
	er.param_override_seq_cnt = er.host_param_override_seq_cnt = 2;
	for (unsigned int host = 0; host < 2; host++)
		for (unsigned int ready = 1; ready <= 6; ready++)
			for (unsigned int fail = 0; fail <= 25; fail++) {
				er.ur.flags = host;
				calls = reads = fail2 = 0; fail1 = fail; ready_after = ready;
				int ret = eusb2_repeater_init(&er.ur);
				if (fail && calls >= fail) assert(ret == -EIO);
				else assert(ret == (ready <= INIT_MAX_CNT ? 0 : -ETIMEDOUT));
				assert(reads <= INIT_MAX_CNT);
				repeater_cases++;
			}
	calls = fail2 = 0; fail1 = 1;
	assert(eusb2_repeater_get_version(&er.ur) == -EIO);
	calls = fail1 = 0;
	assert(eusb2_repeater_get_version(&er.ur) == 0x42);
	calls = 0; fail1 = 1;
	assert(eusb2_repeater_reset(&er.ur, true) == -EIO);
}
static void test_clocks_and_current(void)
{
	for (unsigned int fail = 0; fail < 3; fail++) {
		struct clk src = {0}, ref = {0};
		struct msm_eusb2_phy phy = {.ref_clk_src = &src, .ref_clk = &ref};
		calls = fail2 = 0; fail1 = fail;
		assert(msm_eusb2_phy_clocks(&phy, true) == (fail ? -EIO : 0));
		assert(src.enabled == !fail && ref.enabled == !fail);
		assert(phy.clocks_enabled == !fail);
		assert(!msm_eusb2_phy_clocks(&phy, false));
		assert(!msm_eusb2_phy_clocks(&phy, false));
		assert(!src.enabled && !ref.enabled);
		clock_cases++;
	}
	struct msm_eusb2_phy phy = {0};
	assert(!msm_eusb2_phy_set_power(&phy.phy, 500));
	assert(phy.vbus_draw == 500 && phy.vbus_draw_work.queued == 1);
	phy.cable_connected = true;
	assert(!msm_eusb2_phy_set_power(&phy.phy, 0) && phy.vbus_draw == 500);
	assert(msm_eusb2_phy_set_power(&phy.phy, UINT_MAX) == -ERANGE);
	phy.removing = true;
	assert(msm_eusb2_phy_set_power(&phy.phy, 900) == -ENODEV);
	assert(phy.vbus_draw_work.queued == 1);
	unsigned char registers[1024];
	phy.base = registers;
	for (apb_ready = 1; apb_ready <= 12; apb_ready++) {
		apb_reads = apb_resets = 0;
		assert(eusb2_phy_apb_cmd_wait(&phy) == (apb_ready <= 11 ? 0 : -ETIMEDOUT));
		assert(apb_reads == (apb_ready <= 11 ? apb_ready : 11));
		assert(apb_resets == (apb_ready > 11));
	}
	struct module owner = {.refs = 1};
	struct usb_repeater_ref ref = {.dev = malloc(sizeof(struct device)), .owner = &owner};
	assert(ref.dev);
	devm_usb_repeater_release_by_node(NULL, &ref);
	assert(!owner.refs && devices_released == 1);
}
int main(void)
{
	test_power(); test_sequences(); test_repeater(); test_clocks_and_current();
	printf("{\"power_failure_pairs\":%lu,\"sequence_cases\":%lu,\"repeater_io_cases\":%lu,"
	       "\"clock_failure_cases\":%lu,\"current_update_teardown_checks\":true,"
	       "\"apb_poll_cases\":12,\"managed_reference_release\":true}\n",
	       power_cases, sequence_cases, repeater_cases, clock_cases);
	return 0;
}
