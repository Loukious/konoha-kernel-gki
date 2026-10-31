/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int64_t s64;
typedef uint8_t u8;
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define div_s64(a, b) ((a) / (b))
#define dev_warn(...) ((void)0)
#define BCL_IBAT_RANGE_LVL0 0
static const int bcl_ibat_ext_ranges[] = {10, 20, 25};

/* DRIVER_CONSTANTS */
/* DRIVER_TYPES */

struct mutex { bool held; };
static void mutex_lock(struct mutex *lock) { assert(!lock->held); lock->held = true; }
static void mutex_unlock(struct mutex *lock) { assert(lock->held); lock->held = false; }
#define lockdep_assert_held(lock) assert((lock)->held)

struct bcl_desc { int ibat_scaling_factor; };
struct bcl_device {
	bool ibat_use_qg_adc, no_bit_shift;
	struct mutex stats_lock;
	const struct bcl_desc *desc;
	unsigned long long last_bpm_read_ts;
};
struct bcl_peripheral_data {
	struct mutex state_trans_lock;
	struct bcl_device *dev;
	int trip_thresh, irq_num;
	bool irq_enabled, irq_wake_enabled;
};

static unsigned long long sched_clock(void) { return 123456; }
static int sign_extend32(uint32_t value, int bit)
{
	uint32_t mask = 1U << bit;
	return (int)(value & (mask - 1)) - (int)(value & mask);
}
static uint16_t get_unaligned_le16(const void *ptr)
{
	const uint8_t *p = ptr;
	return p[0] | ((uint16_t)p[1] << 8);
}

static uint8_t registers[11];
static int hold_error, read_error, release_error, holds, releases, reads;
static int bcl_write_register(struct bcl_device *bcl, int offset, uint8_t value)
{
	assert(bcl->stats_lock.held && offset == BPM_EN_OFFSET);
	if (value == BPM_HOLD) {
		holds++;
		return hold_error;
	}
	assert(value == BPM_CLR);
	releases++;
	return release_error;
}
static int bcl_read_multi_register(struct bcl_device *bcl, int offset, void *data, size_t len)
{
	assert(bcl->stats_lock.held && offset == BPM_MAX_IBAT_OFFSET && len == 11);
	reads++;
	if (read_error)
		return read_error;
	memcpy(data, registers, len);
	return 0;
}

static int irq_depth = 1, wake_refs, wake_error;
static void enable_irq(int irq) { assert(irq == 31 && irq_depth == 1); irq_depth--; }
static void disable_irq_nosync(int irq) { assert(irq == 31 && irq_depth == 0); irq_depth++; }
static int enable_irq_wake(int irq)
{
	assert(irq == 31 && !wake_refs);
	if (wake_error)
		return wake_error;
	wake_refs++;
	return 0;
}
static int disable_irq_wake(int irq)
{
	assert(irq == 31 && wake_refs == 1);
	wake_refs--;
	return 0;
}

/* DRIVER_HELPERS */

static void test_conversion(void)
{
	const int scales[] = {61037, 194637, 366220};
	struct bcl_device bcl = {};
	int value;

	for (unsigned int i = 0; i < sizeof(scales) / sizeof(scales[0]); i++)
		for (int sample = -32768; sample <= 32767; sample++) {
			value = sample;
			convert_adc_nu_to_mu_val(&value, scales[i]);
			assert(value == (int)((int64_t)sample * scales[i] / 1000000));
		}
	for (int mode = 0; mode < 3; mode++) {
		bcl.ibat_use_qg_adc = mode == 0;
		bcl.no_bit_shift = mode == 1;
		value = INT_MAX;
		convert_ibat_to_adc_val(&bcl, &value, 937530);
		assert(value == (int)((int64_t)INT_MAX * (mode == 0 ? 40000 :
							 mode == 1 ? 10000 : 20000) / 937530));
		value = -32768;
		convert_adc_to_ibat_val(&bcl, &value, 2343825);
		assert(value == (int)(-32768LL * 2343825 / (mode == 0 ? 20000 : 10000)));
	}
	assert(convert_ibat_to_ccm_val(INT_MIN) == 0);
	assert(convert_ibat_to_ccm_val(799) == 0);
	assert(convert_ibat_to_ccm_val(800) == 0);
	assert(convert_ibat_to_ccm_val(1399) == 5);
	assert(convert_ibat_to_ccm_val(2200) == 14);
	assert(convert_ibat_to_ccm_val(13600) == 14);
	assert(convert_ibat_to_ccm_val(INT_MAX) == 14);
}

