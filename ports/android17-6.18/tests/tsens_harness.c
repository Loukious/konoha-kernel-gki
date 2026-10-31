/* SPDX-License-Identifier: GPL-2.0-only */
/* Register shims are confined to this host test, never linked into a module. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
#define SLOPE_FACTOR 1000
#define VALID_BIT (1U << 21)
struct tsens_priv;
struct thermal_zone_device;
enum tsens_ver { VER_0, VER_0_1, VER_1_X, VER_2_X };
struct regmap_field { unsigned int unused; };
/* DRIVER_TYPES */
struct tsens_priv {
	struct tsens_features *feat;
	struct regmap_field *rf[1];
};
static u32 samples[3];
static unsigned int reads, fail_read;
static int regmap_field_read(struct regmap_field *field, unsigned int *value)
{
	(void)field;
	assert(reads < 3);
	if (++reads == fail_read)
		return -EIO;
	*value = samples[reads - 1];
	return 0;
}
static int32_t sign_extend32(uint32_t value, int index)
{
	return (int32_t)(value << (31 - index)) >> (31 - index);
}
/* DRIVER_HELPERS */

static void check(struct tsens_sensor *sensor, u32 first, u32 second, u32 third,
		  unsigned int error_at, int expected_status, int expected_temp)
{
	int temp = 0x12345678;

	samples[0] = first;
	samples[1] = second;
	samples[2] = third;
	reads = 0;
	fail_read = error_at;
	assert(tsens_hw_to_mC(sensor, 0, &temp) == expected_status);
	assert(temp == (expected_status ? 0x12345678 : expected_temp));
}

int main(void)
{
	struct tsens_features feat = {
		.ver_major = VER_2_X,
		.valid_bit = VALID_BIT,
		.last_temp_mask = 0xfff,
		.last_temp_resolution = 11,
	};
	struct tsens_priv priv = { .feat = &feat };
	struct tsens_sensor sensor = { .priv = &priv, .slope = 3200, .offset = 404000 };

	for (int raw = 0; raw < 4096; raw++) {
		int expected = (raw >= 2048 ? raw - 4096 : raw) * 100;

		check(&sensor, raw | VALID_BIT, 0, 0, 0, 0, expected);
		assert(reads == 1);
	}
	check(&sensor, 100, 100, 200, 0, 0, 10000);
	check(&sensor, 100, 200, 200, 0, 0, 20000);
	check(&sensor, 100, 200, 300, 0, -EAGAIN, 0);
	check(&sensor, 100, VALID_BIT | 400, 0, 0, 0, 40000);
	assert(reads == 2);
	check(&sensor, 100, 200, VALID_BIT | 0xf38, 0, 0, -20000);
	assert(reads == 3);
	for (unsigned int failure = 1; failure <= 3; failure++)
		check(&sensor, 100, 100, 100, failure, -EIO, 0);
	feat.ver_major = VER_0;
	feat.adc = 1;
	check(&sensor, 500, 0, 0, 0, 0, 30000);
	check(&sensor, 308, 0, 0, 0, 0, -30000);
	puts("PASS: 4096 signed temperatures and 10 stability/error/calibration cases");
	return 0;
}
