// SPDX-License-Identifier: GPL-2.0
/*
 * bq27z561 fuel gauge driver
 *
 * Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/smartchg/smart_chg_class.h>
#include <linux/unistd.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_event.h>
#include <mca/strategy/strategy_class.h>
#include "hwid.h"
#include "inc/bq27z561.h"
#include "inc/bq27z561_nfg1000_fw.h"
#include <mca/platform/platform_buckchg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "fuelgauge_ic"
#endif

#define BLOCK_LENGTH_MAX	64
#define REG_BLOCK_LENGTH	32
#define FG_IC_SOH_TH_DEFAUL 92
#define FG_IC_CYCLE_TH_DEFAUL 400
#define FG_IC_CYCLE_STEP_DEFAUL 50
#define FG_I2C_RETRY_CNT	5
#define FG_IC_RESISTANCE_ID_DEFAUL 100000

#define FG_TEMP_LOW_TH -1280
#define FG_TEMP_HIGH_TH 1280
#define FG_VOLT_LOW_TH 0
#define FG_VOLT_HIGH_TH 10000
#define FG_CURR_LOW_TH -25000
#define FG_CURR_HIGH_TH 6000
#define FG_RM_FCC_LOW_TH 0
#define FG_RM_FCC_HIGH_TH 9000
#define FG_RM_FCC_DEFAULT_TH 2500

#define BYTES_TO_U16(bytes) \
	((u16)((bytes)[0] | ((bytes)[1] << 8)))

static int fg_convert_bytes_to_volt(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_curr(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_temp(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_original_temp(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_rm(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_fcc(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_cycle_count(struct bq_fg_chip *fg, u8 *bytes);

static int fg_get_raw_soc(struct bq_fg_chip *bq, int *raw_soc);
static int fg_read_current(struct bq_fg_chip *bq, int *curr);
static int fg_read_temperature(struct bq_fg_chip *bq, int *temp);
static int fg_read_original_temperature(struct bq_fg_chip *bq, int *temp);
static int fg_get_chip_ok(struct bq_fg_chip *bq, int *ok);
static int fg_get_seal(struct bq_fg_chip *bq, int *value);
static int fg_update_record_voltage_level(struct bq_fg_chip *bq);
static inline unsigned long fg_convert_u8_to_u16(unsigned char lsb, unsigned char hsb);
static int fg_read_max_cell_volt(struct bq_fg_chip *bq, int *max_volt);


static int __fg_read_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	struct i2c_msg msgs[2];
	int ret = -EIO, attempt;

	if (!buf || !len)
		return -EINVAL;
	msgs[0] = (struct i2c_msg) { .addr = client->addr, .len = 1, .buf = &reg };
	msgs[1] = (struct i2c_msg) {
		.addr = client->addr, .flags = I2C_M_RD, .len = len, .buf = buf,
	};
	for (attempt = 0; attempt < FG_I2C_RETRY_CNT; attempt++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs))
			return 0;
		if (ret >= 0)
			ret = -EIO;
		usleep_range(4000, 4100);
	}
	return ret;
}

static int __fg_write_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	struct i2c_msg msg;
	u8 *data;
	int ret = -EIO, attempt;

	if (!buf || !len)
		return -EINVAL;
	data = kmalloc(len + 1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data[0] = reg;
	memcpy(data + 1, buf, len);
	msg = (struct i2c_msg) { .addr = client->addr, .len = len + 1, .buf = data };
	for (attempt = 0; attempt < FG_I2C_RETRY_CNT; attempt++) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1) {
			ret = 0;
			break;
		}
		if (ret >= 0)
			ret = -EIO;
		usleep_range(4000, 4100);
	}
	kfree(data);
	return ret;
}

static int __fg_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret < 0) {
			mca_log_err("i2c write byte fail: can't write 0x%02X to reg 0x%02X, ret:%d, cnt:%d\n",
					val, reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		} else {
			return ret;
		}
	}
	mca_log_err("i2c write byte fail: can't write 0x%02X to reg 0x%02X, ret:%d, cnt max:%d\n",
			val, reg, ret, FG_I2C_RETRY_CNT);

	return ret;
}

static int __fg_read_byte(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0) {
			mca_log_err("i2c read byte fail: can't read reg 0x%02X\n, ret:%d, cnt:%d",
					reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		} else {
			*val = (u8)ret;
			return 0;
		}
	}

	mca_log_err("i2c read byte fail: can't read reg 0x%02X\n, ret:%d, cnt max:%d",
			reg, ret, FG_I2C_RETRY_CNT);

	return ret;
}

static int __fg_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0) {
			mca_log_err("i2c read word fail: can't read from reg 0x%02X, ret:%d, cnt:%d\n",
					reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		}  else {
			*val = (u16)ret;
			return 0;
		}
	}

	mca_log_err("i2c read word fail: can't read from reg 0x%02X, ret:%d, cnt max:%d\n",
			reg, ret, FG_I2C_RETRY_CNT);

	return ret;
}

static int __fg_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	int ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_write_word_data(client, reg, val);
		if (ret < 0) {
			mca_log_err("i2c write word fail: can't write 0x%04X to reg 0x%02X, ret:%d, cnt:%d\n",
					val, reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		} else {
			return ret;
		}
	}
	mca_log_err("i2c write word fail: can't write 0x%04X to reg 0x%02X, ret:%d, cnt max:%d\n",
			val, reg, ret, FG_I2C_RETRY_CNT);
	return ret;
}

static int fg_write_byte(struct bq_fg_chip *bq, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_byte(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int fg_read_byte(struct bq_fg_chip *bq, u8 reg, u8 *val)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_read_byte(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int fg_read_word(struct bq_fg_chip *bq, u8 reg, u16 *val)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_read_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int fg_write_word(struct bq_fg_chip *bq, u8 reg, u16 val)
{
	int ret = 0;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

static int fg_write_block(struct bq_fg_chip *bq, u8 reg, u8 *data, u8 len)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_block(bq->client, reg, data, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static int fg_read_block(struct bq_fg_chip *bq, u8 reg, u8 *buf, u8 len)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_read_block(bq->client, reg, buf, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static u8 checksum(u8 *data, u8 len)
{
	u8 i;
	u16 sum = 0;

	for (i = 0; i < len; i++)
		sum += data[i];

	sum &= 0xFF;

	return 0xFF - sum;
}

__maybe_unused
static char *fg_print_buf(const char *msg, u8 *buf, u8 len)
{
	int i;
	int idx = 0;
	int num;
	static u8 strbuf[128];

	for (i = 0; i < len; i++) {
		num = sprintf(&strbuf[idx], "%02X ", buf[i]);
		idx += num;
	}
	mca_log_debug("%s %s\n", msg, strbuf);

	return strbuf;
}

static int fg_write_read_block(struct bq_fg_chip *bq, u8 reg, u8 *data, u8 len, u8 lens)
{
	int ret;

	ret = __fg_write_block(bq->client, reg, data, len);
	if (ret < 0)
		return ret;

	usleep_range(4000, 4100);
	ret = __fg_read_block(bq->client, reg, data, lens);

	return ret;
}

static int fg_mac_read_block(struct bq_fg_chip *bq, u16 cmd, u8 *buf, u8 len)
{
	u8 response[36] = { (u8)cmd, (u8)(cmd >> 8) };
	u8 length;
	int ret;

	if (!buf || !len || len > 32)
		return -EINVAL;
	mutex_lock(&bq->data_lock);
	mutex_lock(&bq->i2c_rw_lock);
	ret = fg_write_read_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], response, 2,
				 sizeof(response));
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		goto out;
	length = response[35];
	/* Length includes the command, checksum and length bytes. */
	if (length < 4 || length > sizeof(response) || len > length - 4) {
		ret = -EMSGSIZE;
		goto out;
	}
	if (response[0] != (u8)cmd || response[1] != (u8)(cmd >> 8) ||
	    checksum(response, length - 2) != response[34]) {
		ret = -EIO;
		goto out;
	}
	memcpy(buf, response + 2, len);
	ret = 0;
out:
	mutex_unlock(&bq->data_lock);
	return ret;
}

static int fg_mac_write_block(struct bq_fg_chip *bq, u16 cmd, u8 *data, u8 len)
{
	int ret;
	u8 cksum;
	u8 t_buf[40];
	int i;
	bool secure_delay = false;

	if (!data || !len || len > 32)
		return -EINVAL;

	if (cmd > 0x4000 || cmd == FG_MAC_CMD_UI_SOH || cmd == FG_MAC_CMD_CLEAR_COUNT_DATA)
		secure_delay = true;

	t_buf[0] = (u8)cmd;
	t_buf[1] = (u8)(cmd >> 8);
	for (i = 0; i < len; i++)
		t_buf[i+2] = data[i];

	mutex_lock(&bq->data_lock);
	/*write command/addr, data*/
	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_block(bq->client, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, len + 2);
	if (ret < 0) {
		mca_log_err("wirte block failed %d %d\n", t_buf[0], t_buf[1]);
		goto err_handle;
	}
	//fg_print_buf("mac_write_block", t_buf, len + 2);

	cksum = checksum(t_buf, len + 2);
	t_buf[0] = cksum;
	t_buf[1] = len + 4; /*buf length, cmd, CRC and len byte itself*/
	/*write checksum and length*/
	ret = __fg_write_block(bq->client, bq->regs[BQ_FG_REG_MAC_CHKSUM], t_buf, 2);
	if (secure_delay)
		msleep(100);
err_handle:
	mutex_unlock(&bq->i2c_rw_lock);
	mutex_unlock(&bq->data_lock);

	return ret;
}

static int fg_read_i2c_block_data(struct bq_fg_chip *info, u8 reg, unsigned short len, u8 *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(info->dev);

	if (!buf || !len)
		return -EINVAL;

	if (!client || !client->adapter)
		return -ENODEV;

	{
		struct i2c_msg msgs[2] = {
			{ .addr = client->addr, .flags = 0, .len = 1,
			  .buf = &reg },
			{ .addr = client->addr, .flags = I2C_M_RD, .len = len,
			  .buf = buf },
		};

		ret = i2c_transfer(client->adapter, msgs, 2);
	}
	if (ret < 0) {
		mca_log_err("I2C read failed\n");
		return ret;
	}

	return ret == 2 ? 0 : -EIO;
}

#define FG_BATT_INFO_LEN 16
static int fg_get_batt_info(struct bq_fg_chip *fg, void *pdata)
{
	int ret;
	struct fg_batt_info *data = (struct fg_batt_info *)pdata;
	u8 buf[FG_BATT_INFO_LEN];
	u8 reg;
	unsigned short len;
	int offset;
	int i, retry_count;

	/* 5: wait resume */
	for (retry_count = 0; retry_count < 5; retry_count++) {
		if (atomic_read(&fg->pm_suspend)) {
			mca_log_info("fg_get_batt_info wait resume\n");
			msleep(30); /* 30: wait i2c client device resume */
		} else {
			break;
		}
	}

	mutex_lock(&fg->i2c_rw_lock);
	reg = bq27z561_regs[BQ_FG_REG_TEMP];
	len = 16;
	memset(buf, 0, FG_BATT_INFO_LEN);
	ret = fg_read_i2c_block_data(fg, reg, len, buf);
	mutex_unlock(&fg->i2c_rw_lock);
	if (ret < 0) {
		mca_log_err("could not read batt data, reg=0x%02x, ret=%d\n", reg, ret);
		return ret;
	}

	for (i = 0; i < len; i += 8) {
		mca_log_debug("%02x regs[%d] = %02x%02x%02x%02x %02x%02x%02x%02x\n",
			reg, i, buf[i], buf[i + 1], buf[i + 2], buf[i + 3],
			buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}

	offset = bq27z561_regs[BQ_FG_REG_CN] - reg;
	data->curr = fg_convert_bytes_to_curr(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_VOLT] - reg;
	data->volt = fg_convert_bytes_to_volt(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_TEMP] - reg;
	data->temp = fg_convert_bytes_to_temp(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_RM] - reg;
	data->rm = fg_convert_bytes_to_rm(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_FCC] - reg;
	data->fcc = fg_convert_bytes_to_fcc(fg, buf + offset);

	fg_read_max_cell_volt(fg, &data->vcell_max);

	mutex_lock(&fg->i2c_rw_lock);
	reg = bq27z561_regs[BQ_FG_REG_CC];
	len = 8;
	memset(buf, 0, FG_BATT_INFO_LEN);
	ret = fg_read_i2c_block_data(fg, reg, len, buf);
	mutex_unlock(&fg->i2c_rw_lock);
	if (ret < 0) {
		mca_log_err("could not read batt data, reg=0x%02x, ret=%d\n", reg, ret);
		return ret;
	}

	for (i = 0; i < len; i += 8) {
		mca_log_debug("%02x regs[%d] = %02x%02x%02x%02x %02x%02x%02x%02x\n",
			reg, i, buf[i], buf[i + 1], buf[i + 2], buf[i + 3],
			buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}

	offset = bq27z561_regs[BQ_FG_REG_CC] - reg;
	data->cycle_count = fg_convert_bytes_to_cycle_count(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_SOC] - reg;
	data->rsoc = BYTES_TO_U16(buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_SOH] - reg;
	data->soh = BYTES_TO_U16(buf + offset);

	mca_log_debug("curr: %d, volt: %d, temp: %d, rsoc: %d, rm: %d, fcc: %d, cycle_count: %d, soh: %d\n",
		data->curr, data->volt, data->temp, data->rsoc,
		data->rm, data->fcc, data->cycle_count, data->soh);
	return 0;
}

#define READ_FASTCHARGE_REG 0
static int fg_get_fastcharge_mode(struct bq_fg_chip *bq, int *ffc)
{
#if READ_FASTCHARGE_REG
	u8 data[3];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHARGING_STATUS, data, 3);
	if (ret < 0) {
		mca_log_err("could not write fastcharge = %d\n", ret);
		return ret;
	}

	if(bq->fg_vendor == MPC7021){
		*ffc = (data[0] & BIT(7)) >> 7;
	}else {
		*ffc = (data[2] & FG_FLAGS_FASTCHAGE) >> 5;
	}

	return 0;
#endif

	*ffc = bq->fast_mode;
	return 0;

}

static int fg_get_gague_mode(struct bq_fg_chip *bq)
{
	u8 data[4];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_GAGUE_STATUS, data, 4);
	if (ret < 0) {
		mca_log_err("could not write gague mode = %d\n", ret);
		return ret;
	}

	bq->batt_fd_1 = !!(data[0] & BIT(0));
	bq->batt_fc_1 = !!(data[0] & BIT(1));
	bq->batt_td_1 = !!(data[0] & BIT(2));
	bq->batt_tc_1 = !!(data[0] & BIT(3));
	mca_log_info("gague status FD %d, FC = %d, TD %d TC %d\n",
		bq->batt_fd_1, bq->batt_fc_1, bq->batt_td_1, bq->batt_tc_1);

	return 0;
}

static int fg_set_fastcharge_mode(struct bq_fg_chip *bq, bool enable)
{
	u8 data[2];
	int ret, len = 0;

	if (bq->fast_mode == enable)
		return 0;

	if (atomic_read(&bq->digest_in_process)) {
		mca_log_info("wait fg auth to be done\n");
		return 0;
	}

	if(bq->fg_vendor != MPC7021)
		len = 2;

	data[0] = enable;
	mca_log_info("set fastcharge mode: enable: %d\n", enable);
	if (enable) {
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FASTCHARGE_EN, data, len);

		if (ret < 0) {
			mca_log_info("could not write fastcharge = %d\n", ret);
			return ret;
		}
	} else {
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FASTCHARGE_DIS, data, len);
		if (ret < 0) {
			mca_log_info("could not write fastcharge = %d\n", ret);
			return ret;
		}
	}
	bq->fast_mode = enable;

	return ret;
}

static int fg_read_status(struct bq_fg_chip *bq)
{
	int ret;
	u16 flags;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_BATT_STATUS], &flags);
	if (ret < 0)
		return ret;

	bq->batt_fc = !!(flags & FG_FLAGS_FC);
	bq->batt_fd = !!(flags & FG_FLAGS_FD);
	bq->batt_rca = !!(flags & FG_FLAGS_RCA);
	bq->batt_dsg = !!(flags & FG_FLAGS_DSG);

	return 0;
}

static int fg_get_manu_info(unsigned char val, int base, int step)
{
	int index = 0;
	int data = 0;

	mca_log_info("val:%d, '0':%d, 'A':%d, 'a':%d\n", val, '0', 'A', 'a');
	if (val > '0' && val < '9')
		index = val - '0';
	if (val > 'A' && val < 'Z')
		index = val - 'A' + 10;
	if (val > 'a' && val < 'z')
		index = val - 'a' + 36;

	mca_log_info("base:%d, index:%d, step:%d\n", base, index, step);
	data = base + index * step;

	return data;
}

static int fg_get_manufacture_data(struct bq_fg_chip *bq)
{
	u8 t_buf[40];
	int ret;
	int i;
	int byte, base, step;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, t_buf,
				FFC_TERMINATION_BYTE + 1);
	if (ret < 0) {
		mca_log_info("failed to get MANE NAME\n");
		return ret;
	}

	if (strncmp(t_buf, "MI", 2) != 0) {
		mca_log_info("Can not get MI battery data\n");
		manu_info[TERMINATION].data = BQ27Z561_DEFUALT_TERM;
		manu_info[FFC_TERMINATION].data = BQ27Z561_DEFUALT_FFC_TERM;
		manu_info[RECHARGE_VOL].data = BQ27Z561_DEFUALT_RECHARGE_VOL;
		return 0;
	}

	for (i = 0; i < MANU_DATA_LEN; i++) {
		byte = manu_info[i].byte;
		base = manu_info[i].base;
		step = manu_info[i].step;
		manu_info[i].data = fg_get_manu_info(t_buf[byte], base, step);
		mca_log_info("manu_info[%d].data %d\n", i, manu_info[i].data);
	}

	return 0;
}

static int fg_read_cell_vendor(struct bq_fg_chip *bq)
{
	u8 t_buf[40];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 4);
	if (ret < 0) {
		mca_log_info("failed to get MANE NAME\n");
		return 0;
	}
	mca_log_info("%s\n", t_buf);

	switch (t_buf[1]) {
	case FG_CELL_NAME_A:
		bq->cell_name = "samsung";
		break;
	case FG_CELL_NAME_B:
		bq->cell_name = "sony";
		break;
	case FG_CELL_NAME_C:
		bq->cell_name = "sanyo";
		break;
	case FG_CELL_NAME_D:
		bq->cell_name = "lg";
		break;
	case FG_CELL_NAME_E:
		bq->cell_name = "panasonic";
		break;
	case FG_CELL_NAME_F:
		bq->cell_name = "atl";
		break;
	case FG_CELL_NAME_G:
		bq->cell_name = "byd";
		break;
	case FG_CELL_NAME_H:
		bq->cell_name = "tws";
		break;
	case FG_CELL_NAME_I:
		bq->cell_name = "lishen";
		break;
	case FG_CELL_NAME_J:
		bq->cell_name = "coslight";
		break;
	case FG_CELL_NAME_L:
		bq->cell_name = "lwn";
		break;
	default:
		bq->cell_name = "default";
		break;
	}

	return 0;
}

static int fg_read_rsoc(struct bq_fg_chip *bq, int *rsoc)
{
	int ret;
	u16 soc;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOC], &soc);
	if (ret < 0) {
		mca_log_info("could not read RSOC, ret = %d\n", ret);
		return ret;
	}

	*rsoc = soc;
	return 0;
}

static int fg_read_temperature(struct bq_fg_chip *bq, int *temp)
{
	int ret;
	u16 temp_now = 0;

	if (bq->fake_temp != FG_FAKE_TEMP_NONE) {
		*temp = bq->fake_temp;
		return 0;
	}

	if (bq->fg_error) {
		*temp = FG_ERROR_FAKE_TEMP;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TEMP], &temp_now);
	if (ret < 0) {
		mca_log_info("could not read temperature, ret = %d\n", ret);
		return ret;
	}

	*temp = fg_convert_bytes_to_temp(bq, (u8 *)&temp_now);
	return 0;
}

static int fg_read_original_temperature(struct bq_fg_chip *bq, int *temp)
{
	int ret;
	u16 temp_now = 0;

	if (bq->fg_error) {
		*temp = FG_ERROR_FAKE_TEMP;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_ORIGINAL_TEMP], &temp_now);
	if (ret < 0) {
		mca_log_info("could not read original temperature, ret = %d\n", ret);
		return ret;
	}

	*temp = fg_convert_bytes_to_original_temp(bq, (u8 *)&temp_now);
	return 0;
}

static int fg_set_temperature(void *data, int value)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	if (bq) {
		bq->fake_temp = value;
		mca_log_err("set fake_temp: %d\n", bq->fake_temp);
	}

	return 0;
}

static int fg_read_volt(struct bq_fg_chip *bq, int *vbat)
{
	int ret;
	u16 volt = 0;

	if (bq->fg_error)
		return 3700;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_VOLT], &volt);
	if (ret < 0) {
		mca_log_err("could not read volt ret = %d\n", ret);
		return ret;
	}

	*vbat = fg_convert_bytes_to_volt(bq, (u8 *)&volt);
	return 0;
}

