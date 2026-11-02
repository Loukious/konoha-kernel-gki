static const struct of_bus *of_match_bus(struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(of_busses); i++)
		if (!of_busses[i].match || of_busses[i].match(np))
			return &of_busses[i];
	return NULL;
}

static int of_translate_one(const struct device_node *parent, const struct of_bus *bus,
			    const struct of_bus *pbus, __be32 *addr,
			    int na, int ns, int pna, const char *rprop)
{
	const __be32 *ranges;
	unsigned int rlen;
	int rone;
	u64 offset = OF_BAD_ADDR;

	/*
	 * Normally, an absence of a "ranges" property means we are
	 * crossing a non-translatable boundary, and thus the addresses
	 * below the current cannot be converted to CPU physical ones.
	 * Unfortunately, while this is very clear in the spec, it's not
	 * what Apple understood, and they do have things like /uni-n or
	 * /ht nodes with no "ranges" property and a lot of perfectly
	 * useable mapped devices below them. Thus we treat the absence of
	 * "ranges" as equivalent to an empty "ranges" property which means
	 * a 1:1 translation at that level. It's up to the caller not to try
	 * to translate addresses that aren't supposed to be translated in
	 * the first place. --BenH.
	 *
	 * As far as we know, this damage only exists on Apple machines, so
	 * This code is only enabled on powerpc. --gcl
	 *
	 * This quirk also applies for 'dma-ranges' which frequently exist in
	 * child nodes without 'dma-ranges' in the parent nodes. --RobH
	 */
	ranges = of_get_property(parent, rprop, &rlen);
	if (ranges == NULL && !of_empty_ranges_quirk(parent) &&
	    strcmp(rprop, "dma-ranges")) {
		pr_debug("no ranges; cannot translate\n");
		return 1;
	}
	if (ranges == NULL || rlen == 0) {
		offset = of_read_number(addr, na);
		/* set address to zero, pass flags through */
		memset(addr + pbus->flag_cells, 0, (pna - pbus->flag_cells) * 4);
		pr_debug("empty ranges; 1:1 translation\n");
		goto finish;
	}

	pr_debug("walking ranges...\n");

	/* Now walk through the ranges */
	rlen /= 4;
	rone = na + pna + ns;
	for (; rlen >= rone; rlen -= rone, ranges += rone) {
		offset = bus->map(addr, ranges, na, ns, pna, bus->flag_cells);
		if (offset != OF_BAD_ADDR)
			break;
	}
	if (offset == OF_BAD_ADDR) {
		pr_debug("not found !\n");
		return 1;
	}
	memcpy(addr, ranges + na, 4 * pna);

 finish:
	of_dump_addr("parent translation for:", addr, pna);
	pr_debug("with offset: %llx\n", offset);

	/* Translate it into parent bus space */
	return pbus->translate(addr, offset, pna);
}

static u64 __of_translate_address(struct device_node *node,
				  struct device_node *(*get_parent)(const struct device_node *),
				  const __be32 *in_addr, const char *rprop,
				  struct device_node **host)
{
	struct device_node *dev __free(device_node) = of_node_get(node);
	struct device_node *parent __free(device_node) = get_parent(dev);
	const struct of_bus *bus, *pbus;
	__be32 addr[OF_MAX_ADDR_CELLS];
	int na, ns, pna, pns;

	pr_debug("** translation for device %pOF **\n", dev);

	*host = NULL;

	if (parent == NULL)
		return OF_BAD_ADDR;
	bus = of_match_bus(parent);
	if (!bus)
		return OF_BAD_ADDR;

	/* Count address cells & copy address locally */
	bus->count_cells(dev, &na, &ns);
	if (!OF_CHECK_COUNTS(na, ns)) {
		pr_debug("Bad cell count for %pOF\n", dev);
		return OF_BAD_ADDR;
	}
	memcpy(addr, in_addr, na * 4);

	pr_debug("bus is %s (na=%d, ns=%d) on %pOF\n",
	    bus->name, na, ns, parent);
	of_dump_addr("translating address:", addr, na);

	/* Translate */
	for (;;) {
		struct logic_pio_hwaddr *iorange;

		/* Switch to parent bus */
		of_node_put(dev);
		dev = parent;
		parent = get_parent(dev);

		/* If root, we have finished */
		if (parent == NULL) {
			pr_debug("reached root node\n");
			return of_read_number(addr, na);
		}

		/*
		 * For indirectIO device which has no ranges property, get
		 * the address from reg directly.
		 */
		iorange = find_io_range_by_fwnode(&dev->fwnode);
		if (iorange && (iorange->flags != LOGIC_PIO_CPU_MMIO)) {
			u64 result = of_read_number(addr + 1, na - 1);
			pr_debug("indirectIO matched(%pOF) 0x%llx\n",
				 dev, result);
			*host = no_free_ptr(dev);
			return result;
		}

		/* Get new parent bus and counts */
		pbus = of_match_bus(parent);
		if (!pbus)
			return OF_BAD_ADDR;
		pbus->count_cells(dev, &pna, &pns);
		if (!OF_CHECK_COUNTS(pna, pns)) {
			pr_err("Bad cell count for %pOF\n", dev);
			return OF_BAD_ADDR;
		}

		pr_debug("parent bus is %s (na=%d, ns=%d) on %pOF\n",
		    pbus->name, pna, pns, parent);

		/* Apply bus translation */
		if (of_translate_one(dev, bus, pbus, addr, na, ns, pna, rprop))
			return OF_BAD_ADDR;

		/* Complete the move up one level */
		na = pna;
		ns = pns;
		bus = pbus;

		of_dump_addr("one level translation:", addr, na);
	}

	unreachable();
}

u64 of_translate_dma_address(struct device_node *dev, const __be32 *in_addr)
{
	struct device_node *host;
	u64 ret;

	ret = __of_translate_address(dev, __of_get_dma_parent,
				     in_addr, "dma-ranges", &host);

	if (host) {
		of_node_put(host);
		return OF_BAD_ADDR;
	}

	return ret;
}

const __be32 *of_translate_dma_region(struct device_node *dev, const __be32 *prop,
				      phys_addr_t *start, size_t *length)
{
	struct device_node *parent __free(device_node) = __of_get_dma_parent(dev);
	u64 address, size;
	int na, ns;

	if (!parent)
		return NULL;

	na = of_bus_n_addr_cells(parent);
	ns = of_bus_n_size_cells(parent);

	address = of_translate_dma_address(dev, prop);
	if (address == OF_BAD_ADDR)
		return NULL;

	size = of_read_number(prop + na, ns);

	if (start)
		*start = address;

	if (length)
		*length = size;

	return prop + na + ns;
}
