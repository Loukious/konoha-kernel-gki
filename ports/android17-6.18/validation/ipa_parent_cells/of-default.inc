static void of_bus_default_count_cells(struct device_node *dev,
				       int *addrc, int *sizec)
{
	if (addrc)
		*addrc = of_n_addr_cells(dev);
	if (sizec)
		*sizec = of_n_size_cells(dev);
}

static u64 of_bus_default_map(__be32 *addr, const __be32 *range,
		int na, int ns, int pna, int fna)
{
	u64 cp, s, da;

	cp = of_read_number(range + fna, na - fna);
	s  = of_read_number(range + na + pna, ns);
	da = of_read_number(addr + fna, na - fna);

	pr_debug("default map, cp=%llx, s=%llx, da=%llx\n", cp, s, da);

	if (da < cp || da >= (cp + s))
		return OF_BAD_ADDR;
	return da - cp;
}

static int of_bus_default_translate(__be32 *addr, u64 offset, int na)
{
	u64 a = of_read_number(addr, na);
	memset(addr, 0, na * 4);
	a += offset;
	if (na > 1)
		addr[na - 2] = cpu_to_be32(a >> 32);
	addr[na - 1] = cpu_to_be32(a & 0xffffffffu);

	return 0;
}

static int of_bus_default_match(struct device_node *np)
{
	/*
	 * Check for presence first since of_bus_n_addr_cells() will warn when
	 * walking parent nodes.
	 */
	return of_property_present(np, "#address-cells");
}