static int fg_convert_bytes_to_volt(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int volt = value;

	if (volt < FG_VOLT_LOW_TH || volt > FG_VOLT_HIGH_TH) {
		mca_log_err("volt out of range: %d\n", volt);
		volt = fg->batt_volt;
	}

	if (!volt && fg->batt_volt > FG_VOLT_LOW_TH)
		volt = fg->batt_volt;

	fg->batt_volt = volt;
	mca_log_debug("volt: %d\n", fg->batt_volt);
	return fg->batt_volt;
}

#define FG_PACK_VOLT_HIGH_TH 5000
static void fg_read_packvolt(struct bq_fg_chip *bq, int *packvolt)
{
	u16 raw = 0;
	int volt;

	if (bq->fg_error)
		return;

	if (fg_read_word(bq, bq->regs[BQ_FG_REG_ORIGINAL_TEMP], &raw) < 0) {
		mca_log_err("could not read volt\n");
		return;
	}

	volt = raw;
	if (volt > FG_PACK_VOLT_HIGH_TH || bq->fg_error)
		volt = bq->batt_volt;
	if (!volt)
		volt = bq->batt_volt > 0 ? bq->batt_volt : 0;

	bq->batt_volt = volt;
	mca_log_debug("volt: %d\n", bq->batt_volt);
	*packvolt = bq->batt_volt;
}

static int fg_convert_bytes_to_curr(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 temp_curr = BYTES_TO_U16(bytes);
	int curr;

	if (temp_curr > 32768)
		curr = -1 * (temp_curr - 65536);
	else
		curr = -1 * temp_curr;

	if (curr < FG_CURR_LOW_TH || curr > FG_CURR_HIGH_TH) {
		mca_log_err("current out of range: %d\n", curr);
		curr = fg->batt_curr / 1000;
	}

	fg->batt_curr = curr * 1000;
	mca_log_debug("temp_curr: %d, curr: %d\n", temp_curr, fg->batt_curr);
	return fg->batt_curr;
}

static int fg_convert_bytes_to_temp(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int temp = value - 2730;

	if (temp < FG_TEMP_LOW_TH || temp > FG_TEMP_HIGH_TH) {
		mca_log_err("temperature out of range: %d\n", temp);
		temp = fg->batt_temp;
	}

	if (fg->fake_temp != FG_FAKE_TEMP_NONE)
		temp = fg->fake_temp;

	fg->batt_temp = temp;
	return fg->batt_temp;
}

static int fg_convert_bytes_to_original_temp(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int temp = value - 2730;

	if (temp < FG_TEMP_LOW_TH || temp > FG_TEMP_HIGH_TH) {
		mca_log_err("original temperature out of range: %d\n", temp);
		temp = fg->original_temp;
	}

	if (fg->fake_temp != FG_FAKE_TEMP_NONE)
		temp = fg->fake_temp;

	fg->original_temp = temp;
	return fg->original_temp;
}

static int fg_convert_bytes_to_rm(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int rm = value * 1000;

	if (value > FG_RM_FCC_HIGH_TH) {
		mca_log_err("rm out of range: %d\n", value);
		rm = fg->batt_rm;
	}

	if (!value && fg->batt_rm / 1000 > FG_RM_FCC_DEFAULT_TH)
		rm = fg->batt_rm;

	if(fg->fg_vendor == MPC7021) {
		rm *= 2;
	}

	fg->batt_rm = rm;
	return fg->batt_rm;
}

static int fg_convert_bytes_to_fcc(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int fcc = value * 1000;

	if (value > FG_RM_FCC_HIGH_TH) {
		mca_log_err("fcc out of range: %d\n", value);
		fcc = fg->batt_fcc;
	}

	if (!value && fg->batt_fcc > 0)
		fcc = fg->batt_fcc;
	else if (!value && !fg->batt_fcc)
		fcc = FG_RM_FCC_DEFAULT_TH * 1000;

	if(fg->fg_vendor == MPC7021) {
		fcc *= 2;
	}

	fg->batt_fcc = fcc;
	return fg->batt_fcc;
}

static int fg_convert_bytes_to_cycle_count(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);

	if (fg->fake_cycle)
		fg->batt_cycle_count = fg->fake_cycle;
	else
		fg->batt_cycle_count = value;

	return fg->batt_cycle_count;
}

static int fg_read_current(struct bq_fg_chip *bq, int *curr)
{
	int ret;
	u16 temp_curr = 0;

	if (bq->fg_error) {
		*curr = -500 * 1000;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CN], &temp_curr);
	if (ret < 0) {
		mca_log_err("could not read current, ret = %d\n", ret);
		*curr = 0;
		return ret;
	}
	*curr = fg_convert_bytes_to_curr(bq, (u8 *)&temp_curr);

	return 0;
}

static int fg_read_max_cell_volt(struct bq_fg_chip *bq, int *max_volt)
{
	int ret;
	int cell_volt_1 = 0, cell_volt_2 = 0;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	/* MPC7021 support */
	if(bq->fg_vendor != MPC7021){
		ret = fg_read_volt(bq, max_volt);
		return ret;
	}

	ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 4);
	if (ret < 0)
		return ret;

	cell_volt_1 = (t_buf[1] << 8) | t_buf[0];
	cell_volt_2 = (t_buf[3] << 8) | t_buf[2];

	mca_log_debug("cell volt = [%d %d] \n", cell_volt_1, cell_volt_2);

	*max_volt = cell_volt_1 > cell_volt_2 ? cell_volt_1 : cell_volt_2;

	return 0;
}

