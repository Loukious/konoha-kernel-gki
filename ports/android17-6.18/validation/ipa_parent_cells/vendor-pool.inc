struct device_node *qcom_iommu_group_parse_phandle(struct device *dev)
{
	struct device_node *np;

	if (!dev->of_node)
		return NULL;

	np = of_parse_phandle(dev->of_node, "qcom,iommu-group", 0);
	return np ? np : dev->of_node;
}

static int of_property_walk_each_entry(struct device *dev, const char *propname,
				       struct qcom_iommu_range_prop_cb_data *cb_data)
{
	struct device_node *np;
	const __be32 *p, *property_end;
	int ret, len, naddr, nsize;

	np = qcom_iommu_group_parse_phandle(dev);
	if (!np)
		return -EINVAL;

	p = of_get_property(np, propname, &len);
	if (!p)
		return -ENODEV;

	len /= sizeof(u32);
	naddr = of_n_addr_cells(np);
	nsize = of_n_size_cells(np);
	if (!naddr || !nsize || len % (naddr + nsize)) {
		dev_err(dev, "%s Invalid length %d. Address cells %d. Size cells %d\n",
			propname, len, naddr, nsize);
		return -EINVAL;
	}
	property_end = p + len;

	while (p < property_end) {
		ret = cb_data->range_prop_entry_cb_fn(p, naddr, nsize, cb_data->arg);
		if (ret)
			return ret;

		p += naddr + nsize;
	}

	return 0;
}

static bool check_overlap(struct iommu_resv_region *region, u64 start, u64 end)
{
	u64 region_end = region->start + region->length - 1;

	return end >= region->start && start <= region_end;
}

static int insert_range(const __be32 *p, int naddr, int nsize, void *arg)
{
	struct list_head *head = arg;
	struct iommu_resv_region *region, *new;
	u64 start = of_read_number(p, naddr);
	u64 end = start + of_read_number(p + naddr, nsize) - 1;

	list_for_each_entry(region, head, list) {
		if (check_overlap(region, start, end))
			return -EINVAL;

		if (start < region->start)
			break;
	}

	new = iommu_alloc_resv_region(start, end - start + 1,
					0, IOMMU_RESV_RESERVED, GFP_KERNEL);
	if (!new)
		return -ENOMEM;
	list_add_tail(&new->list, &region->list);
	return 0;
}

int qcom_iommu_generate_dma_regions(struct device *dev,
		struct list_head *head)
{
	struct qcom_iommu_range_prop_cb_data insert_range_cb_data = {
		.range_prop_entry_cb_fn = insert_range,
		.arg = head,
	};

	return of_property_walk_each_entry(dev, "qcom,iommu-dma-addr-pool",
					   &insert_range_cb_data);
}

static int invert_regions(struct list_head *head, struct list_head *inverted)
{
	struct iommu_resv_region *prev, *curr, *new;
	phys_addr_t rsv_start;
	size_t rsv_size;
	int ret = 0;

	/*
	 * Since its not possible to express start 0, size 1<<64 return
	 * an error instead. Also an iova allocator without any iovas doesn't
	 * make sense.
	 */
	if (list_empty(head))
		return -EINVAL;

	/*
	 * Handle case where there is a non-zero sized area between
	 * iommu_resv_regions A & B.
	 */
	prev = NULL;
	list_for_each_entry(curr, head, list) {
		if (!prev)
			goto next;

		rsv_start = prev->start + prev->length;
		rsv_size = curr->start - rsv_start;
		if (!rsv_size)
			goto next;

		new = iommu_alloc_resv_region(rsv_start, rsv_size,
						0, IOMMU_RESV_RESERVED, GFP_KERNEL);
		if (!new) {
			ret = -ENOMEM;
			goto out_err;
		}
		list_add_tail(&new->list, inverted);
next:
		prev = curr;
	}

	/* Now handle the beginning */
	curr = list_first_entry(head, struct iommu_resv_region, list);
	rsv_start = 0;
	rsv_size = curr->start;
	if (rsv_size) {
		new = iommu_alloc_resv_region(rsv_start, rsv_size,
						0, IOMMU_RESV_RESERVED, GFP_KERNEL);
		if (!new) {
			ret = -ENOMEM;
			goto out_err;
		}
		list_add(&new->list, inverted);
	}

	/* Handle the end - checking for overflow */
	rsv_start = prev->start + prev->length;
	rsv_size = -rsv_start;

	if (rsv_size && (U64_MAX - prev->start > prev->length)) {
		new = iommu_alloc_resv_region(rsv_start, rsv_size,
						0, IOMMU_RESV_RESERVED, GFP_KERNEL);
		if (!new) {
			ret = -ENOMEM;
			goto out_err;
		}
		list_add_tail(&new->list, inverted);
	}

	return 0;

out_err:
	list_for_each_entry_safe(curr, prev, inverted, list)
		kfree(curr);
	return ret;
}

void qcom_iommu_generate_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *region;
	LIST_HEAD(dma_regions);
	LIST_HEAD(resv_regions);
	int ret;

	ret = qcom_iommu_generate_dma_regions(dev, &dma_regions);
	if (ret)
		return;

	ret = invert_regions(&dma_regions, &resv_regions);
	iommu_put_resv_regions(dev, &dma_regions);
	if (ret)
		return;

	list_for_each_entry(region, &resv_regions, list) {
		dev_dbg(dev, "Reserved region %llx-%llx\n",
			(u64)region->start,
			(u64)(region->start + region->length - 1));
	}

	list_splice(&resv_regions, head);
}
