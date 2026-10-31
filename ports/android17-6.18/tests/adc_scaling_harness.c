/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
struct u32_fract { u32 numerator, denominator; };
struct list_head { struct list_head *next, *prev; };
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define div64_s64(a, b) ((a) / (b))
#define div_s64(a, b) ((a) / (b))
#define do_div(a, b) ((a) /= (b))
#define milli_kelvin_to_millicelsius(t) ((t) - 273150)
#define is_power_of_2(v) ((v) && !((v) & ((v) - 1)))
#define __ffs(v) __builtin_ctzl(v)
#define __ffs64(v) __builtin_ctzll(v)
#define EXPORT_SYMBOL(...)
#define EXPORT_SYMBOL_GPL(...)
#define MODULE_LICENSE(...)
#define MODULE_DESCRIPTION(...)
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)

/* DRIVER_TYPES */
/* DRIVER_CODE */

int main(void)
{
	const struct adc5_data variants[] = {
		{ .full_scale_code_volt = 0x70e4, .full_scale_code_cur = 0x2ee0,
		  .full_scale_code_raw = 0x4000 },
		{ .full_scale_code_volt = 0x70e4, .full_scale_code_cur = 0x4e20,
		  .full_scale_code_raw = 0xffff },
	};
	unsigned long checks = 0;
	int result, previous, ret;

	for (unsigned int v = 0; v < ARRAY_SIZE(variants); v++) {
		const struct adc5_data *data = &variants[v];

		for (int scale = SCALE_HW_CALIB_DEFAULT; scale < SCALE_HW_CALIB_INVALID; scale++)
			for (unsigned int code = 0; code <= UINT16_MAX; code++) {
				ret = qcom_adc5_hw_scale(scale, 0, data, code, &result);
				assert(ret == 0 || ret == -EINVAL || ret == -ERANGE);
				checks++;
			}
		previous = INT_MAX;
		for (unsigned int code = 0; code < data->full_scale_code_raw; code++) {
			assert(!qcom_adc5_hw_scale(SCALE_HW_CALIB_THERM_100K_PU_PM7,
						  0, data, code, &result));
			if (result > previous)
				fprintf(stderr, "Gen%u code=%u: temperature rose from %d to %d\n",
					v + 3, code, previous, result);
			assert(result <= previous);
			previous = result;
		}
		for (int temp = -40000; temp <= 125000; temp++) {
			struct adc_tm_config threshold = {
				.high_thr_temp = temp, .low_thr_temp = temp,
			};

			adc_tm_scale_therm_voltage_100k_gen3(&threshold, data);
			assert(!qcom_adc5_hw_scale(SCALE_HW_CALIB_THERM_100K_PU_PM7,
						  0, data, threshold.low_thr_voltage, &result));
			assert(result >= temp);
			assert(!qcom_adc5_hw_scale(SCALE_HW_CALIB_THERM_100K_PU_PM7,
						  0, data, threshold.high_thr_voltage, &result));
			assert(result <= temp);
		}
		result = 1234;
		assert(qcom_adc5_hw_scale(SCALE_HW_CALIB_DEFAULT, UINT_MAX, data, 0, &result) == -EINVAL);
		assert(result == 1234);
		assert(qcom_adc5_hw_scale(-1, 0, data, 0, &result) == -EINVAL);
		assert(qcom_adc5_hw_scale(SCALE_HW_CALIB_INVALID, 0, data, 0, &result) == -EINVAL);
		assert(qcom_adc5_hw_scale(SCALE_HW_CALIB_THERM_100K_PU_PM7,
					 0, data, data->full_scale_code_raw, &result) == -EINVAL);
	}
	printf("ADC scaling: %lu scale/code cases, thermistor monotonicity and 330002 threshold pairs passed\n", checks);
	return 0;
}