static int fg_read_fcc(struct bq_fg_chip *bq, int *fcc)
{
	int ret;
	u16 data;

	if (bq->regs[BQ_FG_REG_FCC] == INVALID_REG_ADDR) {
		mca_log_info("FCC command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_FCC], &data);
	if (ret < 0) {
		mca_log_info("could not read FCC ret %d\n", ret);
		return ret;
	}

	*fcc = fg_convert_bytes_to_fcc(bq, (u8 *)&data);
	return 0;
}

static int fg_get_isc_alert_level(struct bq_fg_chip *bq, int *level)
{
	int ret;
	u8 isc_alert_level;

	if (bq->fake_isc)
		return bq->fake_isc;

	if (bq->regs[NVT_FG_REG_ISC] == INVALID_REG_ADDR) {
		mca_log_info("ISC command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_ISC], &isc_alert_level);

	if (ret < 0)
		mca_log_info("could not read ISC, ret=%d\n", ret);

	*level = isc_alert_level;
	return 0;
}

static int fg_set_isc_alert_level(struct bq_fg_chip *bq, int value)
{
	int ret = 0;

	bq->fake_isc = value;
	mca_log_info("set fake isc:: %d\n", bq->fake_isc);
	return ret;
}

static int fg_get_soa_alert_level(struct bq_fg_chip *bq, int *level)
{
	int ret;
	u8 soa_alert_level;

	if (bq->regs[NVT_FG_REG_SOA_L] == INVALID_REG_ADDR) {
		mca_log_info("SOA command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_SOA_L], &soa_alert_level);
	if (ret < 0) {
		mca_log_info("could not read SOA, ret=%d\n", ret);
		return ret;
	}

	*level = soa_alert_level;
	return 0;
}

static int fg_get_current_dev(struct bq_fg_chip *bq)
{
	int ret;
	u16 curr_dev;

	if (bq->regs[NVT_FG_REG_CUR_DEV] == INVALID_REG_ADDR) {
		mca_log_info("CURR_DEV command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_CUR_DEV], &curr_dev);

	if (ret < 0)
		mca_log_info("could not read CURR_DEV, ret=%d\n", ret);

	if (curr_dev > 32768)
		curr_dev = -1 * (curr_dev - 65536);
	else
		curr_dev = -1 * curr_dev;

	return curr_dev;
}

static int fg_get_power_dev(struct bq_fg_chip *bq)
{
	int ret;
	u16 power_dev;

	if (bq->regs[NVT_FG_REG_POW_DEV] == INVALID_REG_ADDR) {
		mca_log_info("POW_DEV command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_POW_DEV], &power_dev);

	if (ret < 0)
		mca_log_info("could not read POW_DEV, ret=%d\n", ret);

	if (power_dev > 32768)
		power_dev = -1 * (power_dev - 65536);
	else
		power_dev = -1 * power_dev;

	return power_dev;
}

static int fg_get_average_current(struct bq_fg_chip *bq)
{
	int ret;
	u16 ave_curr;

	if (bq->regs[NVT_FG_AVE_CUR] == INVALID_REG_ADDR) {
		mca_log_info("AVE_CUR command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_AVE_CUR], &ave_curr);

	if (ret < 0)
		mca_log_info("could not read AVE_CUR, ret=%d\n", ret);

	if (ave_curr > 32768)
		ave_curr = -1 * (ave_curr - 65536);
	else
		ave_curr = -1 * ave_curr;

	return ave_curr;
}

static int fg_get_average_temp(struct bq_fg_chip *bq)
{
	int ret;
	u16 ave_temp;

	if (bq->regs[NVT_FG_AVE_TEMP] == INVALID_REG_ADDR) {
		mca_log_info("AVE_TEMP command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_AVE_TEMP], &ave_temp);

	if (ret < 0)
		mca_log_info("could not read AVE_TEMP, ret=%d\n", ret);

	ave_temp = ave_temp - 2730;

	return ave_temp;
}

static int fg_get_start_learning(struct bq_fg_chip *bq)
{
	int ret;
	u8 start_learning;

	if (bq->regs[NVT_FG_REG_START_LEARNING] == INVALID_REG_ADDR) {
		mca_log_info("START_LEARNING command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_START_LEARNING], &start_learning);

	if (ret < 0)
		mca_log_info("could not read START_LEARNING, ret=%d\n", ret);

	return start_learning;
}

static int fg_set_start_learning(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_START_LEARNING] == INVALID_REG_ADDR) {
		mca_log_info("START_LEARNING command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_START_LEARNING], 0x01);

	if (ret < 0)
		mca_log_info("could not write START_LEARNING, ret=%d\n", ret);

	return ret;
}

static int fg_get_stop_learning(struct bq_fg_chip *bq)
{
	int ret;
	u8 stop_learning;

	if (bq->regs[NVT_FG_REG_STOP_LEARNING] == INVALID_REG_ADDR) {
		mca_log_info("STOP_LEARNING command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_STOP_LEARNING], &stop_learning);

	if (ret < 0)
		mca_log_info("could not read STOP_LEARNING, ret=%d\n", ret);

	return stop_learning;
}

static int fg_set_stop_learning(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_STOP_LEARNING] == INVALID_REG_ADDR) {
		mca_log_info("STOP_LEARNING command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_STOP_LEARNING], 0x01);

	if (ret < 0)
		mca_log_info("could not write STOP_LEARNING, ret=%d\n", ret);

	return ret;
}

static int fg_get_actual_power(struct bq_fg_chip *bq)
{
	int ret;
	u16 act_power;

	if (bq->regs[NVT_FG_REG_ACT_POWER] == INVALID_REG_ADDR) {
		mca_log_info("ACT_POWER command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_ACT_POWER], &act_power);

	if (ret < 0)
		mca_log_info("could not read ACT_POWER, ret=%d\n", ret);

	if (act_power > 32768)
		act_power = -1 * (act_power - 65536);
	else
		act_power = -1 * act_power;

	return act_power;
}

static int fg_get_learning_power(struct bq_fg_chip *bq)
{
	int ret;
	u16 est_power;

	if (bq->regs[NVT_FG_REG_EST_POWER] == INVALID_REG_ADDR) {
		mca_log_info("EST_POWER command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_EST_POWER], &est_power);

	if (ret < 0)
		mca_log_info("could not read EST_POWER, ret=%d\n", ret);

	if (est_power > 32768)
		est_power = -1 * (est_power - 65536);
	else
		est_power = -1 * est_power;

	return est_power;
}

static int fg_set_learning_power(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 est_power;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	est_power =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_EST_POWER] == INVALID_REG_ADDR) {
		mca_log_info("EST_POWER command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_EST_POWER], est_power);

	if (ret < 0)
		mca_log_info("could not write EST_POWER, ret=%d\n", ret);

	return ret;
}

static int fg_get_learning_power_dev(struct bq_fg_chip *bq)
{
	int ret;
	u16 learning_power_dev;

	if (bq->regs[NVT_FG_REG_POWER_DEV] == INVALID_REG_ADDR) {
		mca_log_info("POWER_DEV command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_POWER_DEV], &learning_power_dev);

	if (ret < 0)
		mca_log_info("could not read POWER_DEV, ret=%d\n", ret);

	if (learning_power_dev > 32768)
		learning_power_dev = -1 * (learning_power_dev - 65536);
	else
		learning_power_dev = -1 * learning_power_dev;

	return learning_power_dev;
}

static int fg_get_learning_time_dev(struct bq_fg_chip *bq)
{
	int ret;
	u16 learning_time_dev;

	if (bq->regs[NVT_FG_REG_TIME_DEV] == INVALID_REG_ADDR) {
		mca_log_info("TIME_DEV command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_TIME_DEV], &learning_time_dev);

	if (ret < 0)
		mca_log_info("could not read TIME_DEV, ret=%d\n", ret);

	if (learning_time_dev > 32768)
		learning_time_dev = -1 * (learning_time_dev - 65536);
	else
		learning_time_dev = -1 * learning_time_dev;

	return learning_time_dev;
}

static int fg_get_constant_power(struct bq_fg_chip *bq)
{
	int ret;
	u16 constant_power;

	if (bq->regs[NVT_FG_REG_CONST_POWER] == INVALID_REG_ADDR) {
		mca_log_info("CONST_POWER command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_CONST_POWER], &constant_power);

	if (ret < 0)
		mca_log_info("could not read CONST_POWER, ret=%d\n", ret);

	if (constant_power > 32768)
		constant_power = -1 * (constant_power - 65536);
	else
		constant_power = -1 * constant_power;

	return constant_power;
}

static int fg_set_constant_power(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 constant_power;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	constant_power =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_CONST_POWER] == INVALID_REG_ADDR) {
		mca_log_info("CONST_POWER command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_CONST_POWER], constant_power);

	if (ret < 0)
		mca_log_info("could not write CONST_POWER, ret=%d\n", ret);

	return ret;
}

static int fg_get_remaining_time(struct bq_fg_chip *bq)
{
	int ret;
	u16 remaining_time;

	if (bq->regs[NVT_FG_REG_CONST_POWER_TM] == INVALID_REG_ADDR) {
		mca_log_info("POWER_TM command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_CONST_POWER_TM], &remaining_time);

	if (ret < 0)
		mca_log_info("could not read POWER_TM, ret=%d\n", ret);

	return remaining_time;
}

static int fg_get_referance_current(struct bq_fg_chip *bq)
{
	int ret;
	u16 referance_current;

	if (bq->regs[NVT_FG_REG_REF_CURRENT] == INVALID_REG_ADDR) {
		mca_log_info("REF_CURR command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_REF_CURRENT], &referance_current);

	if (ret < 0)
		mca_log_info("could not read REF_CURR, ret=%d\n", ret);

	if (referance_current > 32768)
		referance_current = -1 * (referance_current - 65536);
	else
		referance_current = -1 * referance_current;

	return referance_current;
}

static int fg_get_start_learning_b(struct bq_fg_chip *bq)
{
	int ret;
	u8 start_learning_b;

	if (bq->regs[NVT_FG_REG_START_LEARNING_B] == INVALID_REG_ADDR) {
		mca_log_info("START_LEARNING_B command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_START_LEARNING_B], &start_learning_b);

	if (ret < 0)
		mca_log_info("could not read START_LEARNING_B, ret=%d\n", ret);

	return start_learning_b;
}

static int fg_get_stop_learning_b(struct bq_fg_chip *bq)
{
	int ret;
	u8 stop_learning;

	if (bq->regs[NVT_FG_REG_STOP_LEARNING_B] == INVALID_REG_ADDR) {
		mca_log_info("STOP_LEARNING_B command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_STOP_LEARNING_B], &stop_learning);

	if (ret < 0)
		mca_log_info("could not read STOP_LEARNING_B, ret=%d\n", ret);

	return stop_learning;
}

static int fg_get_learning_power_b(struct bq_fg_chip *bq)
{
	int ret;
	u16 est_power_b;

	if (bq->regs[NVT_FG_REG_EST_POWER_B] == INVALID_REG_ADDR) {
		mca_log_info("EST_POWER_B command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_EST_POWER_B], &est_power_b);

	if (ret < 0)
		mca_log_info("could not read EST_POWER_B, ret=%d\n", ret);

	if (est_power_b > 32768)
		est_power_b = -1 * (est_power_b - 65536);
	else
		est_power_b = -1 * est_power_b;

	return est_power_b;
}

static int fg_get_actual_power_b(struct bq_fg_chip *bq)
{
	int ret;
	u16 act_power_b;

	if (bq->regs[NVT_FG_REG_ACT_POWER_B] == INVALID_REG_ADDR) {
		mca_log_info("ACT_POWER_B command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_ACT_POWER_B], &act_power_b);

	if (ret < 0)
		mca_log_info("could not read ACT_POWER_B, ret=%d\n", ret);

	if (act_power_b > 32768)
		act_power_b = -1 * (act_power_b - 65536);
	else
		act_power_b = -1 * act_power_b;

	return act_power_b;
}

static int fg_get_learning_power_dev_b(struct bq_fg_chip *bq)
{
	int ret;
	u16 learning_power_dev_b;

	if (bq->regs[NVT_FG_REG_POWER_DEV_B] == INVALID_REG_ADDR) {
		mca_log_info("POWER_DEV_B command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_POWER_DEV_B], &learning_power_dev_b);

	if (ret < 0)
		mca_log_info("could not read POWER_DEV_B, ret=%d\n", ret);

	if (learning_power_dev_b > 32768)
		learning_power_dev_b = -1 * (learning_power_dev_b - 65536);
	else
		learning_power_dev_b = -1 * learning_power_dev_b;

	return learning_power_dev_b;
}

static int fg_get_rel_soh(struct bq_fg_chip *bq, u32 *rel_soh)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_REL_SOH, t_buf, 2);
	if (ret < 0)
		return ret;

	*rel_soh =  (t_buf[1] << 8) | (t_buf[0]);
	return ret;
}

static int fg_get_rel_soh_cyclecount(struct bq_fg_chip *bq, u32 *rel_soh_cyclecount)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_REL_SOH, t_buf, 4);
	if (ret < 0)
		return ret;

	*rel_soh_cyclecount =  (t_buf[3] << 8) | (t_buf[2]);
	return ret;
}


static int fg_get_eis_soh(struct bq_fg_chip *bq, u32 *eis_soh)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_EIS_SOH, t_buf, 4);
	if (ret < 0)
		return ret;

	*eis_soh =  (t_buf[1] << 8) | (t_buf[0]);
	return ret;
}

static int fg_get_eis_soh_cyclecount(struct bq_fg_chip *bq, u32 *eis_soh_cyclecount)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_EIS_SOH, t_buf, 4);
	if (ret < 0)
		return ret;

	*eis_soh_cyclecount =  (t_buf[3] << 8) | (t_buf[2]);
	return ret;
}

static int fg_get_qmax_cyclecount(struct bq_fg_chip *bq, u32 *qmax_cyclecount)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 12);
	if (ret < 0)
		return ret;

	*qmax_cyclecount =  (t_buf[11] << 8) | (t_buf[10]);
	return ret;
}

static int fg_get_calibration_ffc_iterm(struct bq_fg_chip *bq, int *iterm)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALIBRATION_INFO_1, t_buf, 30);
	if (ret < 0)
		return ret;

	if (bq->batt_temp < 200)
		*iterm = (t_buf[1] << 8) | (t_buf[0]);
	else if (bq->batt_temp < 250)
		*iterm = (t_buf[5] << 8) | (t_buf[4]);
	else if (bq->batt_temp < 300)
		*iterm = (t_buf[9] << 8) | (t_buf[8]);
	else if (bq->batt_temp < 350)
		*iterm = (t_buf[13] << 8) | (t_buf[12]);
	else if (bq->batt_temp < 400)
		*iterm = (t_buf[17] << 8) | (t_buf[16]);
	else if (bq->batt_temp < 450)
		*iterm = (t_buf[21] << 8) | (t_buf[20]);
	else if (bq->batt_temp < 480)
		*iterm = (t_buf[25] << 8) | (t_buf[24]);
	else
		*iterm = (t_buf[29] << 8) | (t_buf[28]);

	return ret;
}

static int fg_get_calibration_charge_energy(struct bq_fg_chip *bq, int *charge_energy)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALIBRATION_INFO_2, t_buf, 32);
	if (ret < 0)
		return ret;

	if (bq->batt_temp < 200)
		*charge_energy = (t_buf[3] << 8) | (t_buf[2]);
	else if (bq->batt_temp < 250)
		*charge_energy = (t_buf[7] << 8) | (t_buf[6]);
	else if (bq->batt_temp < 300)
		*charge_energy = (t_buf[11] << 8) | (t_buf[10]);
	else if (bq->batt_temp < 350)
		*charge_energy = (t_buf[15] << 8) | (t_buf[14]);
	else if (bq->batt_temp < 400)
		*charge_energy = (t_buf[19] << 8) | (t_buf[18]);
	else if (bq->batt_temp < 450)
		*charge_energy = (t_buf[23] << 8) | (t_buf[22]);
	else if (bq->batt_temp < 480)
		*charge_energy = (t_buf[27] << 8) | (t_buf[26]);
	else
		*charge_energy = (t_buf[31] << 8) | (t_buf[30]);

	return ret;
}

static int fg_get_real_supplement_energy(struct bq_fg_chip *bq, int *supplement_energy)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALIBRATION_INFO_4, t_buf, 3);
	if (ret < 0)
		return ret;

	*supplement_energy = (t_buf[2] << 8) | (t_buf[1]);

	return ret;
}

static int fg_get_batt_use_environment(struct bq_fg_chip *bq, u8 *batt_use_environment)
{
	int ret = 0, i;
	unsigned char t_buf[64] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALC_RVALUE, t_buf, 32);
	if (ret < 0) {
		return ret;
	}

	for (i = 0; i < 32; i++)
		batt_use_environment[i] = t_buf[i];

	ret = fg_mac_read_block(bq, FG_MAC_CMD_OVER_VOL_DURATION, t_buf, 6);
	if (ret < 0) {
		return ret;
	}

	for (i = 32; i < 38; i++)
		batt_use_environment[i] = t_buf[i - 32];

	return 0;
}

static int fg_get_max_life_vol(struct bq_fg_chip *bq, u32 *max_life_vol)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 4);
	if (ret < 0)
		return ret;

	switch(bq->fg_vendor) {
	case MPC7021:
		*max_life_vol = (t_buf[1] << 8 | t_buf[0]) + (t_buf[3] << 8 | t_buf[2]);
		break;
	default:
		*max_life_vol = (t_buf[1] << 8) | (t_buf[0]);
		break;
	}

	return ret;
}

static int fg_get_min_life_vol(struct bq_fg_chip *bq, u32 *min_life_vol)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};


	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 20);
		if (ret < 0)
			return ret;
		*min_life_vol = (t_buf[5] << 8 | t_buf[4]) + (t_buf[7] << 8 | t_buf[6]);
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, 20);
		if (ret < 0)
			return ret;

		*min_life_vol = (t_buf[19] << 8) | (t_buf[18]);
		break;
	}

	return ret;
}

static int fg_get_max_life_temp(struct bq_fg_chip *bq, u32 *max_life_temp)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*max_life_temp = t_buf[16];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8);
		if (ret < 0)
			return ret;

		*max_life_temp = t_buf[6];
		break;
	}

	return ret;
}

static int fg_get_min_life_temp(struct bq_fg_chip *bq, u32 *min_life_temp)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*min_life_temp = t_buf[17];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8);
		if (ret < 0)
			return ret;

		*min_life_temp = t_buf[7];
		break;
	}

	return ret;
}

static int fg_get_over_vol_duration(struct bq_fg_chip *bq, u32 *over_vol_duration)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_OVER_VOL_DURATION, t_buf, 12);
	if (ret < 0)
		return ret;

	*over_vol_duration = (t_buf[11] << 24) | (t_buf[10] << 16) | (t_buf[9] << 8) | (t_buf[8]);
	return ret;
}

static int fg_get_referance_power(struct bq_fg_chip *bq)
{
	int ret;
	u16 referance_power;

	if (bq->regs[NVT_FG_REG_REF_POWER] == INVALID_REG_ADDR) {
		mca_log_info("REF_POWER command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_REF_POWER], &referance_power);

	if (ret < 0)
		mca_log_info("could not read REF_POWER, ret=%d\n", ret);

	if (referance_power > 32768)
		referance_power = -1 * (referance_power - 65536);
	else
		referance_power = -1 * referance_power;

	return referance_power;
}

static int fg_get_nvt_referance_power(struct bq_fg_chip *bq)
{
	int ret;
	u16 referance_power;

	if (bq->regs[NVT_FG_REG_NVT_REF_CURRENT] == INVALID_REG_ADDR) {
		mca_log_info("NVT_REF_CURR command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_NVT_REF_CURRENT], &referance_power);

	if (ret < 0)
		mca_log_info("could not read NVT_REF_CURR, ret=%d\n", ret);

	if (referance_power > 32768)
		referance_power = -1 * (referance_power - 65536);
	else
		referance_power = -1 * referance_power;

	return referance_power;
}

static int fg_set_referance_power(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 referance_power;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	referance_power =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_REF_POWER] == INVALID_REG_ADDR) {
		mca_log_info("REF_POWER command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_REF_POWER], referance_power);

	if (ret < 0)
		mca_log_info("could not write REF_POWER, ret=%d\n", ret);

	return ret;
}

static int fg_set_start_learning_b(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_START_LEARNING_B] == INVALID_REG_ADDR) {
		mca_log_info("START_LEARNING_B command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_START_LEARNING_B], 0x01);

	if (ret < 0)
		mca_log_info("could not write START_LEARNING_B, ret=%d\n", ret);

	return ret;
}

static int fg_set_stop_learning_b(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_STOP_LEARNING_B] == INVALID_REG_ADDR) {
		mca_log_info("STOP_LEARNING_B command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_STOP_LEARNING_B], 0x01);

	if (ret < 0)
		mca_log_info("could not write STOP_LEARNING_B, ret=%d\n", ret);

	return ret;
}

static int fg_set_learning_power_b(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 est_power_b;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	est_power_b =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_EST_POWER_B] == INVALID_REG_ADDR) {
		mca_log_info("EST_POWER_B command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_EST_POWER_B], est_power_b);

	if (ret < 0)
		mca_log_info("could not write EST_POWER_B, ret=%d\n", ret);

	return ret;
}

static int fg_get_over_peak_flag(struct bq_fg_chip *bq)
{
	int ret;
	u8 over_peak_flag;

	if (bq->regs[NVT_FG_REG_OVER_PEAK] == INVALID_REG_ADDR) {
		mca_log_info("OVER_PEAK command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_OVER_PEAK], &over_peak_flag);

	if (ret < 0)
		mca_log_info("could not read OVER_PEAK, ret=%d\n", ret);

	return over_peak_flag;
}

static int fg_read_dc(struct bq_fg_chip *bq, int *dc)
{
	int ret;
	u16 tmp;

	if (bq->regs[BQ_FG_REG_DC] == INVALID_REG_ADDR) {
		mca_log_info("DesignCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_DC], &tmp);
	if (ret < 0) {
		mca_log_info("could not read DC, ret=%d\n", ret);
		return -1;
	}

	*dc = tmp * 1000;
	return 0;
}

static int fg_read_qmax(struct bq_fg_chip *bq, u32 *qmax)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 10);
	if (ret < 0)
		return ret;

	*qmax =  (t_buf[9] << 8) | (t_buf[8]);

	if (*qmax == 0) {
		ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX, t_buf, 2);
		if (ret < 0)
			return ret;

		*qmax =  (t_buf[1] << 8) | (t_buf[0]);
	}

	return ret;
}

static int fg_read_rm(struct bq_fg_chip *bq, int *rm)
{
	int ret;
	u16 data;

	if (bq->regs[BQ_FG_REG_RM] == INVALID_REG_ADDR) {
		mca_log_info("RemainingCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_RM], &data);
	if (ret < 0) {
		mca_log_info("could not read DC, ret=%d\n", ret);
		return ret;
	}

	*rm = fg_convert_bytes_to_rm(bq, (u8 *)&data);
	return 0;
}

static int fg_read_soh(struct bq_fg_chip *bq, int *soh)
{
	int ret;
	u16 tmp;

	if (bq->regs[BQ_FG_REG_SOH] == INVALID_REG_ADDR) {
		mca_log_info("SOH command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOH], &tmp);
	if (ret < 0) {
		mca_log_info("could not read DC, ret=%d\n", ret);
		return ret;
	}

	*soh = tmp;
	return 0;
}

static int fg_read_fcc_soh(struct bq_fg_chip *bq, u32 *fcc_soh)
{
	int ret;
	u8 t_buf[64];

	memset(t_buf, 0, 64);
	if (bq->fg_vendor != BQ27Z561)
		return 0;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_FCC_SOH, t_buf, 2);
	if (ret < 0) {
		mca_log_info("could not read FCC_SOH, ret=%d\n", ret);
		return ret;
	}

	*fcc_soh = (t_buf[0] << 8) | t_buf[1];
	return ret;
}

static int fg_read_cyclecount(struct bq_fg_chip *bq, int *cycle_count)
{
	int ret;
	u16 cc;

	if (bq->fake_cycle) {
		*cycle_count = bq->fake_cycle;
		return 0;
	}

	if (bq->regs[BQ_FG_REG_CC] == INVALID_REG_ADDR) {
		mca_log_info("Cycle Count not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CC], &cc);
	if (ret < 0) {
		mca_log_info("could not read Cycle Count, ret=%d\n", ret);
		return ret;
	}

	*cycle_count = fg_convert_bytes_to_cycle_count(bq, (u8 *)&cc);
	return 0;
}

static int fg_set_cyclecount(struct bq_fg_chip *bq, int value)
{
	int ret = 0;

	bq->fake_cycle = value;
	mca_log_info("set fake cyclecount:: %d\n", bq->fake_cycle);
	return ret;
}

static int fg_read_manufacturing_date(struct bq_fg_chip *bq, u8 *buf)
{
	unsigned char t_buf[64] = { 0 };
	int ret, i, len;
	char manufacturing_date[8] = {'2', '0', '2', '0', '0', '0', '0', '0'};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_BATT_SN, t_buf, 32);
	if (ret < 0) {
		mca_log_info("failed to get BATT_SN\n");
		return ret;
	}

	/*'0'-'9'0x30-0x39*/
	manufacturing_date[3] = '0' + (t_buf[6] & 0xF);
	manufacturing_date[6] = '0' + (t_buf[8] & 0xF);
	manufacturing_date[7] = '0' + (t_buf[9] & 0xF);
	/*'A'0x41 10yue, 'A'0x42 'A'0x43*/
	switch (t_buf[7]) {
	case 0x41:
		manufacturing_date[4] = '1';
		manufacturing_date[5] = '0';
		break;
	case 0x42:
		manufacturing_date[4] = '1';
		manufacturing_date[5] = '1';
		break;
	case 0x43:
		manufacturing_date[4] = '1';
		manufacturing_date[5] = '2';
		break;
	default:
		manufacturing_date[4] = '0';
		manufacturing_date[5] = '0' + (t_buf[7] & 0xF);
		break;
	}
	len = ARRAY_SIZE(manufacturing_date);

	mca_log_err("read manufacturing_date=%c,%c,%c,%c,%c,len=%d\n",
	manufacturing_date[3], manufacturing_date[4], manufacturing_date[5],
	manufacturing_date[6], manufacturing_date[7], len);

	for (i = 0; i < len; i++)
		buf[i] = manufacturing_date[i];

	return 0;
}

static int fg_read_first_usage_date(struct bq_fg_chip *bq, u8 *buf)
{
	unsigned char t_buf[64] = { 0 };
	int ret, i, len;
	char first_usage_date[9] = {'0', '0', '0', '0', '0', '0', '0', '0', '\0'};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, t_buf, 32);
	if (ret < 0) {// read error, show 9999999
		memcpy(buf,"99999999",8);
		mca_log_info("failed to get first_usage_date\n");
		return ret;
	}
	/*t_buf[11] year0x00-0x09,t_buf[12] month 0x01-0x0C,t_buf[13] day */
	first_usage_date[0] = '2';
	first_usage_date[1] = '0';
	first_usage_date[2] = '0' + t_buf[11] / 10;
	first_usage_date[3] = '0' + t_buf[11] % 10;
	first_usage_date[4] = '0' + t_buf[12] / 10;
	first_usage_date[5] = '0' + t_buf[12] % 10;
	first_usage_date[6] = '0' + t_buf[13] / 10;
	first_usage_date[7] = '0' + t_buf[13] % 10;

	if (!strncmp(&first_usage_date[2], "000000", 6)) {
		memcpy(first_usage_date, "00000000", 8);
		mca_log_info("usage_date init %s\n", first_usage_date);
	} else {
		if (bq->support_version_compatible) {
			if (!strncmp(first_usage_date, "2004", 4)) {
				first_usage_date[2] = '2';
				mca_log_info("11-11 version compatiable\n");
			} else if (!strncmp(first_usage_date, "2052", 4)) {
				first_usage_date[2] = '2';
				first_usage_date[3] = '4';
				first_usage_date[4] = '0';
				first_usage_date[5] = '9';
				first_usage_date[6] = '0';
				first_usage_date[7] = '1';
				mca_log_info("09-13 version compatiable\n");
			}
		}
		for (i = 0; i < 8; i++) {
			if (first_usage_date[i] < '0' ||
			    first_usage_date[i] > '9') {
				memcpy(buf, "99999999", 8);
				mca_log_info("usage data has invalid char [%s]\n",
					     first_usage_date);
				return 0;
			}
		}
		mca_log_err("read successs = %s\n", first_usage_date);
	}

	len = strlen(first_usage_date);
	for (i = 0; i < len; i++)
		buf[i] = first_usage_date[i];

	return 0;
}

static void fg_write_fake_first_usage_date(struct bq_fg_chip *bq, const char *buf, size_t size)
{
	int ret, len;
	unsigned char data[32] = { 0 };
	char *tmp_buf = NULL;

	tmp_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!tmp_buf)
		return;

	len = strlen(tmp_buf);

	strscpy(tmp_buf, buf, size + 1);
	mca_log_info("write first_usage_date=%s,len=%d\n", tmp_buf,len);

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
	if (ret < 0) {
		mca_log_info("failed to get first_usage\n");
		return;
	}

	/*example,20241220,2024(0x34)1(0x31)2(0x32)2(0x32)0(0x30)*/
	data[11] = (tmp_buf[3] & 0xF);
	data[13] = (tmp_buf[6] & 0xF) * 10 + (tmp_buf[7] & 0xF);

	if (tmp_buf[4] == 0x31)
		data[12] = 0x0A;
	else
		data[12] = 0x00;

	data[12] = data[12] + (tmp_buf[5] & 0xF);

	if (fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32))
		mca_log_err("write first_usage_date failed\n");
}

static void fg_write_first_usage_date(struct bq_fg_chip *bq, const char *buf, size_t size)
{
	int ret, len;
	unsigned char data[32] = { 0 };
	unsigned char data_temp[32] = { 0 };
	char *tmp_buf = NULL;
	int retry = 0;
	size_t i, j;

	tmp_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!tmp_buf)
		return;

	for (i = 0, j = 0; i < size && buf[i]; i++) {
		if (!isspace(buf[i]))
			tmp_buf[j++] = buf[i];
	}
	tmp_buf[j] = '\0';
	len = strnlen(tmp_buf, size);
	mca_log_info("write first_usage_date=%s,len=%d\n", tmp_buf, len);

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
	if (ret < 0) {
		mca_log_info("failed to get first_usage\n");
		return;
	}

	if ((data[11] == 0x00 && data[12] == 0x00 && data[13] == 0x00) ||
	(data[11] == 0xFF && data[12] == 0xFF && data[13] == 0xFF)) {
		mca_log_err("first_usage_date invalid, write\n");
	} else {
		mca_log_err("first_usage_date valid,do not write\n");
		return;
	}

	/*example,20241220,2024(0x34)1(0x31)2(0x32)2(0x32)0(0x30)*/
	data[11] = (tmp_buf[3] & 0xF);
	data[13] = (tmp_buf[6] & 0xF) * 10 + (tmp_buf[7] & 0xF);

	if (tmp_buf[4] == 0x31)
		data[12] = 0x0A;
	else
		data[12] = 0x00;

	data[12] = data[12] + (tmp_buf[5] & 0xF);

	if (fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32))
		mca_log_err("write first_usage_date failed\n");

	while (retry <= 2) {
		msleep(100);
		ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data_temp, 32);
		retry++;
		if (ret < 0) {
			mca_log_info("failed to read first_usage_date\n");
			continue;
		}
		if (data[11] == data_temp[11] &&
			data[12] == data_temp[12] &&
			data[13] == data_temp[13]) {
			mca_log_err("write first_usage_date success, retry: %d\n", retry);
			break;
		} else {
			mca_log_err("write again first_usage_date, retry: %d\n", retry);
			fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
		}
	}
}

static int fg_get_one_first_usage_date(void *data, u8 *buf)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	return fg_read_first_usage_date(bq, buf);
}

static int fg_get_one_manufacturing_date(void *data, u8 *buf)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	return fg_read_manufacturing_date(bq, buf);
}

static int fg_set_one_first_usage_date(void *data, const char *date)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	fg_write_first_usage_date(bq, date, strlen(date));
	return 0;
}

static int fg_read_tte(struct bq_fg_chip *bq, int *tte)
{
	int ret;
	u16 temp;

	if (bq->regs[BQ_FG_REG_TTE] == INVALID_REG_ADDR) {
		mca_log_info("Time To Empty not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TTE], &temp);
	if (ret < 0) {
		mca_log_info("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	*tte = temp;
	return 0;
}

static int fg_read_charging_voltage(struct bq_fg_chip *bq, int *volt)
{
	int ret;
	u16 cv;

	if (bq->regs[BQ_FG_REG_CHG_VOL] == INVALID_REG_ADDR) {
		mca_log_info(" not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CHG_VOL], &cv);
	if (ret < 0) {
		mca_log_info("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	*volt = cv;
	return 0;
}

static int fg_get_temp_max(struct bq_fg_chip *bq, int *fc)
{
	char data_lifetime[32];
	int ret = 0;
	int val = 0;
	int reg_addr = 0, byte_pos = 0;

	if(bq->fg_vendor == MPC7021) {
		reg_addr = FG_MAC_CMD_LIFETIME2;
		byte_pos = 16;
	} else {
		reg_addr = FG_MAC_CMD_LIFETIME1;
		byte_pos = 6;
	}

	memset(data_lifetime, 0, sizeof(data_lifetime));
	ret = fg_mac_read_block(bq, reg_addr, data_lifetime, sizeof(data_lifetime));

	if (ret < 0)
		mca_log_info("failed to get FG_MAC_CMD_LIFETIME1\n");
	val = data_lifetime[byte_pos];

	if (val < 20 || val > 60) {
		ret = fg_mac_read_block(bq, reg_addr, data_lifetime, sizeof(data_lifetime));
		if (ret < 0)
			mca_log_info("failed to get FG_MAC_CMD_LIFETIME\n");
		val = data_lifetime[byte_pos];
	}
	mca_log_info("fg get temperature max is: %d\n", val);

	*fc = val;
	return 0;
}

static int fg_read_ttf(struct bq_fg_chip *bq, int *ttf)
{
	int ret;
	u16 temp;

	if (bq->regs[BQ_FG_REG_TTE] == INVALID_REG_ADDR) {
		mca_log_info("Time To Empty not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TTF], &temp);
	if (ret < 0) {
		mca_log_info("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	*ttf = temp;
	return 0;
}

static int StringToHex(char *str, unsigned char *out, unsigned int *outlen)
{
	char *p = str;
	char high = 0, low = 0;
	int tmplen = strlen(p), cnt = 0;

	if (!p)
		return 0;

	tmplen = strlen(p);
	while (cnt < (tmplen / 2)) {
		high = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;
		low = (*(++ p) > '9' && ((*p <= 'F') || (*p <= 'f'))) ? *(p) - 48 - 7 : *(p) - 48;
		out[cnt] = ((high & 0x0f) << 4 | (low & 0x0f));
		p++;
		cnt++;
	}
	if (tmplen % 2 != 0)
		out[cnt] = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;

	if (!outlen)
		*outlen = tmplen / 2 + tmplen % 2;

	return tmplen / 2 + tmplen % 2;
}

static int fg_sha256_auth(struct bq_fg_chip *bq, u8 *rand_num, int length)
{
	int ret;
	u8 cksum_calc;
	u8 t_buf[36];
	/*
	 * 1. The host writes 0x00 to 0x3E.
	 * 2. The host writes 0x00 to 0x3F.
	 * 3. Write the random challenge should be written in a 32-byte block to address 0x40-0x5F.
	 * 4. Write the checksum (2’s complement sum of (1), (2), and (3)) to address 0x60.
	 * 5. Write the length 0x24 to address 0x61.
	 * 6. Write all 36byte data to 0X3E.
	 * 7. Wait for at least 400ms to get digest.
	*/
	t_buf[0] = 0x00;
	t_buf[1] = 0x00;

	if (length > 32)
		return  0;

	memcpy(t_buf + 2, rand_num, length);
	cksum_calc = checksum(rand_num, length);
	t_buf[2 + length] = cksum_calc;
	t_buf[2 + length + 1] = 0x24;
	atomic_set(&bq->digest_in_process, 1); /* 1: set flag */
	ret = fg_write_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, length + 4);
	if (ret < 0) {
		mca_log_err("batt_auth write BQ_FG_REG_ALT_MAC fail\n");
		atomic_set(&bq->digest_in_process, 0); /* 0: clear flag */
		return ret;
	}
	msleep(400);

	ret = fg_read_block(bq, bq->regs[BQ_FG_REG_MAC_DATA], bq->digest, length);
	if (ret < 0) {
		mca_log_err("batt_auth read verify_digest fail\n");
		atomic_set(&bq->digest_in_process, 0); /* 0: clear flag */
		return ret;
	}

	atomic_set(&bq->digest_in_process, 0); /* 0: clear flag */

	return 0;
}

static int get_verify_digest(struct bq_fg_chip *bq, char *buf)
{
	u8 digest_buf[4];
	int i;
	int len;

	if (!bq)
		return 0;

	for (i = 0; i < BATTERY_DIGEST_LEN; i++) {
		memset(digest_buf, 0, sizeof(digest_buf));
		snprintf(digest_buf, sizeof(digest_buf) - 1, "%02x", bq->digest[i]);
		strlcat(buf, digest_buf, BATTERY_DIGEST_LEN * 2 + 1);
	}
	len = strlen(buf);
	buf[len] = '\0';

	mca_log_info("buf = %s\n", buf);
	return strlen(buf) + 1;
}

static int set_verify_digest(struct bq_fg_chip *bq, u8 *rand_num)
{
	int i;
	u8 random[BATTERY_DIGEST_LEN] = {0};
	char kbuf[BATTERY_RANDOM_LEN + 1] = {0};

	if (!bq)
		return 0;

	strscpy(kbuf, rand_num, BATTERY_RANDOM_LEN + 1);
	StringToHex(kbuf, random, &i);
	fg_sha256_auth(bq, random, BATTERY_DIGEST_LEN);

	return 0;
}

static void fg_update_lifetime_data(struct bq_fg_chip *bq)
{
	int ret;
	u8 t_buf[40] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 32);
	if (ret < 0)
		return;

	switch(bq->fg_vendor) {
	case MPC7021:
		bq->cell1_max = (t_buf[1] << 8 | t_buf[0]) + (t_buf[3] << 8 | t_buf[2]);
		bq->max_charge_current = (t_buf[9] << 8) | t_buf[8];
		bq->max_discharge_current = (signed short int)((t_buf[11] << 8) | t_buf[10]);
		bq->total_fw_runtime = (t_buf[15] << 24) | (t_buf[14] << 16) | (t_buf[13] << 8) | t_buf[12];
		bq->time_spent_in_lt = (t_buf[23] << 24) | (t_buf[22] << 16) | (t_buf[21] << 8) | t_buf[20];
		break;
	default:
		bq->cell1_max = (t_buf[1] << 8) | t_buf[0];
		bq->max_charge_current = (t_buf[3] << 8) | t_buf[2];
		bq->max_discharge_current = (signed short int)((t_buf[5] << 8) | t_buf[4]);

		bq->max_temp_cell = t_buf[6];
		bq->min_temp_cell = t_buf[7];
		break;
	}

	memset(t_buf, 0, sizeof(t_buf));

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 32);
		if (ret < 0)
			return;
		bq->max_temp_cell = t_buf[16];
		bq->min_temp_cell = t_buf[17];
		bq->time_spent_in_ht = (t_buf[7] << 24) | (t_buf[6] << 16) | (t_buf[5] << 8) | t_buf[4];
		bq->time_spent_in_ot = (t_buf[11] << 24) | (t_buf[10] << 16) | (t_buf[9] << 8) | t_buf[8];
		break;
	default:
		break;
	}

	memset(t_buf, 0, sizeof(t_buf));

	switch(bq->fg_vendor) {
	case MPC7021:
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, 32);
		if (ret < 0)
			return;

		bq->total_fw_runtime = (t_buf[1] << 8) | t_buf[0];
		bq->time_spent_in_lt = (t_buf[5] << 8) | t_buf[4];
		bq->time_spent_in_ht = (t_buf[13] << 8) | t_buf[12];
		bq->time_spent_in_ot = (t_buf[15] << 8) | t_buf[14];
		break;
	}
}

static void fg_update_aged_flag(struct bq_fg_chip *bq)
{
	int cc = 0;

	fg_read_cyclecount(bq, &cc);

	if (cc > bq->cycle_th) {
		bq->soh_th -= 1;
		bq->cycle_th += bq->cycle_step;
		bq->aged_flag = 0;
	}

	mca_log_info("ui_soh %d soh_th %d\n", bq->ui_soh, bq->soh_th);
	if (bq->ui_soh < bq->soh_th)
		bq->aged_flag = 1;
}

static void fg_update_data_work(void *data)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	int chip_ok = 0;

	fg_get_chip_ok(bq, &chip_ok);
	if (!chip_ok)
		return;

	fg_update_aged_flag(bq);
	fg_update_lifetime_data(bq);
	fg_get_gague_mode(bq);
	if (bq->update_fw_status == FG_FW_INIT)
		fg_update_record_voltage_level(bq);

}

static void fg_set_force_report_full(void *data)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	u8 t_buf[2] = {0};
	int ret = 0;

	t_buf[0] = 0x31;
	t_buf[1] = 0x00;
	ret = fg_write_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, 2);
	if (ret < 0) {
		mca_log_err("set force_report_full fail\n");
		return;
	}
	mca_log_info("set force_report_full success\n");
}

static int fg_get_chip_ok(struct bq_fg_chip *bq, int *ok)
{
	u16 flags;
	int ret = 0, times = 0;

	while (times < FG_ERROR_CHECK_MAX_TIMES) {
		ret = fg_read_word(bq, bq->regs[BQ_FG_REG_BATT_STATUS], &flags);
		if (ret < 0) {
			mca_log_info("i2c read fail, retry %d\n", times);
			bq->fg_error = true;
			times++;
		} else {
			bq->fg_error = false;
			break;
		}
	}

	*ok =  !bq->fg_error;
	return 0;
}

static int fg_get_raw_soc(struct bq_fg_chip *bq, int *raw_soc)
{
	int rm, fcc;
	int ret;

	ret = fg_read_rm(bq, &rm);
	ret |= fg_read_fcc(bq, &fcc);
	if (ret)
		return -1;

	rm = rm / 1000;
	fcc = fcc / 1000;

	if (fcc <= 0)
		return -ERANGE;
	*raw_soc = div_s64((s64)rm * 10000, fcc);
	return 0;
}

static int fg_read_tsim(struct bq_fg_chip *bq, u32 *tsim)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 18);
	if (ret < 0)
		return ret;

	if (bq->fg_vendor == BQ27Z561)
		*tsim = (t_buf[13] << 8) | t_buf[12];
	else
		*tsim = (t_buf[17] << 8) | t_buf[16];

	return ret;
}

static int fg_read_tambient(struct bq_fg_chip *bq, u32 *tambient)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 16);
	if (ret < 0)
		return ret;

	if (bq->fg_vendor == BQ27Z561)
		*tambient = (t_buf[15] << 8) | (t_buf[14]);
	else
		*tambient = (t_buf[9] << 8) | (t_buf[8]);

	return ret;
}

static int fg_read_tremq(struct bq_fg_chip *bq, u32 *tremq)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 2);
	if (ret < 0)
		return ret;

	*tremq = (t_buf[1] << 8) | (t_buf[0]);
	return ret;
}

static int fg_read_tfullchgq(struct bq_fg_chip *bq, u32 *tfullchgq)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 4);
	if (ret < 0)
		return ret;

	*tfullchgq = (t_buf[3] << 8) | (t_buf[2]);
	return ret;
}

static int fg_read_avercurrent(struct bq_fg_chip *bq, int *avercurrent)
{
	int ret;
	u16 curr = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_AVER_CURRENT], &curr);
	if (ret < 0)
		return ret;

	*avercurrent = curr > 32768 ? (-1 * (curr - 65536) * 1000) : (-1 * curr * 1000);
	return ret;
}

/**ti fg unseal procee*/
static int fg_unseal_send_key_for_ti(struct bq_fg_chip *bq)
{
	int value = 0;
	int ret = 0;
	u8 cmd[2] = {0x2e, 0x00};

	value = (bq->key[2] << 8) | bq->key[3];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
		mca_log_err(" step 1 first word write fail");
		return ret;
	}

	msleep(4000);

	ret |= fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
		mca_log_err("step 2 first word write fail");
		return ret;
	}

	usleep_range(5000, 5100);

	value = (bq->key[0] << 8) | bq->key[1];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
		mca_log_err(" step 3 second word write fail");
		return ret;
	}

	ret |= fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
		mca_log_err("step 4 second word write fail");
		return ret;
	}

	msleep(200);

	mca_log_err("fg sunseal success");

	ret |= fg_write_block(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], cmd, 2);
	if (ret < 0) {
		mca_log_err("step 5 data flush failed,");
		return ret;
	}
	return ret;
}

