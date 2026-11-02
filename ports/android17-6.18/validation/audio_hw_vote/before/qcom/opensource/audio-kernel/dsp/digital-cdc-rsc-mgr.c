// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/ratelimit.h>
#include <dsp/digital-cdc-rsc-mgr.h>
#include <linux/dev_printk.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/mutex.h>

static DEFINE_MUTEX(hw_vote_lock);
static bool is_init_done;

struct hw_vote_entry {
	struct list_head node;
	struct clk *handle;
	u32 requested;
	u32 enabled;
};

static LIST_HEAD(hw_votes);

static struct hw_vote_entry *hw_vote_find(struct clk *handle)
{
	struct hw_vote_entry *vote;

	list_for_each_entry(vote, &hw_votes, node)
		if (vote->handle == handle)
			return vote;
	return NULL;
}

/**
 * digital_cdc_rsc_mgr_hw_vote_enable - Enables hw vote in DSP
 *
 * @vote_handle: vote handle for which voting needs to be done
 * @dev: indicate which device votes
 *
 * Returns 0 on success or -EINVAL/error code on failure
 */
int digital_cdc_rsc_mgr_hw_vote_enable(struct clk *vote_handle, struct device *dev)
{
	struct hw_vote_entry *vote;
	int ret;

	if (IS_ERR_OR_NULL(vote_handle) || !dev)
		return -EINVAL;
	mutex_lock(&hw_vote_lock);
	if (!is_init_done) {
		ret = -ENODEV;
		goto unlock;
	}
	vote = hw_vote_find(vote_handle);
	if (!vote) {
		vote = kzalloc(sizeof(*vote), GFP_KERNEL);
		if (!vote) {
			ret = -ENOMEM;
			goto unlock;
		}
		vote->handle = vote_handle;
		list_add_tail(&vote->node, &hw_votes);
	}
	if (vote->requested == U32_MAX) {
		ret = -EOVERFLOW;
		goto unlock;
	}
	ret = clk_prepare_enable(vote_handle);
	if (!ret) {
		vote->requested++;
		vote->enabled++;
	} else if (!vote->requested) {
		list_del(&vote->node);
		kfree(vote);
	}
unlock:
	mutex_unlock(&hw_vote_lock);
	return ret;
}

EXPORT_SYMBOL(digital_cdc_rsc_mgr_hw_vote_enable);

/**
 * digital_cdc_rsc_mgr_hw_vote_disable - Disables hw vote in DSP
 *
 * @vote_handle: vote handle for which voting needs to be disabled
 * @dev: indicate which device unvotes
 *
 */
void digital_cdc_rsc_mgr_hw_vote_disable(struct clk *vote_handle, struct device *dev)
{
	struct hw_vote_entry *vote;

	if (IS_ERR_OR_NULL(vote_handle) || !dev)
		return;
	mutex_lock(&hw_vote_lock);
	vote = is_init_done ? hw_vote_find(vote_handle) : NULL;
	if (!vote)
		goto unlock;
	vote->requested--;
	/* A partial SSR restoration may have fewer live than requested votes. */
	if (vote->enabled > vote->requested) {
		clk_disable_unprepare(vote_handle);
		vote->enabled--;
	}
	if (!vote->requested) {
		list_del(&vote->node);
		kfree(vote);
	}
unlock:
	mutex_unlock(&hw_vote_lock);
}

EXPORT_SYMBOL(digital_cdc_rsc_mgr_hw_vote_disable);

/**
 * digital_cdc_rsc_mgr_hw_vote_reset - Resets hw vote count
 *
 */
void digital_cdc_rsc_mgr_hw_vote_reset(struct clk *vote_handle)
{
	struct hw_vote_entry *vote;
	int ret;

	if (IS_ERR_OR_NULL(vote_handle))
		return;
	mutex_lock(&hw_vote_lock);
	vote = is_init_done ? hw_vote_find(vote_handle) : NULL;
	if (!vote)
		goto unlock;
	/* Physical enable state includes votes owned by unrelated consumers. */
	while (vote->enabled) {
		clk_disable_unprepare(vote_handle);
		vote->enabled--;
	}
	while (vote->enabled < vote->requested) {
		ret = clk_prepare_enable(vote_handle);
		if (ret) {
			pr_err_ratelimited("%s: vote restoration failed: %d\n",
					   __func__, ret);
			break;
		}
		vote->enabled++;
	}
unlock:
	mutex_unlock(&hw_vote_lock);
}

EXPORT_SYMBOL(digital_cdc_rsc_mgr_hw_vote_reset);

void digital_cdc_rsc_mgr_init(void)
{
	mutex_lock(&hw_vote_lock);
	is_init_done = true;
	mutex_unlock(&hw_vote_lock);
}

void digital_cdc_rsc_mgr_exit(void)
{
	struct hw_vote_entry *vote, *next;

	mutex_lock(&hw_vote_lock);
	is_init_done = false;
	list_for_each_entry_safe(vote, next, &hw_votes, node) {
		while (vote->enabled) {
			clk_disable_unprepare(vote->handle);
			vote->enabled--;
		}
		list_del(&vote->node);
		kfree(vote);
	}
	mutex_unlock(&hw_vote_lock);
}