static void test_snapshot(void)
{
	struct bcl_desc desc = { .ibat_scaling_factor = 61037 };
	struct bcl_device bcl = { .desc = &desc };
	struct bcl_bpm_stats sample, before;
	const uint8_t raw[] = {0xff, 0x7f, 0x00, 0x40, 0x00, 0x80, 0xff, 0xff, 3, 5, 7};

	memcpy(registers, raw, sizeof(raw));
	assert(!get_bpm_stats(&bcl, &sample));
	assert(sample.max_ibat == 1999 && sample.sync_vbat == 3188);
	assert(sample.min_vbat == -6377 && sample.sync_ibat == 0);
	assert(sample.lvl0_cnt == 3 && sample.lvl1_cnt == 5 && sample.lvl2_cnt == 7);
	assert(bcl.last_bpm_read_ts == 123456);
	for (int error = 0; error < 4; error++) {
		memset(&sample, 0x5a, sizeof(sample));
		memcpy(&before, &sample, sizeof(before));
		hold_error = error == 0 ? -EIO : 0;
		read_error = error == 1 || error == 3 ? -EREMOTEIO : 0;
		release_error = error >= 2 ? -ETIMEDOUT : 0;
		holds = releases = reads = 0;
		bcl.last_bpm_read_ts = 0;
		assert(get_bpm_stats(&bcl, &sample) ==
		       (hold_error ? hold_error : read_error ? read_error : release_error));
		assert(!memcmp(&sample, &before, sizeof(sample)));
		assert(!bcl.last_bpm_read_ts && !bcl.stats_lock.held && holds == 1);
		assert(releases == (error != 0) && reads == (error != 0));
	}
}

static void test_irq_lifetime(void)
{
	struct bcl_device bcl = {};
	struct bcl_peripheral_data data = { .dev = &bcl };

	mutex_lock(&data.state_trans_lock);
	assert(!bcl_set_lbat_irq(&data, 1));
	assert(data.trip_thresh == 1 && irq_depth == 1 && !wake_refs);
	data.irq_num = 31;
	for (int i = 0; i < 100; i++) {
		assert(!bcl_set_lbat_irq(&data, 1));
		assert(!bcl_set_lbat_irq(&data, 1));
		assert(!irq_depth && wake_refs == 1);
		assert(!bcl_set_lbat_irq(&data, INT_MAX));
		assert(!bcl_set_lbat_irq(&data, INT_MAX));
		assert(irq_depth == 1 && !wake_refs);
	}
	wake_error = -EINVAL;
	assert(!bcl_set_lbat_irq(&data, 1));
	assert(!irq_depth && !wake_refs && !data.irq_wake_enabled);
	assert(!bcl_set_lbat_irq(&data, INT_MAX));
	wake_error = 0;
	assert(!bcl_set_lbat_irq(&data, 1));
	mutex_unlock(&data.state_trans_lock);
	bcl_irq_quiesce(&data);
	assert(irq_depth == 1 && !wake_refs && !data.irq_num);
	mutex_lock(&data.state_trans_lock);
	assert(!bcl_set_lbat_irq(&data, 1));
	assert(irq_depth == 1 && !wake_refs);
	mutex_unlock(&data.state_trans_lock);
}

int main(void)
{
	test_conversion();
	test_snapshot();
	test_irq_lifetime();
	puts("BCL: 196608 ADC cases, arithmetic boundaries, snapshot failures and IRQ lifetimes passed");
	return 0;
}