static int fg_unseal_send_key_for_nvt(struct bq_fg_chip *bq)
{
	int value = 0;
	int ret = 0;

	value = (bq->key[1] << 8) | bq->key[0];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
		mca_log_err(" step 1 first word write fail");
		return ret;
	}

	msleep(300);

	value = (bq->key[3] << 8) | bq->key[2];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
		mca_log_err(" step 2 second word write fail");
		return ret;
	}

	msleep(300);
	mca_log_err("fg unseal success");
	return ret;
}


#define UNSEAL_COUNT_MAX 3
static int fg_set_seal(struct bq_fg_chip *bq, int value)
{
	int ret;
	int seal_status = 0;
	int count = 0;

	if (value) {
		ret = fg_get_seal(bq, &seal_status);
		if (seal_status == SEAL_STATE_FA || seal_status == SEAL_STATE_UNSEALED) {
			mca_log_info("FG is unsealed");
			return 0;
		}
		while (count++ < UNSEAL_COUNT_MAX) {
			if (bq->fg_vendor == NFG1000A || bq->fg_vendor == NFG1000B ||
				bq->fg_vendor ==  MPC8011B || bq->fg_vendor == MPC7012) {
				ret = fg_unseal_send_key_for_nvt(bq);
			} else if (bq->fg_vendor == BQ27Z561) {
				ret = fg_unseal_send_key_for_ti(bq);
			} else {
				mca_log_info("unseal don't support for venddor %d\n", bq->fg_vendor);
				break;
			}
			ret |= fg_get_seal(bq, &seal_status);
			if (seal_status == SEAL_STATE_FA || seal_status == SEAL_STATE_UNSEALED) {
				mca_log_info("FG is unsealed");
				break;
			}
			msleep(100);
		}
	} else {
		if (seal_status == SEAL_STATE_SEALED) {
			mca_log_info("FG is sealed");
			return 0;
		}
		while (count++ < UNSEAL_COUNT_MAX) {
			ret = fg_write_word(bq, bq->regs[BQ_FG_REG_ALT_MAC], FG_MAC_CMD_SEAL);
			if (ret < 0)
				mca_log_err("Failed to send seal command");
			ret = fg_get_seal(bq, &seal_status);
			if (seal_status == SEAL_STATE_SEALED) {
				mca_log_info("FG is sealed");
				break;
			}
			msleep(100);
		}
	}


	return ret;
}

#define SEAL_STATUS_MASK 0x03
static int fg_get_seal(struct bq_fg_chip *bq, int *value)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = {0};

	ret = fg_mac_read_block(bq, FG_MCA_CMD_SEAL_STATE, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;

	*value = t_buf[1] & SEAL_STATUS_MASK;

	return ret;

}

static int fg_read_df_check(struct bq_fg_chip *bq, int *value)
{
	int ret;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	ret = fg_mac_read_block(bq, FG_MCA_CMD_DF, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;
	*value = t_buf[1] << 8 | t_buf[0];

	return ret;
}

static void fg_get_ui_soh(void *data, int *ui_soh)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	*ui_soh = info->ui_soh;
}

static int fg_read_pack_vendor(struct bq_fg_chip *bq)
{
	u8 t_buf[4];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 4);
	if (ret < 0) {
		mca_log_info("failed to get MANE NAME\n");
		return 0;
	}
	mca_log_info("%s\n", t_buf);

	switch (t_buf[2]) {
	case 'B':
		bq->pack_vendor = PACK_SUPPLIER_BYD;
		break;
	case 'C':
		bq->pack_vendor = PACK_SUPPLIER_COSLIGHT;
		break;
	case 'S':
		bq->pack_vendor = PACK_SUPPLIER_SUNWODA;
		break;
	case 'N':
		bq->pack_vendor = PACK_SUPPLIER_NVT;
		break;
	case 'U':
		bq->pack_vendor = PACK_SUPPLIER_SCUD;
		break;
	case 'T':
		bq->pack_vendor = PACK_SUPPLIER_TWS;
		break;
	case 'I':
		bq->pack_vendor = PACK_SUPPLIER_LISHEN;
		break;
	case 'K':
		bq->pack_vendor = PACK_SUPPLIER_DESAY;
		break;
	default:
		bq->pack_vendor = PACK_SUPPLIER_NVT;
		break;
	}
	return 0;
}

static int fg_read_vendor(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH];

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, t_buf, 3);
	if (ret < 0)
		return ret;

	mca_log_info("fg_vendor = %d", t_buf[2]);

	switch (t_buf[2]) {
	case '4':
		bq->fg_vendor = NFG1000B;
		break;
	case '5':
		bq->fg_vendor = NFG1000A;
		break;
	case '6':
		bq->fg_vendor = MPC8011B;
		break;
	case '7':
		bq->fg_vendor = MPC7012;
		break;
	case 'C':
		bq->fg_vendor = BQ27Z561;
		break;
	case '8':
		bq->fg_vendor = MPC7021;
		break;
	default:
		bq->fg_vendor = MPC8011B;
		break;
	}
	return ret;
}

#define FG_FW_R0	0x0001
#define FG_FW_R1	0x0102
static int fg_read_fw_version(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[4] = {0};
	int value = 0;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_FW_VER, t_buf, 4);
	if (ret < 0)
		return ret;

	value = (t_buf[2] << 8) | (t_buf[3]);
	if (value == FG_FW_R0)
		bq->fw_ver = FW_R0;
	else if (value == FG_FW_R1)
		bq->fw_ver = FW_R1;
	else
		bq->fw_ver = FW_DEFAULT;

	return ret;
}

static int fg_read_eeprom_version(struct bq_fg_chip *bq)
{
	u8 t_buf[4];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 4);
	if (ret < 0) {
		mca_log_info("failed to get MANE NAME\n");
		return 0;
	}
	mca_log_info("%c", t_buf[3]);

	bq->eeprom_version = t_buf[3];

	return 0;
}

static int fg_read_max_temp_occur_time(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*value = (t_buf[15] << 24) | (t_buf[14] << 16) | (t_buf[13] << 8) | t_buf[12];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = t_buf[20] | (t_buf[21] << 8) | (t_buf[22] << 16) | (t_buf[23] << 24);
		break;
	}

	mca_log_info("%d", *value);
	return ret;
}

static int fg_read_run_time(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;
	*value = t_buf[20] | (t_buf[21] << 8) | (t_buf[22] << 16) | (t_buf[23] << 24);
	mca_log_info("%d", *value);
	return ret;
}

static int fg_read_max_temp_time(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	int max_temp_time = 0;
	int run_time = 0;

	ret = fg_read_max_temp_occur_time(bq, &max_temp_time);
	ret |= fg_read_run_time(bq, &run_time);

	*value = run_time > max_temp_time ? ((run_time - max_temp_time) / 60) : 0;
	return ret;
}

static int fg_read_total_fw_runtime(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_FW_RUNTIME, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;
	*value = t_buf[8] | (t_buf[9] << 8);
	mca_log_info("%d", *value);
	return ret;
}

static int fg_get_temp_min(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[BLOCK_LENGTH_MAX] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*value = t_buf[17];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8);
		if (ret < 0)
			return ret;

		*value = t_buf[7];
		break;
	}

	return ret;
}

static int fg_read_time_ht(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	switch (bq->fg_vendor) {
	case BQ27Z561:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[19] << 8) | (t_buf[18])) * 2;
		break;
	case NFG1000A:
	case NFG1000B:
	case MPC8011B:
	case MPC7012:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[13] << 8) | (t_buf[12]));
		break;
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = (t_buf[7] << 24) | (t_buf[6] << 16) | (t_buf[5] << 8) | t_buf[4];
		break;
	default:
		break;
	}
	return ret;
}

static int fg_read_time_ot(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	switch (bq->fg_vendor) {
	case BQ27Z561:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[21] << 8) | (t_buf[20])) * 2;
		break;
	case NFG1000A:
	case NFG1000B:
	case MPC8011B:
	case MPC7012:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[15] << 8) | (t_buf[14]));
		break;
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;

		*value = (t_buf[11] << 24) | (t_buf[10] << 16) | (t_buf[9] << 8) | t_buf[8];
		break;
	default:
		break;
	}
	return ret;
}

static int fg_get_soc_decimal_rate(struct bq_fg_chip *bq, int *rate)
{
	int soc, i;
	int ret;

	if (bq->dec_rate_len <= 0)
		return 0;

	ret = fg_read_rsoc(bq, &soc);
	if (ret)
		return -1;

	for (i = 0; i < bq->dec_rate_len; i += 2) {
		if (soc < bq->dec_rate_seq[i]) {
			*rate = bq->dec_rate_seq[i - 1];
			return 0;
		}
	}

	*rate = bq->dec_rate_seq[bq->dec_rate_len - 1];
	return 0;
}

static int fg_get_soc_decimal(struct bq_fg_chip *bq, int *soc_decimal)
{
	int rsoc, raw_soc;

	if (!bq)
		return 0;

	fg_read_rsoc(bq, &rsoc);
	fg_get_raw_soc(bq, &raw_soc);

	*soc_decimal = raw_soc % 100;
	return 0;
}

static int fg_read_one_probe_ok(void *data, bool *ok)
{
	*ok = 1;
	return 0;
}

#define TERM_VOLTAGE_MIN 3000
#define TERM_VOLTAGE_MAX 3400
static int fg_set_cutoff_voltage(struct bq_fg_chip *bq, int value)
{
	int ret = 0;
	u8 data[2] = {0xb8, 0x0b};

	if (value < TERM_VOLTAGE_MIN && value > TERM_VOLTAGE_MAX) {
		mca_log_info("term_voltage over range\n");
		return ret;
	}

	data[0] = (value & 0xFF);
	data[1] = (value >> 8) & 0xFF;
	mca_log_info("data[0] = %d, data[1] = %d, value = %d", data[0], data[1], value);
	ret = fg_mac_write_block(bq, FG_MAC_CMD_CUTOFF_VOLTAGE, data, 2);
	if (ret < 0) {
		mca_log_err("could not write term_voltage = %d\n", ret);
		return ret;
	}
	return ret;
}

static int fg_get_cutoff_voltage(struct bq_fg_chip *bq, int *volt)
{
	u8 data[2] = {0};
	int ret;
	int value = 3000;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CUTOFF_VOLTAGE, data, 2);
	if (ret < 0) {
		mca_log_err("could not read cutoff_voltage = %d\n", ret);
		return ret;
	}
	value = (data[1] << 8) | data[0];

	*volt = value;
	return 0;
}

