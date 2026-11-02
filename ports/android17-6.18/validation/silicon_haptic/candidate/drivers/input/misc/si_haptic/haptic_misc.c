/*
 *  Silicon Integrated Co., Ltd haptic sih688x haptic misc file
 *
 *  Copyright (c) 2021 kugua <canzhen.peng@si-in.com>
 *  Copyright (c) 2021 tianchi <tianchi.zheng@si-in.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include <linux/vmalloc.h>
#include <linux/pm_qos.h>
#include <linux/mm.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <linux/errno.h>
#include <linux/mman.h>

#include "haptic.h"
#include "haptic_mid.h"
#include "haptic_regmap.h"
#include "haptic_misc.h"
#include "sih688x_reg.h"

#define STREAM_RTP_MODE

static void haptic_clean_buf(sih_haptic_t *sih_haptic, int status)
{
	mmap_buf_format_t *opbuf = sih_haptic->stream_para.start_buf;
	int i;

	for (i = 0; i < IOCTL_MMAP_BUF_SUM; i++) {
		opbuf->status = status;
		opbuf = sih_haptic->stream_para.start_buf + (i + 1) % IOCTL_MMAP_BUF_SUM;
	}
}

static inline unsigned long int sih_get_sys_msecs(void)
{
	ktime_t get_time;
	unsigned long int get_time_ms;

	get_time = ktime_get();
	get_time_ms = ktime_to_ms(get_time);

	return get_time_ms;
}

static void stream_work_proc(struct work_struct *work)
{
	sih_haptic_t *sih_haptic =
		container_of(work, sih_haptic_t, stream_para.stream_work);
	mmap_buf_format_t *opbuf = sih_haptic->stream_para.start_buf;
	uint32_t count = IOCTL_WAIT_BUFF_VALID_MAX_TRY;
	unsigned int index = 0;
	unsigned int length;
	uint8_t reg_val;
	unsigned int write_start;

	while (true && count--) {
		if (!sih_haptic->stream_para.stream_mode)
			return;
		if (opbuf->status == MMAP_BUF_DATA_VALID) {
			mutex_lock(&sih_haptic->lock);
			sih_haptic->hp_func->set_play_mode(sih_haptic,
							   SIH_RTP_MODE);
			sih_haptic->hp_func->set_rtp_aei(sih_haptic, true);
			sih_haptic->hp_func->clear_interrupt_state(sih_haptic);
			sih_haptic->hp_func->play_go(sih_haptic, true);
			mutex_unlock(&sih_haptic->lock);
			break;
		}
		msleep(20);
	}
	write_start = sih_get_sys_msecs();
	reg_val = SIH_SYSSST_BIT_FIFO_AE;
	while (1) {
		if (!sih_haptic->stream_para.stream_mode)
			break;
		if (sih_get_sys_msecs() > (write_start + 800)) {
			hp_err("%s:failed!endless loop\n", __func__);
			break;
		}

		if ((reg_val & SIH_SYSSST_BIT_STANDBY) ||
		    (sih_haptic->stream_para.done_flag)) {
			hp_err("%s:buff status:0x%02x length:%d\n", __func__,
			       opbuf->status, opbuf->length);
			break;
		} else if (opbuf->status == MMAP_BUF_DATA_VALID &&
			   ((reg_val & SIH_SYSSST_BIT_FIFO_AE) ==
			    SIH_SYSSST_BIT_FIFO_AE)) {
			length = READ_ONCE(opbuf->length);
			if (length > sizeof(opbuf->data))
				break;
			haptic_regmap_write(sih_haptic->regmapp.regmapping,
					    SIH688X_REG_RTPDATA, length,
					    opbuf->data);

			hp_info("%s:writes length:%d\n", __func__,
				opbuf->length);
			memset(opbuf->data, 0, length);
			opbuf->status = MMAP_BUF_DATA_INVALID;
			opbuf->length = 0;
			index = (index + 1) % IOCTL_MMAP_BUF_SUM;
			opbuf = sih_haptic->stream_para.start_buf + index;
			write_start = sih_get_sys_msecs();
		} else {
			usleep_range(1000, 2000);
		}
		haptic_regmap_read(sih_haptic->regmapp.regmapping,
				   SIH688X_REG_SYSSST, SIH_I2C_OPERA_BYTE_ONE,
				   &reg_val);
	}
	sih_haptic->hp_func->set_rtp_aei(sih_haptic, false);
	sih_haptic->stream_para.stream_mode = false;
}

static void sih_vma_open(struct vm_area_struct *vma)
{
	get_page(vma->vm_private_data);
}

static void sih_vma_close(struct vm_area_struct *vma)
{
	put_page(vma->vm_private_data);
}

static const struct vm_operations_struct sih_vm_ops = {
	.open = sih_vma_open,
	.close = sih_vma_close,
};

static int memo_file_mmap(struct file *filp, struct vm_area_struct *vma)
{
	sih_haptic_t *sih_haptic = sih_lock_global();
	struct page *page;
	int ret;

	if (!sih_haptic)
		return -ENODEV;
	if (vma->vm_pgoff || !(vma->vm_flags & VM_SHARED) ||
	    (vma->vm_flags & VM_EXEC) ||
	    vma->vm_end - vma->vm_start != PAGE_SIZE << IOCTL_MMAP_PAGE_ORDER) {
		ret = -EINVAL;
		goto out;
	}
	page = virt_to_page(sih_haptic->stream_para.start_buf);
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	vm_flags_clear(vma, VM_MAYEXEC);
	ret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(page),
			      vma->vm_end - vma->vm_start, vma->vm_page_prot);
	if (!ret) {
		vma->vm_private_data = page;
		vma->vm_ops = &sih_vm_ops;
		sih_vma_open(vma);
	}
out:
	sih_unlock_global();
	return ret;
}

static int memo_file_open(struct inode *inode, struct file *file)
{
	sih_haptic_t *sih_haptic = sih_lock_global();

	if (!sih_haptic)
		return -ENODEV;
	sih_unlock_global();
	return 0;
}

static int memo_file_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t memo_file_read(struct file *filp, char __user *buff, size_t len,
			      loff_t *offset)
{
	hp_info("memo file read\n");
	return len;
}

static ssize_t memo_file_write(struct file *filp, const char __user *buff,
			       size_t len, loff_t *offset)
{
	hp_info("memo file write\n");
	return len;
}

static long memo_file_unlocked_ioctl(struct file *file, unsigned int cmd,
				     unsigned long arg)
{
	sih_haptic_t *sih_haptic = sih_lock_global();
	uint32_t tmp;
	int ret = 0;

	if (!sih_haptic)
		return -ENODEV;
	if (cmd == IOCTL_STREAM_MODE || cmd == IOCTL_STOP_MODE) {
		WRITE_ONCE(sih_haptic->stream_para.stream_mode, false);
		cancel_work_sync(&sih_haptic->stream_para.stream_work);
	}
	hp_info("cmd:%u arg:%lu\n", cmd, arg);
	mutex_lock(&sih_haptic->lock);
	switch (cmd) {
	case IOCTL_GET_HWINFO:
		tmp = IOCTL_HWINFO;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(uint32_t)))
			ret = -EFAULT;
		break;
	case IOCTL_MODE_RTP_MODE:
		sih_haptic->hp_func->stop(sih_haptic);
		if (copy_from_user(sih_haptic->stream_para.rtp_ptr,
				   (void __user *)arg,
				   IOCTL_MMAP_BUF_SIZE * IOCTL_MMAP_BUF_SUM)) {
			ret = -EFAULT;
			break;
		}
		tmp = *((uint32_t *)sih_haptic->stream_para.rtp_ptr);
		if (tmp > (IOCTL_MMAP_BUF_SIZE * IOCTL_MMAP_BUF_SUM - 4)) {
			hp_info("%s:rtp mode data len error %d\n", __func__,
				tmp);
			ret = -EINVAL;
			break;
		}
		sih_haptic->hp_func->set_play_mode(sih_haptic, SIH_RTP_MODE);
		sih_haptic->hp_func->play_go(sih_haptic, true);
		usleep_range(2000, 2500);
		haptic_regmap_write(sih_haptic->regmapp.regmapping,
				    SIH688X_REG_RTPDATA, tmp,
				    &sih_haptic->stream_para.rtp_ptr[4]);
		break;
	case IOCTL_OFF_MODE:
		break;
	case IOCTL_GET_F0:
		tmp = sih_haptic->detect.tracking_f0;
		if (copy_to_user((void __user *)arg, &tmp, sizeof(uint32_t)))
			ret = -EFAULT;
		break;
	case IOCTL_SETTING_GAIN:
		hp_info("%s:gain mode enter\n", __func__);
		if (arg > SIH_HAPTIC_GAIN_LIMIT)
			arg = SIH_HAPTIC_GAIN_LIMIT;
		sih_haptic->hp_func->set_gain(sih_haptic, arg);
		break;
	case IOCTL_STREAM_MODE:
		hp_info("%s:stream mode enter\n", __func__);
		sih_haptic->stream_para.stream_mode = false;
		haptic_clean_buf(sih_haptic, MMAP_BUF_DATA_INVALID);
		sih_haptic->hp_func->stop(sih_haptic);
		sih_haptic->stream_para.done_flag = false;
		sih_haptic->stream_para.stream_mode = true;
		schedule_work(&sih_haptic->stream_para.stream_work);
		break;
	case IOCTL_STOP_MODE:
		hp_info("%s:stop mode enter\n", __func__);
		sih_haptic->stream_para.done_flag = true;
		sih_haptic->stream_para.stream_mode = false;
		sih_haptic->chip_ipara.state = SIH_STANDBY_MODE;
		sih_haptic->chip_ipara.play_mode = SIH_IDLE_MODE;
		sih_haptic->hp_func->set_rtp_aei(sih_haptic, false);
		sih_haptic->hp_func->stop(sih_haptic);
		sih_haptic->hp_func->clear_interrupt_state(sih_haptic);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	mutex_unlock(&sih_haptic->lock);
	sih_unlock_global();
	return ret;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = memo_file_read,
	.write = memo_file_write,
	.mmap = memo_file_mmap,
	.unlocked_ioctl = memo_file_unlocked_ioctl,
	.open = memo_file_open,
	.release = memo_file_release,
};

static struct miscdevice sih_haptic_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = SIH_DEV_HAPTIC_NAME,
	.fops = &fops,
};

static int sih_stream_rtp_work_init(sih_haptic_t *sih_haptic)
{
#ifdef STREAM_RTP_MODE
	sih_haptic->stream_para.rtp_ptr =
		kzalloc(IOCTL_MMAP_BUF_SIZE * IOCTL_MMAP_BUF_SUM, GFP_KERNEL);
	if (sih_haptic->stream_para.rtp_ptr == NULL) {
		hp_err("malloc rtp memory failed\n");
		return -ENOMEM;
	}

	sih_haptic->stream_para.start_buf =
		(mmap_buf_format_t *)__get_free_pages(GFP_KERNEL | __GFP_ZERO | __GFP_COMP,
						      IOCTL_MMAP_PAGE_ORDER);
	if (sih_haptic->stream_para.start_buf == NULL) {
		hp_err("error get page failed\n");
		kfree(sih_haptic->stream_para.rtp_ptr);
		sih_haptic->stream_para.rtp_ptr = NULL;
		return -ENOMEM;
	}
	{
		mmap_buf_format_t *temp;
		uint32_t i;

		temp = sih_haptic->stream_para.start_buf;
		for (i = 1; i < IOCTL_MMAP_BUF_SUM; i++) {
			temp->kernel_next =
				(sih_haptic->stream_para.start_buf + i);
			temp = temp->kernel_next;
		}
		temp->kernel_next = sih_haptic->stream_para.start_buf;
	}

	INIT_WORK(&sih_haptic->stream_para.stream_work, stream_work_proc);
	sih_haptic->stream_para.done_flag = true;
	sih_haptic->stream_para.stream_mode = false;

	hp_info("%s:init ok\n", __func__);
#endif

	return 0;
}

static void sih_stream_rtp_work_release(sih_haptic_t *sih_haptic)
{
#ifdef STREAM_RTP_MODE
	WRITE_ONCE(sih_haptic->stream_para.stream_mode, false);
	cancel_work_sync(&sih_haptic->stream_para.stream_work);
	kfree(sih_haptic->stream_para.rtp_ptr);
	sih_haptic->stream_para.rtp_ptr = NULL;
	free_pages((unsigned long)sih_haptic->stream_para.start_buf,
		   IOCTL_MMAP_PAGE_ORDER);
	sih_haptic->stream_para.start_buf = NULL;
#endif
}

static bool sih_is_stream_mode(sih_haptic_t *sih_haptic)
{
#ifdef STREAM_RTP_MODE
	if (sih_haptic->stream_para.stream_mode)
		return true;
#endif
	return false;
}

haptic_stream_func_t stream_play_func = {
	.is_stream_mode = sih_is_stream_mode,
	.stream_rtp_work_init = sih_stream_rtp_work_init,
	.stream_rtp_work_release = sih_stream_rtp_work_release,
};

int sih_add_misc_dev(void)
{
#ifdef STREAM_RTP_MODE
	hp_info("%s:enter\n", __func__);
	return misc_register(&sih_haptic_misc);
#else
	return 0;
#endif
}

void sih_del_misc_dev(void)
{
	misc_deregister(&sih_haptic_misc);
}
