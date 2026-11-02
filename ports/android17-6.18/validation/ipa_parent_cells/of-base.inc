int of_bus_n_addr_cells(struct device_node *np)
{
	u32 cells;

	for (; np; np = np->parent) {
		if (!of_property_read_u32(np, "#address-cells", &cells))
			return cells;
		/*
		 * Default root value and walking parent nodes for "#address-cells"
		 * is deprecated. Any platforms which hit this warning should
		 * be added to the excluded list.
		 */
		WARN_ONCE(!EXCLUDED_DEFAULT_CELLS_PLATFORMS,
			  "Missing '#address-cells' in %pOF\n", np);
	}
	return OF_ROOT_NODE_ADDR_CELLS_DEFAULT;
}

int of_n_addr_cells(struct device_node *np)
{
	if (np->parent)
		np = np->parent;

	return of_bus_n_addr_cells(np);
}

int of_bus_n_size_cells(struct device_node *np)
{
	u32 cells;

	for (; np; np = np->parent) {
		if (!of_property_read_u32(np, "#size-cells", &cells))
			return cells;
		/*
		 * Default root value and walking parent nodes for "#size-cells"
		 * is deprecated. Any platforms which hit this warning should
		 * be added to the excluded list.
		 */
		WARN_ONCE(!EXCLUDED_DEFAULT_CELLS_PLATFORMS,
			  "Missing '#size-cells' in %pOF\n", np);
	}
	return OF_ROOT_NODE_SIZE_CELLS_DEFAULT;
}

int of_n_size_cells(struct device_node *np)
{
	if (np->parent)
		np = np->parent;

	return of_bus_n_size_cells(np);
}