#define THE_10_BYTE 0xB2
#define THE_11_BYTE 0x0C
#define THE_12_BYTE 0x66
#define THE_13_BYTE 0x0D
#define VERSION_NUMBER 0x42
#define DF_CHECK_FIRST 0X5f
#define DF_CHECK_SECOND 0X80
#define MAX_RETRY_FOR_UPDATE_FW 0x03
static int fg_update_record_voltage_level(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[36] = {0};
	int seal_status = 0;
	int count = 0;
	int address = bq->start_byte_address;
	bool check_status = 0;

	if (!bq->support_voltage_record_level || bq->fg_vendor != MPC8011B) {
		mca_log_err("can't support ota \n");
		bq->update_fw_status = FG_FW_INVALID;
		return -1;
	}

	/*First if updated*/
	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 6);
	if (t_buf[3] >= bq->version_number) {
		mca_log_info("it has been upteded version_number[0x%x][0x%x]\n", t_buf[3], bq->version_number);
		bq->update_fw_status = FG_FW_SUCCESS;
		return 0;
	}
	/*Second Update Fw*/
	ret = fg_set_seal(bq, true);
	ret |= fg_get_seal(bq, &seal_status);
	if (seal_status != SEAL_STATE_UNSEALED && seal_status != SEAL_STATE_FA) {
		mca_log_info("Fg Unseal Failed\n");
		bq->update_fw_status = FG_FW_ERROR;
		return ret;
	}

	/*Modify Vcutoff*/
	while (count++ < MAX_RETRY_FOR_UPDATE_FW) {
		memset(t_buf, 0, sizeof(t_buf));
		check_status = false;
		ret = fg_mac_read_block(bq, FG_MAC_CMD_RECORD_VOLTAGE_LEVEL, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not read record_voltage_level, ret=%d\n", ret);
			msleep(100);
			continue;
		}
		for (int i = 0; i < bq->byte_length; i++) {
			t_buf[address + i] = bq->record_voltage[i];
			mca_log_info("Reg:t_buf[%d]=[0x%x]\n", address + i, t_buf[address + i]);
		}

		ret = fg_mac_write_block(bq, FG_MAC_CMD_RECORD_VOLTAGE_LEVEL, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not write record_voltage_level, ret=%d\n", ret);
			msleep(100);
			continue;
		}
		msleep(100);
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_RECORD_VOLTAGE_LEVEL, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not read record_voltage_level, ret=%d\n", ret);
			msleep(100);
			continue;
		}

		for (int i = 0; i < bq->byte_length; i++) {
			if (t_buf[address + i] != bq->record_voltage[i]) {
				check_status = true;
				break;
			}
			mca_log_info("Update:t_buf[%d]=[0x%x]\n", address + i, t_buf[address + i]);
		}

		if (check_status) {
			mca_log_err("Modify record_voltage_level Error\n");
			msleep(100);
			continue;
		} else {
			mca_log_info("Modify record_voltage_level Suceess\n");
			break;
		}
	}

	if (count >= MAX_RETRY_FOR_UPDATE_FW) {
		bq->update_fw_status = FG_FW_ERROR;
		goto error;
	}

	count = 0;
	/*Modify Version Number*/
	while (count++ < MAX_RETRY_FOR_UPDATE_FW) {
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_UPDATE_VERSION, t_buf, 32);
		t_buf[3] = bq->version_number;
		ret = fg_mac_write_block(bq, FG_MAC_CMD_UPDATE_VERSION, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not write update_version, ret=%d\n", ret);
			msleep(100);
			continue;
		}
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_UPDATE_VERSION, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not read update_version, ret=%d\n", ret);
			msleep(100);
			continue;
		}

		if (t_buf[3] != bq->version_number) {
			mca_log_err("Modify update_version Error\n");
			msleep(100);
			continue;
		} else {
			mca_log_info("Modify update_version Sucess\n");
			break;
		}
	}

	if (count >= MAX_RETRY_FOR_UPDATE_FW) {
		bq->update_fw_status = FG_FW_ERROR;
		goto error;
	}

	bq->update_fw_status = FG_FW_SUCCESS;
error:
	fg_set_seal(bq, false);
	return ret;
}

static int fg_clear_count_data(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[4] = {0xD3, 0xC2, 0xB1, 0xA0};

	ret = fg_mac_write_block(bq, FG_MAC_CMD_CLEAR_COUNT_DATA, t_buf, 4);
	if (ret < 0) {
		mca_log_err("could not write clear_cout_data = %d\n", ret);
		return ret;
	}

	return 0;
}


static const char *fg_get_device_name(struct bq_fg_chip *bq)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	char data[6] = { 0 };
	int ret;

	if (strcmp(bq->fake_device_name, "") != 0)
		return bq->fake_device_name;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_DEVICE_NAME, data, 5);
	if (ret < 0) {
		mca_log_err("could not read device_name, ret=%d\n", ret);
		return bq->device_name;
	}
	// O2@BP54#    XM34@BMX0#
	memset(bq->device_name, 0, sizeof(bq->device_name));
	memcpy(bq->device_name, data, 5);
	mca_log_info("0x004A: %s, model_name: %s, hwid: %u\n", data,
		     bq->device_name, hwid ? hwid->hwid_value : 0);

	return bq->device_name;
}

static int fg_get_adapt_power(struct bq_fg_chip *bq, int *adapt_power)
{
	int ret = 0;
	u8 t_buf[5];
	static char ch = '\0';

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, t_buf, 5);
	if (ret < 0) {
		mca_log_err("failed to get adapt power = %d\n", ret);
		return ret;
	}
	ch = t_buf[4];

	switch (ch) {
	case '0':
		bq->adapt_power = 10;
		break;
	case '1':
		bq->adapt_power = 15;
		break;
	case '2':
		bq->adapt_power = 18;
		break;
	case '3':
		bq->adapt_power = 25;
		break;
	case '4':
		bq->adapt_power = 33;
		break;
	case '5':
		bq->adapt_power = 35;
		break;
	case '6':
		bq->adapt_power = 40;
		break;
	case '7':
		bq->adapt_power = 55;
		break;
	case '8':
		bq->adapt_power = 60;
		break;
	case '9':
		bq->adapt_power = 67;
		break;
	case 'A':
		bq->adapt_power = 80;
		break;
	case 'B':
		bq->adapt_power = 90;
		break;
	case 'C':
		bq->adapt_power = 100;
		break;
	case 'D':
		bq->adapt_power = 120;
		break;
	case 'E':
		bq->adapt_power = 140;
		break;
	case 'F':
		bq->adapt_power = 160;
		break;
	case 'G':
		bq->adapt_power = 180;
		break;
	case 'H':
		bq->adapt_power = 200;
		break;
	case 'I':
		bq->adapt_power = 220;
		break;
	case 'J':
		bq->adapt_power = 240;
		break;
	default:
		bq->adapt_power = 0;
		break;
	}
	mca_log_info("ch: %c, adapt_power: %d\n", ch, bq->adapt_power);

	*adapt_power = bq->adapt_power;

	return ret;
}

static int fg_get_dod_count(struct bq_fg_chip *bq)
{
	u8 data[REG_BLOCK_LENGTH] = {0};
	int ret = 0;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MIXDATARECORD1, data, REG_BLOCK_LENGTH);
	if (ret < 0) {
		mca_log_err("could not read dod_count = %d\n", ret);
		return ret;
	}

	bq->count_level1 = fg_convert_u8_to_u16(data[8], data[9]);
	bq->count_level2 = fg_convert_u8_to_u16(data[10], data[11]);
	bq->count_level3 = fg_convert_u8_to_u16(data[12], data[13]);
	bq->count_lowtemp = fg_convert_u8_to_u16(data[20], data[21]);

	mca_log_info("dod_count: %d, %d, %d, %d\n",
		bq->count_level1, bq->count_level2, bq->count_level3, bq->count_lowtemp);

	return 0;
}

static int fg_get_count_level1(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_level1)
		*count = bq->fake_count_level1;
	else
		*count = bq->count_level1;
	return 0;
}

static int fg_get_count_level2(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_level2)
		*count = bq->fake_count_level2;
	else
		*count = bq->count_level2;
	return 0;
}

static int fg_get_count_level3(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_level3)
		*count = bq->fake_count_level3;
	else
		*count = bq->count_level3;
	return 0;
}

static int fg_get_count_lowtemp(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_lowtemp)
		*count = bq->fake_count_lowtemp;
	else
		*count = bq->count_lowtemp;
	return 0;
}

static int fg_get_one_dod_count(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_dod_count(info);
}

static int fg_get_one_count_level1(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_level1(info, count);
}

static int fg_get_one_count_level2(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_level2(info, count);
}

static int fg_get_one_count_level3(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_level3(info, count);
}


static int fg_get_one_count_lowtemp(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_lowtemp(info, count);
}

static int fg_get_one_cutoff_voltage(void *data, int *volt)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_cutoff_voltage(info, volt);
}

static int fg_set_one_cutoff_voltage(void *data, int value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_set_cutoff_voltage(info, value);
}

static int fg_set_one_clear_count_data(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_clear_count_data(info);
}

static int fg_read_one_fc(void *data, bool *fc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int rc;

	rc = fg_read_status(info);
	*fc = info->batt_fc;

	return rc;
}

static int fg_toggle_co(struct bq_fg_chip *bq, u8 co_cmd, int expect, u8 *data);

static int fg_set_one_co(void *data, bool value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	u8 buf[4] = { 0xa1, 0xb2, 0xc3, 0xd4 };
	int ret;

	if (value) {
		ret = fg_toggle_co(info, FG_MAC_CMD_CLOSE_CO, 0, buf);
		if (ret < 0) {
			mca_log_err("close co fail, retry\n");
			ret = fg_toggle_co(info, FG_MAC_CMD_CLOSE_CO, 0, buf);
			if (ret < 0)
				mca_log_err("close co retry fail\n");
		}
	} else {
		ret = fg_toggle_co(info, FG_MAC_CMD_OPEN_CO, 1, buf);
		if (ret < 0) {
			mca_log_err("open co fail, retry\n");
			ret = fg_toggle_co(info, FG_MAC_CMD_OPEN_CO, 1, buf);
			if (ret < 0)
				mca_log_err("open co retry fail\n");
		}
	}

	return ret;
}

static int fg_read_one_rsoc(void *data, int *rsoc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_rsoc(info, rsoc);
}

static int fg_read_one_current(void *data, int *curr)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_current(info, curr);
}
static int fg_set_verify_digest(void *data, char *buf)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return set_verify_digest(info, buf);
}

static int fg_get_verify_digest(void *data, char *buf)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return get_verify_digest(info, buf);
}

static int fg_set_authentic(void *data, int value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	info->batt_auth = value;
	return 0;
}

static int fg_get_authentic(void *data, int *value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	*value = info->batt_auth;
	return 0;
}

static int fg_get_error_state(void *data, bool *error)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int ok;

	(void)fg_get_chip_ok(info, &ok);

	*error = info->fg_error;
	return 0;
}

static int fg_read_one_volt(void *data, int *volt)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_volt(info, volt);
}

static int fg_read_one_temperature(void *data, int *temp)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int rc;

	rc = fg_read_temperature(info, temp);

	return rc;
}

static int fg_read_one_original_temperature(void *data, int *temp)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int rc;

	rc = fg_read_original_temperature(info, temp);

	return rc;
}

static int fg_read_one_status(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_status(info);
}

static int fg_read_one_rm(void *data, int *rm)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_rm(info, rm);
}

static int fg_get_one_fastcharge_mode(void *data, int *ffc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_fastcharge_mode(info, ffc);
}

static int fg_set_one_fastcharge_mode(void *data, bool en)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_set_fastcharge_mode(info, en);
}

static int fg_get_one_soc_decimal(void *data, int *soc_decimal)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_soc_decimal(info, soc_decimal);
}

static int fg_read_one_charging_voltage(void *data, int *volt)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_charging_voltage(info, volt);
}

static int fg_get_one_chip_ok(void *data, int *ok)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_chip_ok(info, ok);
}

static int fg_read_one_cyclecount(void *data, int *cc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_cyclecount(info, cc);
}

static int fg_read_one_tte(void *data, int *tte)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_tte(info, tte);
}

static int fg_read_one_ttf(void *data, int *ttf)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_ttf(info, ttf);
}

static int fg_read_one_fcc(void *data, int *fcc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_fcc(info, fcc);
}

static int fg_read_one_dc(void *data, int *dc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_dc(info, dc);
}

static int fg_get_one_soc_decimal_rate(void *data, int *rate)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_soc_decimal_rate(info, rate);
}

static int fg_read_one_soh(void *data, int *soh)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_soh(info, soh);
}

static int fg_get_one_temp_max(void *data, int *temp_max)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_temp_max(info, temp_max);
}

static int fg_get_one_temp_min(void *data, int *temp_min)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_temp_min(info, temp_min);
}

static int fg_get_one_time_ot(void *data, int *time_ot)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_time_ot(info, time_ot);
}

static int fg_get_one_batt_cell_info(void *data, const char **name)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	if (info->cell_name) {
		*name = info->cell_name;
		return 0;
	}

	return -1;
}

static int fg_get_one_adapt_power(void *data, int *adapt_power)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_adapt_power(info, adapt_power);
}

static int fg_get_aged_flag(void *data, int *aged_flag)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	if (!data)
		return -1;

	*aged_flag = info->aged_flag;
	return 0;
}

static int fg_get_one_isc_alert_level(void *data, int *level)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_isc_alert_level(info, level);
}

static int fg_get_one_soa_alert_level(void *data, int *level)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_soa_alert_level(info, level);
}

static  int fg_get_one_raw_soc(void *data, int *raw_soc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_raw_soc(info, raw_soc);
}

static int fg_read_batt_info(void *data, void *info)
{
	struct bq_fg_chip *fg = (struct bq_fg_chip *)data;

	return fg_get_batt_info(fg, info);
}

static int fg_get_one_calibration_ffc_iterm(void *data, int *iterm)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_calibration_ffc_iterm(info, iterm);
}

static int fg_get_one_real_supplement_energy(void *data, int *supplement_energy)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_real_supplement_energy(info, supplement_energy);
}

static int fg_get_one_calibration_charge_energy(void *data, int *charge_energy)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_calibration_charge_energy(info, charge_energy);
}

static void fg_fl4p0_enable_check(void *data)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	int ret, count = 0;
	int seal_status = 0;
	u8 t_buf[36] = {0};

	ret = fg_set_seal(bq, true);
	ret |= fg_get_seal(bq, &seal_status);
	if (seal_status != SEAL_STATE_UNSEALED && seal_status != SEAL_STATE_FA) {
		mca_log_err("Fg Unseal Failed");
		return;
	}

	while (count++ < UNSEAL_COUNT_MAX) {
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_FEATURE_INFO, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not read feature_info, ret=%d", ret);
			msleep(100);
			continue;
		}
		t_buf[0] = 0x78;
		t_buf[1] = 0x00;
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FEATURE_INFO, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not write feature_info, ret=%d", ret);
			msleep(100);
		} else {
			mca_log_err("write feature_info success");
			break;
		}
	}

	count = 0;
	while (count++ < UNSEAL_COUNT_MAX) {
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_FEATURE_INFO, t_buf, 32);
		if (ret < 0) {
			mca_log_err("could not read feature_info, ret=%d", ret);
			msleep(100);
			continue;
		}

		if (t_buf[0] != 0x78 || t_buf[1] != 0x00) {
			mca_log_err("Enable FL4P0 failed");
			msleep(100);
		} else {
			mca_log_err("Enable FL4P0 Sucess");
			break;
		}
	}

	fg_set_seal(bq, false);
	return;
}

static int fg_get_one_device_name(void *data, const char **device_name)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	if (!info || !device_name)
		return -1;


	*device_name = fg_get_device_name(info);
	if (*device_name)
		return 0;

	return -1;
}

static int bq_parse_dt(struct bq_fg_chip *bq)
{
	struct device_node *node = bq->dev->of_node;
	int size = 0;
	int ret = 0;

	of_get_property(node, "bq,soc_decimal_rate", &size);
	if (size) {
		if (size % (2 * sizeof(*bq->dec_rate_seq)))
			return -EINVAL;
		bq->dec_rate_seq = devm_kzalloc(bq->dev, size, GFP_KERNEL);
		if (bq->dec_rate_seq) {
			bq->dec_rate_len = (size / sizeof(*bq->dec_rate_seq));
			if (bq->dec_rate_len % 2) {
				mca_log_info("invalid soc decimal rate seq\n");
				return -EINVAL;
			}
			ret = of_property_read_u32_array(node,
					"bq,soc_decimal_rate",
					bq->dec_rate_seq,
					bq->dec_rate_len);
			if (ret) {
				mca_log_info("Failed to get bq,soc_decimal_rate!\n");
				return -EINVAL;
			}
		} else {
			return -ENOMEM;
		}
	}
	mca_parse_dts_u32(node, "bq,charge-full-design", &bq->batt_dc, DEFUALT_FULL_DESIGN);
	mca_parse_dts_u32(node, "ic_role", &bq->dev_role, 0);
	if (bq->dev_role >= FG_IC_MAX)
		return -EINVAL;
	mca_parse_dts_u32(node, "soh_th", &bq->soh_th, FG_IC_SOH_TH_DEFAUL);
	mca_parse_dts_u32(node, "cycle_th", &bq->cycle_th, FG_IC_CYCLE_TH_DEFAUL);
	mca_parse_dts_u32(node, "cycle_step", &bq->cycle_step, FG_IC_CYCLE_STEP_DEFAUL);
	mca_parse_dts_u32(node, "resistance-id", &bq->resistance_id, FG_IC_RESISTANCE_ID_DEFAUL);
	mca_parse_dts_u8_array(node, "unseal_key", bq->key, BATTERY_UNSEAL_KEY_LEN);
	mca_log_err("0x%x,0x%x,0x%x,0x%x\n",bq->key[0], bq->key[1], bq->key[2], bq->key[3]);
	bq->support_voltage_record_level = of_property_read_bool(node, "support-voltage-record-level");
	if (bq->support_voltage_record_level) {
		mca_parse_dts_u8(node, "version_number", &bq->version_number, 0);
		mca_parse_dts_u8(node, "start_byte_address", &bq->start_byte_address, 0);
		mca_parse_dts_u8(node, "byte_length", &bq->byte_length, 0);
		size = of_property_count_u8_elems(node, "record_voltage");
		if (size < 0 ) {
			mca_log_err("record_voltage is not exist\n");
			bq->update_fw_status = FG_FW_INVALID;
		} else {
			mca_log_info("record_voltage size %d, reg_length %d\n", size, bq->byte_length);
			bq->record_voltage = devm_kzalloc(bq->dev, size, GFP_KERNEL);
			if (bq->record_voltage)
				mca_parse_dts_u8_array(node, "record_voltage", bq->record_voltage, size);
			else
				bq->update_fw_status = FG_FW_INVALID;

		}
		if (!bq->version_number || !bq->start_byte_address || !bq->byte_length) {
			bq->update_fw_status = FG_FW_INVALID;
			mca_log_err("OTA INFO ERROR [0x%x][0x%x][0x%x]\n", bq->version_number, bq->start_byte_address, bq->byte_length);
		}
	}

	bq->support_version_compatible =
		of_property_read_bool(node, "support-version-compatible");
	mca_parse_dts_u32(node, "support_nvt1000_ota",
			  &bq->support_nvt1000_ota, 0);

	bq->support_co_by_gpio = of_property_read_bool(node, "co_by_gpio");
	if (bq->support_co_by_gpio) {
		bq->co_gpio = of_get_named_gpio(node, "batt_ctl", 0);
		if (bq->co_gpio < 0) {
			mca_log_err("failed to parse ctl_co_gpio\n");
		} else {
			if (devm_gpio_request_one(bq->dev, bq->co_gpio,
						  GPIOF_OUT_INIT_LOW, dev_name(bq->dev)))
				mca_log_err("%s unable to request ctl_co gpio [%d]\n",
					    dev_name(bq->dev), bq->co_gpio);
			else if (gpiod_direction_output_raw(
					 gpio_to_desc(bq->co_gpio), 0))
				mca_log_err("%s unable to set direction for ctl_co gpio [%d]\n",
					    dev_name(bq->dev), bq->co_gpio);
			mca_log_info("ctl_co_gpio[%d] val is :%d\n", bq->co_gpio,
				     gpiod_get_raw_value(
					     gpio_to_desc(bq->co_gpio)));
		}
	}

	return 0;
}

static int fg_set_term_config(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 data_term[32] = {0};

	if (bq->fg_vendor != BQ27Z561)
		return 0;

	data_term[0] = 220;
	data_term[1] = 5;

	ret = fg_mac_write_block(bq, FG_MAC_CMD_TERM_CURRENTS, data_term, 2);
	if (ret < 0) {
		mca_log_info("could not write term current = %d\n", ret);
		return ret;
	}
	return ret;
}

static inline unsigned long fg_convert_u8_to_u16(unsigned char lsb, unsigned char hsb)
{
	return ((hsb << 8) | lsb);
}

static unsigned long fg_get_calc_rvalue(struct bq_fg_chip *bq)
{
	unsigned long k1 = 45;
	unsigned long k2 = 45;
	unsigned long k3 = 70;
	unsigned long k4 = 45;
	unsigned long k5 = 100;
	unsigned long k6 = 70;
	unsigned long k7 = 45;
	unsigned long CCcc, FFff, IIii, JJjj, MMmm, NNnn, OOoo;
	unsigned char data[32] = { 0 };
	unsigned long rvalue;

	if (fg_mac_read_block(bq, FG_MAC_CMD_CALC_RVALUE, data, 32) < 0)
		return 0;

	CCcc = fg_convert_u8_to_u16(data[4], data[5]);
	FFff = fg_convert_u8_to_u16(data[10], data[11]);
	IIii = fg_convert_u8_to_u16(data[16], data[17]);
	JJjj = fg_convert_u8_to_u16(data[18], data[19]);
	MMmm = fg_convert_u8_to_u16(data[24], data[25]);
	NNnn = fg_convert_u8_to_u16(data[26], data[27]);
	OOoo = fg_convert_u8_to_u16(data[28], data[29]);

	rvalue = (CCcc * k1 + FFff * k2 + IIii * k3 + JJjj * k4 +
		MMmm * k5 + NNnn * k6 + OOoo * k7) / 10;
	mca_log_info("[%u]calr_rvalue=%lu\n", bq->dev_role, rvalue);

	return rvalue;
}

static unsigned long fg_get_one_calc_rvalue(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_calc_rvalue(info);
}

