/* Actual I/O and protection helpers with injected bus/device responses. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define GFP_KERNEL 0
#define FG_I2C_RETRY_CNT 3
#define I2C_SMBUS_BLOCK_MAX 32
#define I2C_M_RD 1
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define BQ_FG_REG_ALT_MAC 0
#define CP_MODE_DIV_MAX 3
struct i2c_client { int addr; void *adapter; };
struct i2c_msg { int addr, flags; size_t len; u8 *buf; };
struct mca_platform_base_device { struct i2c_client **plat_i2c; int slave_addr_num; };
struct bq_fg_chip { int data_lock, i2c_rw_lock; u8 regs[1]; };
struct sc8581_device { int unused; };
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define usleep_range(a,b) ((void)0)
static int bus_status, writes, reads;
static int i2c_smbus_write_i2c_block_data(struct i2c_client *c, u8 reg, u8 len, const u8 *data)
{
	assert(c && reg == 0x40 && len <= 32 && data);
	writes++;
	return bus_status;
}
static int i2c_smbus_read_i2c_block_data(struct i2c_client *c, u8 reg, u8 len, void *data)
{
	assert(c && reg == 0x40 && len <= 32 && data);
	reads++;
	return bus_status;
}
static int transfers, transfer_results[3];
static int i2c_transfer(void *adapter, struct i2c_msg *msgs, int num)
{
	(void)adapter;
	assert(msgs && (num == 1 || num == 2));
	int ret = transfer_results[transfers++];
	return ret;
}
static int live_allocs;
static void *kmalloc(size_t size, int flags) { (void)flags; void *p = malloc(size); if (p) live_allocs++; return p; }
static void kfree(void *p) { if (p) live_allocs--; free(p); }
static u8 frame[36];
static int mac_read_error, mac_reads;
static int fg_write_read_block(struct bq_fg_chip *bq, u8 reg, u8 *buf, u8 len, u8 read_len)
{
	(void)bq; (void)reg;
	assert(len == 2 && read_len == 36);
	mac_reads++;
	if (mac_read_error) return mac_read_error;
	memcpy(buf, frame, 36);
	return 0;
}
static int protection_index, protection_fail;
static int protection_call(void) { return ++protection_index == protection_fail ? -EREMOTEIO : 0; }
/* PROTECTION_MOCKS */
/* STAGED_DEFINITIONS */
static void test_bus(void)
{
	struct i2c_client client = { .addr = 0x55 };
	struct i2c_client *clients[] = { &client };
	struct mca_platform_base_device base = { clients, 1 };
	u8 data[35] = { 0, 0x40, 0x55 }, out[32];
	for (int n = 0; n < 3; n++) assert(mca_platform_base_regmap_write(&base, data, n) == -EINVAL);
	assert(mca_platform_base_regmap_write(&base, data, 35) == -EINVAL);
	assert(!writes);
	assert(!mca_platform_base_regmap_write(&base, data, 34) && writes == 1);
	data[0] = 1;
	assert(mca_platform_base_regmap_write(&base, data, 3) == -EINVAL && writes == 1);
	data[0] = 0;
	assert(mca_platform_base_regmap_read(&base, data, 1, out, 2) == -EINVAL);
	assert(mca_platform_base_regmap_read(&base, data, 2, out, 33) == -EINVAL);
	bus_status = 1;
	assert(mca_platform_base_regmap_read(&base, data, 2, out, 2) == -EIO);
	bus_status = 2;
	assert(!mca_platform_base_regmap_read(&base, data, 2, out, 2));
	transfer_results[0] = 1; transfer_results[1] = 2; transfers = 0;
	assert(!__fg_read_block(&client, 0x40, out, 2) && transfers == 2);
	for (int i = 0; i < 3; i++) transfer_results[i] = 1;
	transfers = 0;
	assert(__fg_read_block(&client, 0x40, out, 2) == -EIO && transfers == 3);
	for (int i = 0; i < 3; i++) transfer_results[i] = 0;
	transfers = 0;
	assert(__fg_write_block(&client, 0x40, data, 2) == -EIO && transfers == 3);
	transfer_results[0] = -EREMOTEIO; transfer_results[1] = 1; transfers = 0;
	assert(!__fg_write_block(&client, 0x40, data, 2) && transfers == 2);
	assert(!live_allocs);
}
static void test_mac(void)
{
	struct bq_fg_chip bq = {};
	u8 output[32];
	for (int i = 0; i < 34; i++) frame[i] = i;
	frame[0] = 0x34; frame[1] = 0x12;
	for (int n = 0; n < 256; n++) {
		frame[35] = n;
		if (n >= 4 && n <= 36) {
			unsigned sum = 0;
			for (int i = 0; i < n - 2; i++) sum += frame[i];
			frame[34] = 0xff - (sum & 0xff);
		}
		memset(output, 0xcc, sizeof(output));
		int ret = fg_mac_read_block(&bq, 0x1234, output, 4);
		if (n >= 8 && n <= 36) assert(!ret && !memcmp(output, frame + 2, 4));
		else assert(ret == -EMSGSIZE && output[0] == 0xcc);
	}
	frame[35] = 36;
	frame[34] = checksum(frame, 34);
	for (int n = 0; n < 256; n++) {
		int old = mac_reads;
		int ret = fg_mac_read_block(&bq, 0x1234, output, n);
		if (n > 0 && n <= 32) assert(!ret && mac_reads == old + 1);
		else assert(ret == -EINVAL && mac_reads == old);
	}
	frame[34] ^= 1;
	assert(fg_mac_read_block(&bq, 0x1234, output, 4) == -EIO);
	frame[34] ^= 1;
	assert(fg_mac_read_block(&bq, 0x1235, output, 4) == -EIO);
	mac_read_error = -EREMOTEIO;
	assert(fg_mac_read_block(&bq, 0x1234, output, 4) == -EREMOTEIO);
}
int main(void)
{
	test_bus(); test_mac();
	struct sc8581_device cp = {};
	assert(sc8581_init_protection(&cp, -1) == -EINVAL && !protection_index);
	assert(sc8581_init_protection(&cp, CP_MODE_DIV_MAX) == -EINVAL && !protection_index);
	assert(!sc8581_init_protection(&cp, 0));
	int calls = protection_index;
	for (int n = 1; n <= calls; n++) {
		protection_index = 0; protection_fail = n;
		assert(sc8581_init_protection(&cp, 0) == -EREMOTEIO && protection_index == n);
	}
	printf("{\"passed\":true,\"gauge_encoded_lengths\":256,\"gauge_requested_lengths\":256,\"i2c_short_transfers\":true,\"i2c_bank_bounds\":true,\"protection_failure_points\":%d}\n", calls);
	return 0;
}
