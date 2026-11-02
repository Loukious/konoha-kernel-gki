// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/uio_driver.h>

#include <soc/qcom/secure_buffer.h>

#define MPSS_RMTS_CLIENT_ID 1

struct msm_sharedmem {
	struct uio_info info;
	struct device *dev;
	void *allocation;
	dma_addr_t dma_addr;
	size_t allocation_size;
	unsigned long guard_pages;
	u64 perms;
	bool assigned;
};

static int sharedmem_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	struct msm_sharedmem *shared = info->priv;
	struct uio_mem *mem = &info->mem[0];
	unsigned long original_offset = vma->vm_pgoff;
	int ret;

	/* UIO offsets select map indices; this driver exports only map zero. */
	if (original_offset || !mem->size ||
	    vma->vm_end - vma->vm_start > mem->size)
		return -EINVAL;

	if (shared->allocation) {
		vma->vm_pgoff = shared->guard_pages;
		ret = dma_mmap_coherent(shared->dev, vma, shared->allocation,
					shared->dma_addr, shared->allocation_size);
		vma->vm_pgoff = original_offset;
		return ret;
	}

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, mem->addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int sharedmem_assign(struct msm_sharedmem *shared, u32 client_id,
			    bool nav)
{
	struct qcom_scm_vmperm destinations[] = {
		{ VMID_HLOS, PERM_READ | PERM_WRITE },
		{ VMID_MSS_MSA, PERM_READ | PERM_WRITE },
		{ VMID_NAV, PERM_READ | PERM_WRITE },
	};
	int ret;

	if (client_id != MPSS_RMTS_CLIENT_ID)
		return 0;
	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	shared->perms = BIT(VMID_HLOS);
	ret = qcom_scm_assign_mem(shared->info.mem[0].addr,
				  shared->info.mem[0].size, &shared->perms,
				  destinations, nav ? 3 : 2);
	if (!ret)
		shared->assigned = true;
	return ret;
}

static void sharedmem_free(struct msm_sharedmem *shared)
{
	struct qcom_scm_vmperm host = { VMID_HLOS, PERM_READ | PERM_WRITE };
	int ret;

	if (shared->assigned) {
		ret = qcom_scm_assign_mem(shared->info.mem[0].addr,
					  shared->info.mem[0].size,
					  &shared->perms, &host, 1);
		if (ret) {
			/* A remote owner must never access pages reused by Linux. */
			dev_err(shared->dev,
				"failed to reclaim shared memory: %d; retaining allocation\n",
				ret);
			return;
		}
		shared->assigned = false;
	}
	if (shared->allocation) {
		dma_free_coherent(shared->dev, shared->allocation_size,
				  shared->allocation, shared->dma_addr);
		shared->allocation = NULL;
	}
}

static int sharedmem_allocate(struct msm_sharedmem *shared,
			      resource_size_t size, bool guarded)
{
	struct sg_table table;
	int ret;

	if (!size || !PAGE_ALIGNED(size) || size > SIZE_MAX)
		return -EINVAL;
	shared->guard_pages = guarded ? SZ_4K / PAGE_SIZE : 0;
	/* The native guard ABI requires 4 KiB pages on this device. */
	if (guarded && PAGE_SIZE != SZ_4K)
		return -EINVAL;
	if (check_add_overflow((size_t)size, guarded ? (size_t)SZ_8K : 0,
			       &shared->allocation_size))
		return -EOVERFLOW;

	ret = dma_set_mask_and_coherent(shared->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	shared->allocation = dma_alloc_coherent(shared->dev,
						shared->allocation_size, &shared->dma_addr,
				GFP_KERNEL);
	if (!shared->allocation)
		return -ENOMEM;

	/* The modem ABI exports a contiguous physical address, never an IOVA. */
	ret = dma_get_sgtable(shared->dev, &table, shared->allocation,
			      shared->dma_addr, shared->allocation_size);
	if (ret)
		goto free_allocation;
	if (table.orig_nents != 1 || table.sgl->length < shared->allocation_size) {
		ret = -EINVAL;
	} else {
		shared->info.mem[0].addr = sg_phys(table.sgl) +
					    (guarded ? SZ_4K : 0);
	}
	sg_free_table(&table);
	if (!ret)
		return 0;
free_allocation:
	sharedmem_free(shared);
	return ret;
}

static int msm_sharedmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msm_sharedmem *shared;
	struct resource *resource;
	resource_size_t size;
	u32 client_id;
	int ret;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource || resource->end < resource->start)
		return -EINVAL;
	size = resource_size(resource);
	if (!size || !PAGE_ALIGNED(size) || !PAGE_ALIGNED(resource->start))
		return -EINVAL;
	ret = of_property_read_u32(dev->of_node, "qcom,client-id", &client_id);
	if (ret)
		return ret;
	if (client_id == MPSS_RMTS_CLIENT_ID && !qcom_scm_is_available())
		return -EPROBE_DEFER;

	shared = devm_kzalloc(dev, sizeof(*shared), GFP_KERNEL);
	if (!shared)
		return -ENOMEM;
	shared->dev = dev;
	shared->info.priv = shared;
	shared->info.mmap = sharedmem_mmap;
	shared->info.name = resource->name;
	shared->info.version = "1.0";
	shared->info.mem[0].addr = resource->start;
	shared->info.mem[0].size = size;
	shared->info.mem[0].memtype = UIO_MEM_PHYS;

	if (!resource->start) {
		ret = sharedmem_allocate(shared, size,
					 of_property_read_bool(dev->of_node, "qcom,guard-memory"));
		if (ret)
			return dev_err_probe(dev, ret, "shared memory allocation failed\n");
	}
	ret = sharedmem_assign(shared, client_id,
			       of_property_read_bool(dev->of_node, "qcom,vm-nav-path"));
	if (ret)
		goto free_memory;
	ret = uio_register_device(dev, &shared->info);
	if (ret)
		goto free_memory;

	platform_set_drvdata(pdev, shared);
	dev_info(dev, "Device created for client '%s', physical=%pa size=%pa\n",
		 shared->info.name, &shared->info.mem[0].addr,
		 &shared->info.mem[0].size);
	return 0;
free_memory:
	sharedmem_free(shared);
	return dev_err_probe(dev, ret, "shared memory registration failed\n");
}

static void msm_sharedmem_remove(struct platform_device *pdev)
{
	struct msm_sharedmem *shared = platform_get_drvdata(pdev);

	uio_unregister_device(&shared->info);
	sharedmem_free(shared);
}

static const struct of_device_id msm_sharedmem_of_match[] = {
	{ .compatible = "qcom,sharedmem-uio" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_sharedmem_of_match);

static struct platform_driver msm_sharedmem_driver = {
	.probe = msm_sharedmem_probe,
	.remove = msm_sharedmem_remove,
	.driver = {
		.name = "msm_sharedmem",
		.of_match_table = msm_sharedmem_of_match,
		/* UIO's owner pins this module while files or mappings are open. */
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(msm_sharedmem_driver);

MODULE_DESCRIPTION("Qualcomm modem remote-storage shared memory UIO interface");
MODULE_LICENSE("GPL");