static void fg_store_ui_soh(struct bq_fg_chip *bq, const char *buf, size_t size)
{
	char *pchar = NULL;
	int count = 0, i = 0, ret;
	unsigned char data_uisoh[32] = { 0 };
	unsigned char data[32] = { 0 };
	unsigned int temp = 0;
	char *tmp_buf = NULL;

	tmp_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!tmp_buf)
		return;

	strscpy(tmp_buf, buf, size + 1);
	mca_log_info("%s\n", tmp_buf);
	while ((pchar = strsep(&tmp_buf, " ")) != NULL) {
		if (kstrtouint(pchar, 0, &temp)) {
			mca_log_err("pchar cover to int fail\n");
			goto _write_reg;
		}
		data_uisoh[count] = (unsigned char)temp;
		mca_log_info("data_uisoh[%d]=%d\n", count, data_uisoh[count]);
		++count;
		if (count >= 11)
			break;
	}

_write_reg:
	bq->ui_soh = data_uisoh[0];

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
	if (ret < 0) {
		mca_log_info("failed to get first_usage\n");
		return;
	}
	for (i = 0; i < 11; i++)
		data[i] = data_uisoh[i];

	if (fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32))
		mca_log_err("write ui_soh failed\n");
}

static int __fg_mac_read_block(struct bq_fg_chip *bq, u16 cmd, u8 *buf,
					u8 len)
{
	int ret;
	u8 cksum_calc, cksum;
	u8 t_buf[40];
	u8 t_len;
	int i;

	t_buf[0] = (u8)cmd;
	t_buf[1] = (u8)(cmd >> 8);

	ret = fg_write_read_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, 2, 36);
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		return ret;

	{
		char strbuf[128];
		int pos = 0;

		for (i = 0; i < 36; i++)
			pos += sprintf(&strbuf[pos], "%02X ", t_buf[i]);
		mca_log_debug("mac 0x%04x read: %s\n", cmd, strbuf);
	}

	cksum = t_buf[34];
	t_len = t_buf[35];
	if (t_len > 42)
		t_len = 42;
	else if (t_len < 2)
		t_len = 2;

	cksum_calc = checksum(t_buf, t_len - 2);
	if (cksum_calc != cksum)
		return -EIO;

	for (i = 0; i < len; i++)
		buf[i] = t_buf[i + 2];

	return ret;
}

static int
fg_diag_mac_read(struct bq_fg_chip *bq, u16 cmd, u8 *buf, u8 len)
{
	if (atomic_read(&bq->pm_suspend))
		return -1;

	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (atomic_read(&bq->pm_suspend))
			return -1;
		usleep_range(40, 50);
	}

	return __fg_mac_read_block(bq, cmd, buf, len);
}

static int fg_toggle_co(struct bq_fg_chip *bq, u8 co_cmd, int expect, u8 *data)
{
	u8 flag = 0;
	int ret, co_state = 0, want = expect & 1, i;

	ret = fg_mac_write_block(bq, co_cmd, data, 4);
	if (ret < 0)
		mca_log_err("write co cmd 0x%x fail\n", co_cmd);

	for (i = 0; i < 4; i++) {
		msleep(i ? 100 : 1100);
		flag = 0;
		if (fg_diag_mac_read(bq, FG_MCA_CMD_SEAL_STATE, &flag, 1) < 0)
			mca_log_err("get co state fail\n");
		co_state = !!(flag & FG_FLAGS_CO);
		mca_log_err("co cmd 0x%x, co_state %d\n", co_cmd, co_state);
		if (co_state == want)
			break;
	}

	return (co_state == want) ? 0 : -1;
}

#define NFG1000_STATUS_CMD	0x3f
#define NFG1000_ERASE_REG	0x37
#define NFG1000_WRITE_REG	0x30
#define NFG1000_READ_REG	0x31
#define NFG1000_SECTION_SIZE	0x200
#define NFG1000_PAGE_SIZE	0x80

#define NFG1000_OTA_VER_MARK	20
#define NFG1000_OTA_DEV_NAME	64
#define NFG1000_OTA_STAMP	99
#define NFG1000_OTA_DF_SIG	120
#define NFG1000_OTA_FLAG	231
#define NFG1000_OTA_CRC		511

#define NFG1000_OTA_MAGIC	0x33666633

u8 sha_256_ramdom_data[32] = {
	0x12, 0x12, 0xe3, 0x39, 0x36, 0x86, 0x49, 0xf0,
	0x3f, 0xc7, 0x6b, 0x4c, 0x17, 0xfb, 0xb3, 0x15,
	0x32, 0xbe, 0x5c, 0xa5, 0xfc, 0x63, 0x99, 0x22,
	0x71, 0xe9, 0xf9, 0x36, 0x91, 0x35, 0xb1, 0x72,
};
u8 sha_256_pass_word[32] = {
	0x46, 0x6c, 0xea, 0x38, 0xbd, 0x1f, 0x52, 0x73,
	0x69, 0x96, 0x79, 0x0e, 0x53, 0xd5, 0xa2, 0x89,
	0xb8, 0x6b, 0x91, 0x77, 0xd6, 0xee, 0x82, 0x94,
	0x6b, 0x78, 0x94, 0x53, 0xbe, 0xbb, 0x32, 0xf5,
};
u8 data_flash_erase_order[19] = {
	0x00, 0x07, 0x01, 0x86, 0x01, 0x87, 0x01, 0x88,
	0x01, 0x89, 0x01, 0x8a, 0x01, 0x8b, 0x01, 0x8c,
	0x01, 0x8d, 0x07,
};
u8 static_df_sig[16] = {
	0x08, 0x00, 0x0e, 0x00, 0x10, 0x00, 0x0d, 0x00,
	0xf4, 0x00, 0x19, 0x00, 0xb9, 0x00, 0x7c, 0x00,
};
u8 code_version_crc16[16] = {
	0x23, 0x0e, 0x23, 0x0e, 0x23, 0x0e, 0x23, 0x0e,
	0xe2, 0x1a, 0xfb, 0x9e, 0x38, 0xc2, 0x38, 0xc2,
};

static u8 GetCRC8(u8 *buf, int len)
{
	int crc = 0, i;

	while (len--) {
		crc ^= *buf++;
		for (i = 0; i < 8; i++)
			crc = ((s8)crc < 0) ? ((crc << 1) ^ 0x07) :
					      (crc << 1);
	}
	return crc;
}

static int nfg1000_write(struct bq_fg_chip *bq, u8 reg, u8 *buf,
					 u8 len)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_block(bq->client, reg, buf, len);
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}

static int nfg1000_status_ok(struct bq_fg_chip *bq)
{
	u8 wbuf = NFG1000_STATUS_CMD;
	u8 rbuf[3] = { 0 };
	struct i2c_msg msg[2];
	int ret;

	if (!bq->client || !bq->client->adapter)
		return -ENODEV;

	msg[0].addr = bq->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &wbuf;
	msg[1].addr = bq->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(rbuf);
	msg[1].buf = rbuf;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_transfer(bq->client->adapter, msg, 2);
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		return ret;

	return (rbuf[0] == 0x01 && rbuf[1] == 0x55 && rbuf[2] == 0x48) ? 0 :
									-EIO;
}

static int nfg1000_erase_flash_step(struct bq_fg_chip *bq, u32 addr, u8 param)
{
	u8 frame[10];
	int ret, retry;

	frame[0] = 0xaa;
	frame[1] = NFG1000_ERASE_REG;
	frame[2] = 0x06;
	frame[3] = addr & 0xff;
	frame[4] = (addr >> 8) & 0xff;
	frame[5] = (addr >> 16) & 0xff;
	frame[6] = 0;
	frame[7] = param;
	frame[8] = 0;
	frame[9] = GetCRC8(frame, 9);

	for (retry = 0; retry < 5; retry++) {
		ret = nfg1000_write(bq, frame[1], &frame[2], 8);
		msleep((param & 0xff) * 10);
		if (ret < 0) {
			mca_log_err("erase write fail\n");
		} else if (nfg1000_status_ok(bq) == 0) {
			usleep_range(2000, 2100);
			return 0;
		} else {
			usleep_range(2000, 2100);
			mca_log_err("erase status fail\n");
		}
		usleep_range(10000, 10100);
	}

	mca_log_err("erase flash step 0x%x fail\n", addr);
	return -1;
}

static int nfg1000_read_flash_step(struct bq_fg_chip *bq, u32 addr, u8 *buf,
				   int len)
{
	u8 frame[8];
	struct i2c_msg msg[2];
	u8 raddr[3];
	int ret, retry;

	frame[0] = 0xaa;
	frame[1] = NFG1000_READ_REG;
	frame[2] = 0x04;
	frame[3] = 0x04;
	frame[4] = 0x04;
	frame[5] = 0x04;
	frame[6] = 0;
	frame[7] = GetCRC8(frame, 7);

	for (retry = 0; retry < 5; retry++) {
		ret = nfg1000_write(bq, frame[1], &frame[2], 6);
		usleep_range(2000, 2100);
		if (ret < 0) {
			mca_log_err("read setup fail\n");
			usleep_range(10000, 10100);
			continue;
		}

		if (!bq->client || !bq->client->adapter)
			return -ENODEV;
		raddr[0] = 0xaa;
		raddr[1] = addr & 0xff;
		raddr[2] = 0xab;
		msg[0].addr = bq->client->addr;
		msg[0].flags = 0;
		msg[0].len = sizeof(raddr);
		msg[0].buf = raddr;
		msg[1].addr = bq->client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = len;
		msg[1].buf = buf;

		mutex_lock(&bq->i2c_rw_lock);
		ret = i2c_transfer(bq->client->adapter, msg, 2);
		mutex_unlock(&bq->i2c_rw_lock);
		usleep_range(2000, 2100);
		if (ret >= 0)
			return 0;
		usleep_range(10000, 10100);
	}

	mca_log_err("read flash step 0x%x fail\n", addr);
	return -1;
}

static int nfg1000_update_flash_step(struct bq_fg_chip *bq, u32 addr,
				     u8 *src)
{
	u8 frame[8];
	u8 page[2 + NFG1000_PAGE_SIZE];
	u8 verify[NFG1000_SECTION_SIZE];
	int ret, off;

	if (nfg1000_erase_flash_step(bq, addr, 1) < 0)
		mca_log_err("update: erase 0x%x fail\n", addr);

	for (off = 0; off < NFG1000_SECTION_SIZE; off += NFG1000_PAGE_SIZE) {
		u32 paddr = addr + off;

		frame[0] = 0xaa;
		frame[1] = NFG1000_WRITE_REG;
		frame[2] = 0x04;
		frame[3] = paddr & 0xff;
		frame[4] = (paddr >> 8) & 0xff;
		frame[5] = (paddr >> 16) & 0xff;
		frame[6] = 0;
		frame[7] = GetCRC8(frame, 7);
		ret = nfg1000_write(bq, frame[1], &frame[2], 6);
		usleep_range(2000, 2100);
		if (ret < 0)
			mca_log_err("update: setup 0x%x fail\n", paddr);

		page[0] = 0xaa;
		page[1] = paddr & 0xff;
		memcpy(&page[2], src + off, NFG1000_PAGE_SIZE);
		ret = nfg1000_write(bq, page[0], &page[1], 1 + NFG1000_PAGE_SIZE);
		usleep_range(10000, 10100);
		if (ret < 0)
			mca_log_err("update: write 0x%x fail\n", paddr);
	}

	if (nfg1000_read_flash_step(bq, addr, verify, NFG1000_SECTION_SIZE) < 0)
		return -1;
	for (off = 0; off < NFG1000_SECTION_SIZE; off++) {
		if (verify[off] != src[off]) {
			mca_log_err("update: verify 0x%x mismatch\n", addr);
			return -1;
		}
	}
	return 0;
}

static int nfg1000_ota_program_step5_update_gauge(struct bq_fg_chip *bq)
{
	int ver = bq->nfg1000_ver;
	u8 *code = nfg1000_code_data;

	if (ver < 0 || ver > 7)
		return -1;

	bq->nfg1000_section_marker = 0xaaaaaaaa;

	if (nfg1000_erase_flash_step(bq, 0x3400, 1) < 0) {
		mca_log_err("failed earse flash step1\n");
		return -1;
	}
	if (nfg1000_erase_flash_step(bq, ver < 4 ? 0xda00 : 0xd800, 1) < 0) {
		mca_log_err("failed earse flash step2/3\n");
		return -1;
	}
	if (nfg1000_erase_flash_step(bq, 0x1e200, ver < 2 ? 2 : 4) < 0) {
		mca_log_err("failed earse flash step4/5\n");
		return -1;
	}

	if (bq->nfg1000_magic == NFG1000_OTA_MAGIC &&
	    nfg1000_update_flash_step(bq, 0x1f000, bq->nfg1000_ota_buf) < 0) {
		mca_log_err("failed earse flash step6\n");
		return -1;
	}

	bq->nfg1000_ota_buf[NFG1000_OTA_STAMP] = 0x42;
	bq->nfg1000_ota_buf[NFG1000_OTA_DF_SIG] = static_df_sig[ver * 2];
	bq->nfg1000_ota_buf[NFG1000_OTA_DF_SIG + 1] = static_df_sig[ver * 2 + 1];
	bq->nfg1000_ota_buf[NFG1000_OTA_FLAG] &= 0xfb;
	bq->nfg1000_ota_buf[NFG1000_OTA_CRC] =
		GetCRC8(bq->nfg1000_ota_buf, NFG1000_OTA_CRC);

	if (nfg1000_update_flash_step(bq, 0x1e000, bq->nfg1000_ota_buf) < 0) {
		mca_log_err("failed write flash step11\n");
		return -1;
	}

	if (nfg1000_update_flash_step(bq, 0x1e200, code + ver * 0xe00 + 0x600) < 0 ||
	    nfg1000_update_flash_step(bq, 0x1e400, code + ver * 0xe00 + 0x800) < 0) {
		mca_log_err("update 0x0001E200 error\n");
		return -1;
	}
	if (ver > 1 &&
	    (nfg1000_update_flash_step(bq, 0x1e600, code + ver * 0xe00 + 0xa00) < 0 ||
	     nfg1000_update_flash_step(bq, 0x1e800, code + ver * 0xe00 + 0xc00) < 0)) {
		mca_log_err("update 0x0001E200 error\n");
		return -1;
	}

	if (nfg1000_update_flash_step(bq, ver < 4 ? 0xda00 : 0xd800,
				      code + ver * 0xe00 + 0x400) < 0) {
		mca_log_err("update 0x0000DA00/D800 error\n");
		return -1;
	}
	if (nfg1000_update_flash_step(bq, 0x3400, code + ver * 0xe00) < 0) {
		mca_log_err("update 0x00003400 error\n");
		return -1;
	}
	return 0;
}

static int nfg1000_ota_program_step6_CheckCrc(struct bq_fg_chip *bq)
{
	u8 frame[14] = { 0 };
	int ret, retry;

	if (bq->nfg1000_ver > 7)
		return -1;

	frame[0] = 0xaa;
	frame[1] = 0x38;
	frame[2] = 0x0a;
	frame[3] = 0x04;
	frame[4] = 0x34;
	frame[7] = 0xfc;
	frame[8] = 0x8b;
	frame[9] = 0x01;
	frame[11] = code_version_crc16[bq->nfg1000_ver * 2];
	frame[12] = code_version_crc16[bq->nfg1000_ver * 2 + 1];
	frame[13] = GetCRC8(frame, 13);

	for (retry = 0; retry < 5; retry++) {
		ret = nfg1000_write(bq, frame[1], &frame[2], 12);
		usleep_range(2000, 2100);
		if (ret < 0) {
			mca_log_err("could not write flash\n");
		} else {
			msleep(100);
			if (nfg1000_status_ok(bq) == 0)
				return 0;
		}
		usleep_range(10000, 10100);
	}

	mca_log_err("nfg1000_crc16_check_step: %x error!!\n", NFG1000_STATUS_CMD);
	return -1;
}

static int nfg1000_ota_program_step7_ExitBoot(struct bq_fg_chip *bq)
{
	u8 frame[5];
	int ret;

	frame[0] = 0xaa;
	frame[1] = 0x3d;
	frame[2] = 0x01;
	frame[3] = 0x00;
	frame[4] = GetCRC8(frame, 4);

	ret = nfg1000_write(bq, frame[1], &frame[2], 3);
	usleep_range(2000, 2100);
	usleep_range(10000, 10100);
	if (ret < 0 || nfg1000_status_ok(bq) < 0) {
		mca_log_err("step7 exit boot fail\n");
		return -1;
	}
	return 0;
}

static int nfg1000_update_judge(struct bq_fg_chip *bq)
{
	u8 name[16] = { 0 };
	u8 ffn[6] = { 0 };
	u8 sub[2] = { 0 };
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_mac_read_block(bq, 0x4a, name, 16);
	if (ret < 0) {
		mca_log_err("judge: read name fail\n");
		return 1;
	}
	if (name[0] != 'X' || name[1] != 'M' || name[2] != '1' ||
	    name[3] != '5')
		return 1;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_mac_read_block(bq, 0x4b, ffn, 6);
	if (ret < 0) {
		mca_log_err("judge: read ffn fail\n");
		return 1;
	}
	if (ffn[0] != 'F' || ffn[1] != 'F' || ffn[2] != 'N')
		return 1;
	if (ffn[3] < 'B' || ffn[3] > 'N') {
		if (ffn[3] != 'A')
			return 1;
	}

	mutex_lock(&bq->i2c_rw_lock);
	if (ffn[3] == 'A')
		ret = __fg_mac_read_block(bq, 0x05, sub, 2);
	else
		ret = __fg_mac_read_block(bq, 0x08, sub, 2);
	if (ret < 0) {
		mca_log_err("judge: read sub fail\n");
		return 1;
	}

	return sub[1] ? 1 : 0;
}

static int nfg1000_ota_program_step2_ShaAuth(struct bq_fg_chip *bq)
{
	u8 frame[36] = { 0 };
	int ret;

	frame[0] = 0xaa;
	frame[1] = 0x81;
	frame[2] = 0x20;
	memcpy(&frame[3], sha_256_ramdom_data, sizeof(sha_256_ramdom_data));
	frame[35] = GetCRC8(frame, 35);
	ret = nfg1000_write(bq, frame[1], &frame[2], 34);
	usleep_range(2000, 2100);
	if (ret < 0) {
		mca_log_err("sha auth: challenge write fail\n");
		return -1;
	}

	frame[0] = 0xaa;
	frame[1] = 0x82;
	frame[2] = 0x20;
	memcpy(&frame[3], sha_256_pass_word, sizeof(sha_256_pass_word));
	frame[35] = GetCRC8(frame, 35);
	ret = nfg1000_write(bq, frame[1], &frame[2], 34);
	usleep_range(2000, 2100);
	if (ret < 0) {
		mca_log_err("sha auth: response write fail\n");
		return -1;
	}

	if (nfg1000_status_ok(bq) < 0) {
		mca_log_err("sha auth status fail\n");
		return -1;
	}
	return 0;
}

static int nfg1000_mcu_auth_ok(struct bq_fg_chip *bq)
{
	u8 wbuf = 0x50;
	u8 rbuf[6] = { 0 };
	struct i2c_msg msg[2];
	int ret;

	if (!bq->client || !bq->client->adapter)
		return -ENODEV;

	msg[0].addr = bq->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &wbuf;
	msg[1].addr = bq->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(rbuf);
	msg[1].buf = rbuf;

	mutex_lock(&bq->i2c_rw_lock);
	ret = i2c_transfer(bq->client->adapter, msg, 2);
	mutex_unlock(&bq->i2c_rw_lock);
	if (ret < 0)
		return ret;

	return (rbuf[0] == 0x04 && rbuf[1] == 0x11 && rbuf[2] == 0x83 &&
		rbuf[3] == 0x00 && rbuf[4] == 0x00 && rbuf[5] == 'R') ? 0 :
									-EIO;
}

static int nfg1000_ota_program_step4_gauge_version(struct bq_fg_chip *bq)
{
	u8 *buf = bq->nfg1000_ota_buf;
	u8 code_sec[NFG1000_SECTION_SIZE];
	int ver;
	u8 sig;

	if (nfg1000_read_flash_step(bq, 0x1f000, buf, NFG1000_SECTION_SIZE) < 0) {
		mca_log_err("failed read flash step1\n");
		return -1;
	}
	if (GetCRC8(buf, NFG1000_OTA_CRC) == buf[NFG1000_OTA_CRC] &&
	    buf[NFG1000_OTA_VER_MARK] == 0x06 &&
	    buf[NFG1000_OTA_VER_MARK + 1] == 0x00 &&
	    buf[NFG1000_OTA_CRC] != 0xff) {
		bq->nfg1000_magic = NFG1000_OTA_MAGIC;
	} else {
		if (nfg1000_read_flash_step(bq, 0x1e000, buf,
					    NFG1000_SECTION_SIZE) < 0) {
			mca_log_err("failed read flash step2\n");
			return -1;
		}
		if (!(GetCRC8(buf, NFG1000_OTA_CRC) == buf[NFG1000_OTA_CRC] &&
		      buf[NFG1000_OTA_VER_MARK] == 0x06 &&
		      buf[NFG1000_OTA_VER_MARK + 1] == 0x00 &&
		      buf[NFG1000_OTA_CRC] != 0xff)) {
			mca_log_err("read error: %x\n", buf[NFG1000_OTA_CRC]);
			return -1;
		}
		bq->nfg1000_magic = 0;
	}

	if (buf[NFG1000_OTA_DF_SIG + 1] != 0) {
		mca_log_err("static df signature check error: %x\n",
			    buf[NFG1000_OTA_DF_SIG]);
		return -1;
	}
	sig = buf[NFG1000_OTA_DF_SIG];
	switch (sig) {
	case 0x47: ver = 0; break;
	case 0x41: ver = 1; break;
	case 0xc3: ver = 2; break;
	case 0x7f: ver = 3; break;
	case 0xa8: ver = 4; break;
	case 0xf6: ver = 5; break;
	case 0x14: ver = 6; break;
	case 0x2b: ver = 7; break;
	default:
		mca_log_err("static df signature check error: %x\n", sig);
		return -1;
	}
	bq->nfg1000_ver = ver;

	/* even versions ship device name "XM15@BM6A#", odd "XM15@BP5B#" */
	if (memcmp(&buf[NFG1000_OTA_DEV_NAME],
		   (ver & 1) ? "XM15@BP5B#" : "XM15@BM6A#", 10)) {
		mca_log_err("device name error\n");
		return -1;
	}

	if (nfg1000_read_flash_step(bq, 0x3600, code_sec,
				    NFG1000_SECTION_SIZE) < 0) {
		mca_log_err("failed read flash step3\n");
		return -1;
	}
	if (memcmp(nfg1000_code_data + ver * 0xe00 + 0x200, code_sec,
		   NFG1000_SECTION_SIZE)) {
		mca_log_err("app code check error\n");
		return -1;
	}
	return 0;
}

static int nfg1000_update_APP(struct bq_fg_chip *bq)
{
	u8 cmd[4];

	cmd[0] = 0x00; cmd[1] = 0x0f;
	nfg1000_write(bq, 0x00, cmd, 2);
	cmd[0] = 0x34; cmd[1] = 0x12;
	nfg1000_write(bq, 0x00, cmd, 2);
	cmd[0] = 0x34; cmd[1] = 0x12;
	nfg1000_write(bq, 0x00, cmd, 2);
	usleep_range(2000, 2100);

	cmd[0] = 0x36; cmd[1] = 0x00; cmd[2] = 0x00;
	nfg1000_write(bq, 0x36, cmd, 3);
	usleep_range(2000, 2100);
	nfg1000_write(bq, 0x36, cmd, 3);
	usleep_range(2000, 2100);
	if (nfg1000_status_ok(bq) < 0)
		mca_log_err("update_APP: enter boot fail\n");

	if (nfg1000_ota_program_step2_ShaAuth(bq) < 0)
		return -1;

	if (nfg1000_mcu_auth_ok(bq) < 0) {
		mca_log_err("nfg1000 ota program step3 fail\n");
		return -1;
	}

	if (nfg1000_ota_program_step4_gauge_version(bq) < 0)
		return -1;

	if (nfg1000_ota_program_step5_update_gauge(bq) < 0)
		return -1;
	if (nfg1000_ota_program_step6_CheckCrc(bq) < 0)
		return -1;
	if (nfg1000_ota_program_step7_ExitBoot(bq) < 0)
		return -1;

	msleep(1500);
	return 0;
}

static int nfg1000_ota_update_check(struct bq_fg_chip *bq)
{
	u8 cmd[4];
	int ret = 0, i;

	if (nfg1000_update_judge(bq))
		return 0;

	cmd[0] = 0x00; cmd[1] = 0x0f;
	nfg1000_write(bq, 0x00, cmd, 2);
	usleep_range(2000, 2100);

	for (i = 0; i < 3; i++) {
		ret = nfg1000_update_APP(bq);
		if (ret == 0)
			break;
		mca_log_err("ota update attempt %d fail\n", i);
		nfg1000_ota_program_step7_ExitBoot(bq);
		msleep(1500);
	}

	return ret;
}

static void nfg1000_boot_app_judge(struct bq_fg_chip *bq)
{
	u8 alt_mac_data[2] = { 0x00, 0x02 };	/* 0x0200 */
	u16 volt = 0;
	u8 boot_cmd = 0;
	int ret = 0, i;

	if (!bq->support_nvt1000_ota)
		return;

	for (i = 0; i < 3; i++) {
		mutex_lock(&bq->i2c_rw_lock);
		ret = __fg_read_word(bq->client, bq->regs[BQ_FG_REG_VOLT],
				     &volt);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret >= 0)
			break;
		mca_log_err("could not read volt ret = %d\n", ret);
		msleep(100);
	}
	if (ret < 0)
		return;

	for (i = 0; i < 3; i++) {
		mutex_lock(&bq->i2c_rw_lock);
		__fg_write_block(bq->client, bq->regs[BQ_FG_REG_ALT_MAC],
				 alt_mac_data, 2);
		mutex_unlock(&bq->i2c_rw_lock);
		usleep_range(1000, 1100);
		mutex_lock(&bq->i2c_rw_lock);
		ret = __fg_read_byte(bq->client, 0x3f, &boot_cmd);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret >= 0)
			break;
		mca_log_err("could not read boot_cmd = %d\n", 0x3f);
		msleep(100);
	}

	if (volt == 0 && boot_cmd == 1) {
		for (i = 0; i < 3; i++) {
			ret = nfg1000_update_APP(bq);
			if (ret == 0)
				break;
			nfg1000_ota_program_step7_ExitBoot(bq);
			msleep(1500);
		}
	} else {
		nfg1000_ota_update_check(bq);
	}
}

static int fg_get_ota_update_flag(void *data, int *flag)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	*flag = bq->ota_update_flag;
	mca_log_info("ota update flag %d\n", *flag);
	return 0;
}

static int fg_get_csd_flag(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CSD, t_buf, 8) < 0)
		return 0;
	return t_buf[1];
}

static int fg_get_csd_r1(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CSD, t_buf, 8) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[2], t_buf[3]);
}

static int fg_get_csd_r2(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CSD, t_buf, 8) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[4], t_buf[5]);
}

static void fg_get_cuv_count(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CUV_OCD, t_buf, 32) < 0)
		bq->cuv_count = 0;
	else
		bq->cuv_count = fg_convert_u8_to_u16(t_buf[16], t_buf[17]);
}

static void fg_get_cuv_last_cycle(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CUV_OCD, t_buf, 32) < 0)
		bq->cuv_last_cycle = 0;
	else
		bq->cuv_last_cycle = fg_convert_u8_to_u16(t_buf[18], t_buf[19]);
}

static void fg_get_ocd_count(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CUV_OCD, t_buf, 32) < 0)
		bq->ocd_count = 0;
	else
		bq->ocd_count = fg_convert_u8_to_u16(t_buf[28], t_buf[29]);
}

static void fg_get_ocd_last_cycle(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CUV_OCD, t_buf, 32) < 0)
		bq->ocd_last_cycle = 0;
	else
		bq->ocd_last_cycle = fg_convert_u8_to_u16(t_buf[30], t_buf[31]);
}

static void fg_get_hcuv_count(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_HCUV_HOCD, t_buf, 8) < 0)
		bq->hcuv_count = 0;
	else
		bq->hcuv_count = fg_convert_u8_to_u16(t_buf[0], t_buf[1]);
}

static void fg_get_hcuv_last_cycle(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_HCUV_HOCD, t_buf, 8) < 0)
		bq->hcuv_last_cycle = 0;
	else
		bq->hcuv_last_cycle = fg_convert_u8_to_u16(t_buf[2], t_buf[3]);
}

static void fg_get_hocd_count(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_HCUV_HOCD, t_buf, 32) < 0)
		bq->hocd_count = 0;
	else
		bq->hocd_count = fg_convert_u8_to_u16(t_buf[12], t_buf[13]);
}

static void fg_get_hocd_last_cycle(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_HCUV_HOCD, t_buf, 32) < 0)
		bq->hocd_last_cycle = 0;
	else
		bq->hocd_last_cycle = fg_convert_u8_to_u16(t_buf[14], t_buf[15]);
}

static void fg_get_hscd_count(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_HCUV_HOCD, t_buf, 32) < 0)
		bq->hscd_count = 0;
	else
		bq->hscd_count = fg_convert_u8_to_u16(t_buf[16], t_buf[17]);
}

static void fg_get_hscd_last_cycle(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_HCUV_HOCD, t_buf, 32) < 0)
		bq->hscd_last_cycle = 0;
	else
		bq->hscd_last_cycle = fg_convert_u8_to_u16(t_buf[18], t_buf[19]);
}

/*
 * Snapshot the cached abnormal-event counters into the DFX buffer. Order is
 * ABI: ocd, hocd, cuv, hcuv, hscd (count then last-cycle within each pair).
 * The counters are refreshed by their fg_get_* readers (sysfs / periodic path).
 */
static int fg_get_batt_abnormal_info(void *data, int *info)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	info[0] = bq->ocd_count;
	info[1] = bq->ocd_last_cycle;
	info[2] = bq->hocd_count;
	info[3] = bq->hocd_last_cycle;
	info[4] = bq->cuv_count;
	info[5] = bq->cuv_last_cycle;
	info[6] = bq->hcuv_count;
	info[7] = bq->hcuv_last_cycle;
	info[8] = bq->hscd_count;
	info[9] = bq->hscd_last_cycle;
	return 0;
}

static void fg_read_co_status(struct bq_fg_chip *bq, u32 *co_status)
{
	u8 t_buf[1] = { 0 };

	if (fg_diag_mac_read(bq, FG_MCA_CMD_SEAL_STATE, t_buf, 1) < 0)
		mca_log_err("Failed to read CO state\n");
	*co_status = (t_buf[0] >> 2) & 1;
}

static void fg_read_co_auto_open(struct bq_fg_chip *bq, u32 *co_auto_open)
{
	u8 t_buf[2] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CO_AUTO_OPEN, t_buf, 2) < 0)
		mca_log_err("Failed to read Co Atuo Open\n");
	*co_auto_open = (t_buf[1] >> 2) & 1;
}

static void fg_read_chem_df_sign(struct bq_fg_chip *bq, u32 *chem_df_sign)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_CHEM_DF_SIGN, t_buf, 32) < 0)
		*chem_df_sign = 0;
	else
		*chem_df_sign = ((u32)t_buf[0] << 8) | t_buf[1];
}

static int fg_get_dcr_slope1(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_DCR_SLOPE, t_buf, 12) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[4], t_buf[5]);
}

static int fg_get_dcr_slope2(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_DCR_SLOPE, t_buf, 12) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[6], t_buf[7]);
}

static int fg_get_dcr_slope3(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_DCR_SLOPE, t_buf, 12) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[8], t_buf[9]);
}

static int fg_get_max_dsg_current(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[4], t_buf[5]);
}

static int fg_get_min_temp(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8) < 0)
		return 0;
	return t_buf[7];
}

static int fg_get_min_voltage(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_MIN_VOLTAGE, t_buf, 8) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[6], t_buf[7]);
}

static int fg_get_charge_absoc(struct bq_fg_chip *bq)
{
	u8 t_buf[BATTERY_RANDOM_LEN] = { 0 };

	if (fg_diag_mac_read(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 8) < 0)
		return 0;
	return fg_convert_u8_to_u16(t_buf[2], t_buf[3]);
}

static ssize_t fg_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t fg_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info fg_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CHIP_OK, chip_ok),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_VOL, vbatt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_PACK_VOL, pack_vbatt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CURRENT, ibatt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RSOC, rsoc),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TEMP, temp),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_CYCLE, cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_QMAX, qmax),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RM, rm),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FCC, charger_full),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_SOH, soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FCC_SOH, fcc_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FAST_CHARGE, fast_charge),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RESISTANCE_ID, resistance_id),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TSIM, tsim),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TAMBIENT, tambinet),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TREMQ, tremq),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TFULLCHGQ, tfullchgq),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AVERAGE_CURRENT, avercurrent),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_SEAL, seal),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DF_CHECK, df_check),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_VENDOR_ID, vendor),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_PACK_VENDOR_ID, pack_vendor),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CELL_VENDOR_ID, cell_vendor),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_EEPROM_VERSION, eeprom_version),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DESIGN_CAPACITY, design_capacity),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CALC_RVALUE, calc_rvalue),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AGED_FLAG, aged_flag),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_UI_SOH, ui_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_BATT_SN, batt_sn),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MANUFACTURING_DATE, manufacturing_date),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_FIRST_USAGE_DATE, first_usage_date),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_FAKE_FIRST_USAGE_DATE, fake_first_usage_date),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_SOH_NEW, soh_new),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_DOD_COUNT, dod_count),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LEVEL1, count_level1),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LEVEL2, count_level2),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LEVEL3, count_level3),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LOWTEMP, count_lt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CUTOFF_VOL, cutoff_vol),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_DEVICE_NAME, device_name),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_TEMP_OCCUR_TIME, max_temp_occur_time),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RUN_TIME, run_time),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_TEMP_TIME, max_temp_time),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TOTAL_FW_RUN_TIME, total_fw_runtime),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TEMP_MAX, temp_max),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TIME_HT, time_ht),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TIME_OT, time_ot),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_OVER_PEAK_FLAG, peak_flag),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_MONITOR_ISC, isc),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MONITOR_SOA, soa),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CURRENT_DEVIATION, current_deviation),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_POWER_DEVIATION, power_deviation),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AVERAGE_CURRENT, average_current),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AVERAGE_TEMPERATURE, average_temperature),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_START_LEARNING, start_learning),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_STOP_LEARNING, stop_learning),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_ACTUAL_POWER, action_power),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_LEARNING_POWER, learning_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_LEARNING_POWER_DEV, learning_power_dev),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_LEARNING_TIME_DEV, learning_time_dev),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_CONSTANT_POWER, constant_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REMAINING_TIME, remaining_time),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_REFERANCE_POWER, referance_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_NVT_REFERANCE_POWER, nvt_referance_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REFERANCE_CURRENT, referance_current),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_START_LEARNING_B, start_learning_b),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_STOP_LEARNING_B, stop_learning_b),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_LEARNING_POWER_B, learning_power_b),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_ACTUAL_POWER_B, action_power_b),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_LEARNING_POWER_DEV_B, learning_power_dev_b),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REL_SOH, rel_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REL_SOH_CYCLECOUNT, rel_soh_cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_EIS_SOH, eis_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_EIS_SOH_CYCLECOUNT, eis_soh_cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_QMAX_CYCLECOUNT, qmax_cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_BATT_USE_ENVIRRONMENT, batt_use_environment),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_LIFE_VOL, max_life_vol),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MIN_LIFE_VOL, min_life_vol),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_LIFE_TEMP, max_life_temp),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MIN_LIFE_TEMP, min_life_temp),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_OVER_VOL_DURATION, over_vol_duration),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FC, fc),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CSD_FLAG, csd_flag),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CSD_R1, csd_r1),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CSD_R2, csd_r2),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CUV_COUNT, cuv_count),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CUV_LAST_CYCLE, cuv_last_cycle),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_OCD_COUNT, ocd_count),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_OCD_LAST_CYCLE, ocd_last_cycle),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_HCUV_COUNT, hcuv_count),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_HCUV_LAST_CYCLE, hcuv_last_cycle),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_HOCD_COUNT, hocd_count),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_HOCD_LAST_CYCLE, hocd_last_cycle),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_HSCD_COUNT, hscd_count),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_HSCD_LAST_CYCLE, hscd_last_cycle),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CO_STATUS, co_status),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CO_AUTO_OPEN, co_auto_open),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CHEM_DF_SIGN, chem_df_sign),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DCR_SLOPE1, dcr_slope1),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DCR_SLOPE2, dcr_slope2),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DCR_SLOPE3, dcr_slope3),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_DSG_CURRENT, max_dsg_current),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MIN_TEMP, min_temp),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MIN_VOLTAGE, min_voltage),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CHARGE_ABSOC, charge_absoc),
};

#define FG_SYSFS_ATTRS_SIZE   ARRAY_SIZE(fg_sysfs_field_tbl)

static struct attribute *fg_sysfs_attrs[FG_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group fg_sysfs_attr_group = {
	.attrs = fg_sysfs_attrs,
};

static ssize_t fg_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;
	unsigned long rvalue;
	unsigned char data[64] = { 0 };
	ssize_t count = 0;
	struct bq_fg_chip *info = (struct bq_fg_chip *)dev_get_drvdata(dev);
	const char *strval;
	bool fc_val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		fg_sysfs_field_tbl, FG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case FG_IC_PROP_CHIP_OK:
		fg_get_chip_ok(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_VOL:
		fg_read_volt(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_PACK_VOL:
		fg_read_packvolt(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CURRENT:
		fg_read_current(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_RSOC:
		fg_read_rsoc(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TEMP:
		fg_read_temperature(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CYCLE:
		fg_read_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_QMAX:
		fg_read_qmax(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_RM:
		fg_read_rm(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FCC:
		fg_read_fcc(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_SOH:
		fg_read_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FCC_SOH:
		fg_read_fcc_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FAST_CHARGE:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fast_mode);
		break;
	case FG_IC_PROP_RESISTANCE_ID:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->resistance_id);
		break;
	case FG_IC_PROP_TSIM:
		fg_read_tsim(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TAMBIENT:
		fg_read_tambient(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TREMQ:
		fg_read_tremq(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TFULLCHGQ:
		fg_read_tfullchgq(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_AVERCURRENT:
		fg_read_avercurrent(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_SEAL:
		fg_get_seal(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_DF_CHECK:
		fg_read_df_check(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_VENDOR_ID:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fg_vendor);
		break;
	case FG_IC_PROP_PACK_VENDOR_ID:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->pack_vendor);
		break;
	case FG_IC_PROP_CELL_VENDOR_ID:
		count = scnprintf(buf, PAGE_SIZE, "%s\n", info->cell_name);
		break;
	case FG_IC_PROP_EEPROM_VERSION:
		count = scnprintf(buf, PAGE_SIZE, "%c\n", info->eeprom_version);
		break;
	case FG_IC_PROP_MAX_TEMP_OCCUR_TIME:
		fg_read_max_temp_occur_time(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_DESIGN_CAPACITY:
		fg_read_dc(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CALC_RVALUE:
		rvalue = fg_get_calc_rvalue(info);
		count = scnprintf(buf, PAGE_SIZE, "%lu\n", rvalue);
		break;
	case FG_IC_PROP_AGED_FLAG:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->aged_flag);
		break;
	case FG_IC_PROP_UI_SOH:
		if (fg_mac_read_block(info, FG_MAC_CMD_UI_SOH, data, 11) < 0)
			return -1;
		count = scnprintf(buf, PAGE_SIZE,
			"%u %u %u %u %u %u %u %u %u %u %u\n",
			data[0], data[1], data[2], data[3], data[4], data[5],
			data[6], data[7], data[8], data[9], data[10]);
		break;
	case FG_IC_PROP_BATT_SN:
		if (fg_mac_read_block(info, FG_MAC_CMD_BATT_SN, data, 32) < 0)
			return -1;
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_MANUFACTURING_DATE:
		if (fg_read_manufacturing_date(info, data))
			return -1;
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_FIRST_USAGE_DATE:
		fg_read_first_usage_date(info, data);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_FAKE_FIRST_USAGE_DATE:
		if (fg_read_first_usage_date(info, data))
			return -1;
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_SOH_NEW:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->ui_soh);
		break;
	case FG_IC_PROP_DOD_COUNT:
		val = fg_get_dod_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LEVEL1:
		fg_get_count_level1(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LEVEL2:
		fg_get_count_level2(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LEVEL3:
		fg_get_count_level3(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LOWTEMP:
		fg_get_count_lowtemp(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CUTOFF_VOL:
		fg_get_cutoff_voltage(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_DEVICE_NAME:
		strval = fg_get_device_name(info);
		count = scnprintf(buf, PAGE_SIZE, "%s", strval);
		break;
	case FG_IC_PROP_RUN_TIME:
		fg_read_run_time(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MAX_TEMP_TIME:
		fg_read_max_temp_time(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TOTAL_FW_RUN_TIME:
		fg_read_total_fw_runtime(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TEMP_MAX:
		fg_get_one_temp_max(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TIME_HT:
		fg_read_time_ht(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TIME_OT:
		fg_read_time_ot(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_OVER_PEAK_FLAG:
		val = fg_get_over_peak_flag(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MONITOR_ISC:
		fg_get_isc_alert_level(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MONITOR_SOA:
		fg_get_soa_alert_level(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CURRENT_DEVIATION:
		val = fg_get_current_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_POWER_DEVIATION:
		val = fg_get_power_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_AVERAGE_CURRENT:
		val = fg_get_average_current(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_AVERAGE_TEMPERATURE:
		val = fg_get_average_temp(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_START_LEARNING:
		val = fg_get_start_learning(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_STOP_LEARNING:
		val = fg_get_stop_learning(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_ACTUAL_POWER:
		val = fg_get_actual_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER:
		val = fg_get_learning_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER_DEV:
		val = fg_get_learning_power_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_TIME_DEV:
		val = fg_get_learning_time_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CONSTANT_POWER:
		val = fg_get_constant_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REMAINING_TIME:
		val = fg_get_remaining_time(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REFERANCE_POWER:
		val = fg_get_referance_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_NVT_REFERANCE_POWER:
		val = fg_get_nvt_referance_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REFERANCE_CURRENT:
		val = fg_get_referance_current(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_START_LEARNING_B:
		val = fg_get_start_learning_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_STOP_LEARNING_B:
		val = fg_get_stop_learning_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER_B:
		val = fg_get_learning_power_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_ACTUAL_POWER_B:
		val = fg_get_actual_power_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER_DEV_B:
		val = fg_get_learning_power_dev_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REL_SOH:
		fg_get_rel_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REL_SOH_CYCLECOUNT:
		fg_get_rel_soh_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_EIS_SOH:
		fg_get_eis_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_EIS_SOH_CYCLECOUNT:
		fg_get_eis_soh_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_QMAX_CYCLECOUNT:
		fg_get_qmax_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_BATT_USE_ENVIRRONMENT:
		if (fg_get_batt_use_environment(info, data))
			return -1;
		count = scnprintf(buf, PAGE_SIZE,
			"%u %u %u %u %u %u %u %u %u %u "
			"%u %u %u %u %u %u %u %u %u %u "
			"%u %u %u %u %u %u %u %u %u %u "
			"%u %u %u %u %u %u %u %u\n",
			data[0], data[1], data[2], data[3], data[4], data[5],
			data[6], data[7], data[8], data[9], data[10], data[11],
			data[12], data[13], data[14], data[15], data[16], data[17],
			data[18], data[19], data[20], data[21], data[22], data[23],
			data[24], data[25], data[26], data[27], data[28], data[29],
			data[30], data[31], data[32], data[33], data[34], data[35],
			data[36], data[37]);
		break;
	case FG_IC_PROP_MAX_LIFE_VOL:
		fg_get_max_life_vol(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MIN_LIFE_VOL:
		fg_get_min_life_vol(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MAX_LIFE_TEMP:
		fg_get_max_life_temp(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MIN_LIFE_TEMP:
		fg_get_min_life_temp(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_OVER_VOL_DURATION:
		fg_get_over_vol_duration(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FC:
		fg_read_one_fc(info, &fc_val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fc_val);
		break;
	case FG_IC_PROP_CSD_FLAG:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_csd_flag(info));
		break;
	case FG_IC_PROP_CSD_R1:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_csd_r1(info));
		break;
	case FG_IC_PROP_CSD_R2:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_csd_r2(info));
		break;
	case FG_IC_PROP_CUV_COUNT:
		fg_get_cuv_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->cuv_count);
		break;
	case FG_IC_PROP_CUV_LAST_CYCLE:
		fg_get_cuv_last_cycle(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->cuv_last_cycle);
		break;
	case FG_IC_PROP_OCD_COUNT:
		fg_get_ocd_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->ocd_count);
		break;
	case FG_IC_PROP_OCD_LAST_CYCLE:
		fg_get_ocd_last_cycle(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->ocd_last_cycle);
		break;
	case FG_IC_PROP_HCUV_COUNT:
		fg_get_hcuv_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->hcuv_count);
		break;
	case FG_IC_PROP_HCUV_LAST_CYCLE:
		fg_get_hcuv_last_cycle(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->hcuv_last_cycle);
		break;
	case FG_IC_PROP_HOCD_COUNT:
		fg_get_hocd_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->hocd_count);
		break;
	case FG_IC_PROP_HOCD_LAST_CYCLE:
		fg_get_hocd_last_cycle(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->hocd_last_cycle);
		break;
	case FG_IC_PROP_HSCD_COUNT:
		fg_get_hscd_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->hscd_count);
		break;
	case FG_IC_PROP_HSCD_LAST_CYCLE:
		fg_get_hscd_last_cycle(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->hscd_last_cycle);
		break;
	case FG_IC_PROP_CO_STATUS: {
		u32 co_status = 0;

		fg_read_co_status(info, &co_status);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", co_status);
		break;
	}
	case FG_IC_PROP_CO_AUTO_OPEN: {
		u32 co_auto_open = 0;

		fg_read_co_auto_open(info, &co_auto_open);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", co_auto_open);
		break;
	}
	case FG_IC_PROP_CHEM_DF_SIGN: {
		u32 chem_df_sign = 0;

		fg_read_chem_df_sign(info, &chem_df_sign);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", chem_df_sign);
		break;
	}
	case FG_IC_PROP_DCR_SLOPE1:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_dcr_slope1(info));
		break;
	case FG_IC_PROP_DCR_SLOPE2:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_dcr_slope2(info));
		break;
	case FG_IC_PROP_DCR_SLOPE3:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_dcr_slope3(info));
		break;
	case FG_IC_PROP_MAX_DSG_CURRENT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n",
				  fg_get_max_dsg_current(info));
		break;
	case FG_IC_PROP_MIN_TEMP:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_min_temp(info));
		break;
	case FG_IC_PROP_MIN_VOLTAGE:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_min_voltage(info));
		break;
	case FG_IC_PROP_CHARGE_ABSOC:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fg_get_charge_absoc(info));
		break;
	default:
		break;
	}

	return count;
}

static ssize_t fg_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct bq_fg_chip *info = (struct bq_fg_chip *)dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		fg_sysfs_field_tbl, FG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	(void)kstrtoint(buf, 10, &val);

	switch (attr_info->sysfs_attr_name) {
	case FG_IC_PROP_SEAL:
		fg_set_seal(info, val);
		break;
	case FG_IC_PROP_RSOC:
		break;
	case FG_IC_PROP_CYCLE:
		fg_set_cyclecount(info, val);
		break;
	case FG_IC_PROP_COUNT_LEVEL1:
		info->fake_count_level1 = val;
		break;
	case FG_IC_PROP_COUNT_LEVEL2:
		info->fake_count_level2 = val;
		break;
	case FG_IC_PROP_COUNT_LEVEL3:
		info->fake_count_level3 = val;
		break;
	case FG_IC_PROP_COUNT_LOWTEMP:
		info->count_lowtemp = val;
		break;
	case FG_IC_PROP_DEVICE_NAME:
		strscpy(info->fake_device_name, buf, count + 1);
		break;
	case FG_IC_PROP_MONITOR_ISC:
		fg_set_isc_alert_level(info, val);
		break;
	case FG_IC_PROP_UI_SOH:
		fg_store_ui_soh(info, buf, count);
		break;
	case FG_IC_PROP_FIRST_USAGE_DATE:
		fg_write_first_usage_date(info, buf, count);
		break;
	case FG_IC_PROP_FAKE_FIRST_USAGE_DATE:
		fg_write_fake_first_usage_date(info, buf, count);
		break;
	case FG_IC_PROP_START_LEARNING:
		fg_set_start_learning(info);
		break;
	case FG_IC_PROP_STOP_LEARNING:
		fg_set_stop_learning(info);
		break;
	case FG_IC_PROP_LEARNING_POWER:
		fg_set_learning_power(info, val);
		break;
	case FG_IC_PROP_CONSTANT_POWER:
		fg_set_constant_power(info, val);
		break;
	case FG_IC_PROP_REFERANCE_POWER:
		fg_set_referance_power(info, val);
		break;
	case FG_IC_PROP_START_LEARNING_B:
		fg_set_start_learning_b(info);
		break;
	case FG_IC_PROP_STOP_LEARNING_B:
		fg_set_stop_learning_b(info);
		break;
	case FG_IC_PROP_LEARNING_POWER_B:
		fg_set_learning_power_b(info, val);
		break;
	default:
		break;
	}

	return count;
}

static int fg_sysfs_create_group(struct bq_fg_chip *info)
{
	mca_sysfs_init_attrs(fg_sysfs_attrs, fg_sysfs_field_tbl, FG_SYSFS_ATTRS_SIZE);
	info->sysfs_dev = mca_sysfs_create_group("xm_power",
		info->dev_role == FG_IC_MASTER ? "fg_master" : "fg_slave", &fg_sysfs_attr_group);
	if (!info->sysfs_dev)
		return -ENOMEM;
	dev_set_drvdata(info->sysfs_dev, info);
	return 0;
}

static void fg_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_group("xm_power", dev, &fg_sysfs_attr_group);
}

static const struct of_device_id bq_fg_match_table[] = {
	{
		.compatible = "ti,bq27z561_master",
		.data = &bq_mode_data[BQ27Z561_MASTER],
	},
	{
		.compatible = "ti,bq27z561_slave",
		.data = &bq_mode_data[BQ27Z561_SLAVE],
	},
	{
		.compatible = "ti,bq28z610",
		.data = &bq_mode_data[BQ28Z610],
	},
	{},
};
MODULE_DEVICE_TABLE(of, bq_fg_match_table);

static struct i2c_device_id bq_fg_id[] = {
	{ "bq27z561_master", BQ27Z561_MASTER },
	{ "bq27z561_slave", BQ27Z561_SLAVE },
	{ "bq28z610", BQ28Z610 },
	{},
};

static int fg_get_pack_vendor(void *data, int *vendor)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	*vendor = info->pack_vendor;
	return 0;
}

static int fg_get_one_average_current(void *data, int *val)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	*val = fg_get_average_current(info);
	return 0;
}

static struct fuelguage_ic_ops g_bq_fg_ops = {
	.fg_ic_probe_ok = fg_read_one_probe_ok,
	.fg_ic_get_batt_info = fg_read_batt_info,
	.fg_ic_get_rsoc = fg_read_one_rsoc,
	.fg_ic_get_curr = fg_read_one_current,
	.fg_ic_get_volt = fg_read_one_volt,
	.fg_ic_set_temp = fg_set_temperature,
	.fg_ic_get_temp = fg_read_one_temperature,
	.fg_ic_get_original_temp = fg_read_one_original_temperature,
	.fg_ic_get_charge_status = fg_read_one_status,
	.fg_ic_get_rm = fg_read_one_rm,
	.fg_ic_get_fastcharge = fg_get_one_fastcharge_mode,
	.fg_ic_set_fastcharge = fg_set_one_fastcharge_mode,
	.fg_ic_get_chg_vol = fg_read_one_charging_voltage,
	.fg_ic_get_chip_ok = fg_get_one_chip_ok,
	.fg_ic_get_cyclecount = fg_read_one_cyclecount,
	.fg_ic_get_tte = fg_read_one_tte,
	.fg_ic_get_ttf = fg_read_one_ttf,
	.fg_ic_get_fcc = fg_read_one_fcc,
	.fg_ic_get_full_design = fg_read_one_dc,
	.fg_ic_get_decimal_rate = fg_get_one_soc_decimal_rate,
	.fg_ic_get_decimal = fg_get_one_soc_decimal,
	.fg_ic_get_soh = fg_read_one_soh,
	.fg_ic_get_temp_max = fg_get_one_temp_max,
	.fg_ic_get_time_ot = fg_get_one_time_ot,
	.fg_ic_get_batt_cell_info = fg_get_one_batt_cell_info,
	.fg_ic_set_verify_digest = fg_set_verify_digest,
	.fg_ic_get_verify_digest = fg_get_verify_digest,
	.fg_ic_set_authentic = fg_set_authentic,
	.fg_ic_get_authentic = fg_get_authentic,
	.fg_ic_get_error_state = fg_get_error_state,
	.fg_ic_get_cutoff_voltage = fg_get_one_cutoff_voltage,
	.fg_ic_set_cutoff_voltage = fg_set_one_cutoff_voltage,
	.fg_ic_get_dod_count = fg_get_one_dod_count,
	.fg_ic_get_count_level1 = fg_get_one_count_level1,
	.fg_ic_get_count_level2 = fg_get_one_count_level2,
	.fg_ic_get_count_level3 = fg_get_one_count_level3,
	.fg_ic_get_count_lowtemp = fg_get_one_count_lowtemp,
	.fg_ic_get_adapt_power = fg_get_one_adapt_power,
	.fg_ic_get_pack_vendor = fg_get_pack_vendor,
	.fg_ic_get_average_current = fg_get_one_average_current,
	.fg_ic_get_aged_flag = fg_get_aged_flag,
	.fg_ic_get_isc_alert_level = fg_get_one_isc_alert_level,
	.fg_ic_get_soa_alert_level = fg_get_one_soa_alert_level,
	.fg_ic_update_fw = fg_update_data_work,
	.fg_ic_get_raw_soc = fg_get_one_raw_soc,
	.fg_ic_get_device_name = fg_get_one_device_name,
	.fg_ic_get_temp_min = fg_get_one_temp_min,
	.fg_ic_set_force_report_full = fg_set_force_report_full,
	.fg_ic_set_clear_count_data = fg_set_one_clear_count_data,
	.fg_ic_get_fc = fg_read_one_fc,
	.fg_ic_set_co = fg_set_one_co,
	.fg_ic_get_ota_update_flag = fg_get_ota_update_flag,
	.fg_ic_get_calibration_ffc_iterm = fg_get_one_calibration_ffc_iterm,
	.fg_ic_get_real_supplement_energy = fg_get_one_real_supplement_energy,
	.fg_ic_get_calibration_charge_energy = fg_get_one_calibration_charge_energy,
	.fg_ic_fl4p0_enable_check = fg_fl4p0_enable_check,
	.fg_ic_get_ui_soh = fg_get_ui_soh,
	.fg_ic_get_calc_rvalue = fg_get_one_calc_rvalue,
	.fg_ic_get_batt_abnormal_info = fg_get_batt_abnormal_info,
	.fg_ic_get_first_usage_date = fg_get_one_first_usage_date,
	.fg_ic_get_manufacturing_date = fg_get_one_manufacturing_date,
	.fg_ic_set_first_usage_date = fg_set_one_first_usage_date,
};

static int bq27z561_dump_log_head(void *data, char *buf, int size)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	if (!data)
		return 0;

	if (bq->dev_role == 0)
		return snprintf(buf, size, "fg_type Cell      RSoc Soh Volt Curr    RM    FCC   Qmax  Temp SOA ");
	else
		return snprintf(buf, size, "fg_type1 Cell1     RSoc1 Soh1 Volt1 Curr1   RM1   FCC1  Qmax1 Temp1 SOA1 ");
}

static int bq27z561_dump_log_context(void *data, char *buf, int size)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	int rsoc, soh, volt, curr, rm, fcc, qmax, temp, soa;

	if (!data)
		return 0;

	fg_read_rsoc(bq, &rsoc);
	fg_read_soh(bq, &soh);
	fg_read_volt(bq, &volt);
	fg_read_current(bq, &curr);
	fg_read_rm(bq, &rm);
	fg_read_fcc(bq, &fcc);
	fg_read_qmax(bq, &qmax);
	fg_read_temperature(bq, &temp);
	fg_get_soa_alert_level(bq, &soa);

	if (bq->dev_role == 0)
		return snprintf(buf, size, "%-8d%-10s%-5d%-4d%-5d%-8d%-6d%-6d%-6d%-5d%-4d",
			bq->fg_vendor, bq->cell_name, rsoc, soh, volt, curr / 1000,
			rm / 1000, fcc / 1000, qmax / 1000, temp, soa);
	else
		return snprintf(buf, size, "%-9d%-10s%-6d%-5d%-6d%-8d%-6d%-6d%-6d%-6d%-5d",
			bq->fg_vendor, bq->cell_name, rsoc, soh, volt, curr / 1000,
			rm / 1000, fcc / 1000, qmax / 1000, temp, soa);
}

static struct mca_log_charge_log_ops g_bq27z516_log_ops = {
	.dump_log_head = bq27z561_dump_log_head,
	.dump_log_context = bq27z561_dump_log_context,
};

static void fg_set_force_sim_soh(struct bq_fg_chip *bq)
{
	u8 wbuf[2] = { 0 };
	u16 flag;
	int ret, i;

	for (i = 3; i > 0; i--) {
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FORCE_SIM_SOH, wbuf, 0);
		if (ret < 0) {
			mca_log_info("could not write 0x56 to 0x3E %d\n", ret);
			return;
		}
		msleep(30);
		flag = 0;
		if (fg_diag_mac_read(bq, FG_MAC_CMD_MIXDATARECORD1,
				     (u8 *)&flag, 2) < 0) {
			mca_log_info("failed to get 0x2001 data\n");
			return;
		}
		mca_log_info("flag = %d, try_conut = %d\n", flag & 4, i - 1);
		if ((flag >> 2) & 1)
			return;
	}
}

static void fg_force_sim_soh_if_needed(struct bq_fg_chip *bq)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	u8 name[8] = { 0 };
	char dev5[6] = { 0 };

	if (!hwid || hwid->platform_version != 3 ||
	    hwid->country_version != 0) {
		mca_log_err("not miro CN device, none to do!\n");
		return;
	}
	if (fg_diag_mac_read(bq, FG_MAC_CMD_DEVICE_NAME, name, 8) < 0) {
		mca_log_err("could not read device_name\n");
		return;
	}
	memcpy(dev5, name, 5);
	if (!strcmp("3XM31", dev5))
		fg_set_force_sim_soh(bq);
	else
		mca_log_err("not CN NVT device, none to do!\n");
}

static int bq_fg_probe(struct i2c_client *client)
{
	int ret;
	struct bq_fg_chip *bq;
	u8 data[3];

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	bq->ui_soh = 100;
	bq->fake_temp = FG_FAKE_TEMP_NONE;
	bq->batt_fcc = FG_RM_FCC_DEFAULT_TH * 1000;
	i2c_set_clientdata(client, bq);
	mutex_init(&bq->i2c_rw_lock);
	mutex_init(&bq->data_lock);
	atomic_set(&bq->pm_suspend, 0);
	atomic_set(&bq->digest_in_process, 0);
	bq->update_fw_status = FG_FW_INIT;
	strcpy(bq->fake_device_name, "");

	ret = bq_parse_dt(bq);
	if (ret)
		return ret;
	memcpy(bq->regs, bq27z561_regs, NUM_REGS);

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, data, sizeof(data));
	if (ret)
		return dev_err_probe(bq->dev, ret, "fuel gauge identity read failed\n");

	device_init_wakeup(bq->dev, 1);
	fg_set_term_config(bq);
	fg_get_manufacture_data(bq);
	fg_set_fastcharge_mode(bq, false);
	fg_read_cell_vendor(bq);
	fg_read_pack_vendor(bq);
	fg_read_vendor(bq);
	fg_read_fw_version(bq);
	fg_read_eeprom_version(bq);
	nfg1000_boot_app_judge(bq);

	ret = platform_fg_ic_ops_register(bq->dev_role, bq, &g_bq_fg_ops);
	if (ret) {
		mca_log_err("reg ops fail, role = %u\n", bq->dev_role);
		goto disable_wakeup;
	}

	mca_log_err("FuelGaugw probe successfully, role = %u\n", bq->dev_role);

	fg_force_sim_soh_if_needed(bq);
	ret = fg_sysfs_create_group(bq);
	if (ret)
		goto unregister_ops;
	mca_log_charge_log_register(bq->dev_role == FG_IC_MASTER ?
		MCA_CHARGE_LOG_ID_FG_MASTER_IC : MCA_CHARGE_LOG_ID_FG_SLAVE_IC,
		&g_bq27z516_log_ops, bq);
	return 0;
unregister_ops:
	platform_fg_ic_ops_unregister(bq->dev_role, bq);
disable_wakeup:
	device_init_wakeup(bq->dev, false);
	return ret;
}

static int bq_fg_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	if (!bq)
		return 0;

	mca_log_info("suspend\n");
	atomic_set(&bq->pm_suspend, 1); /* 1: set atomic pm suspend flag */

	return 0;
}

static int bq_fg_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	if (!bq)
		return 0;

	mca_log_info("resume\n");
	atomic_set(&bq->pm_suspend, 0); /* 0: clear atomic pm suspend flag */
	if (bq->fg_vendor == NFG1000A || bq->fg_vendor == NFG1000B)
		nfg1000_ota_update_check(bq);

	return 0;
}

static void bq_fg_remove(struct i2c_client *client)
{
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	mca_log_charge_log_unregister(bq->dev_role == FG_IC_MASTER ?
		MCA_CHARGE_LOG_ID_FG_MASTER_IC : MCA_CHARGE_LOG_ID_FG_SLAVE_IC, bq);
	fg_sysfs_remove_group(bq->sysfs_dev);
	platform_fg_ic_ops_unregister(bq->dev_role, bq);
	device_init_wakeup(bq->dev, false);
}

static void bq_fg_shutdown(struct i2c_client *client)
{
	mca_log_info("bq fuel gauge driver shutdown!\n");
}

static const struct dev_pm_ops bq_fg_pm_ops = {
	.resume		= bq_fg_resume,
	.suspend	= bq_fg_suspend,
};

MODULE_DEVICE_TABLE(i2c, bq_fg_id);

static struct i2c_driver bq_fg_driver = {
	.driver	= {
		.name   = "bq27z561",
		.owner  = THIS_MODULE,
		.of_match_table = bq_fg_match_table,
		.pm	 = &bq_fg_pm_ops,
	},
	.id_table	   = bq_fg_id,

	.probe		  = bq_fg_probe,
	.remove		= bq_fg_remove,
	.shutdown	= bq_fg_shutdown,

};

module_i2c_driver(bq_fg_driver);

MODULE_DESCRIPTION("TI BQ27Z561 Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");

