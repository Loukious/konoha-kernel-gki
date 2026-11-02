static void mca_path_control_update_status_work(struct work_struct *work)
{
	struct mca_path_control *info = container_of(
		work, struct mca_path_control, update_condition_work.work);
	int ret = 0;

	mca_log_info("update condition status work\n");

	ret = platform_class_buckchg_ops_get_otg_boost_src(
		MAIN_BUCK_CHARGER, &info->boost_src_default[OTG_BOOST_TYPE]);
	ret |= mca_wireless_rev_get_rev_boost_default(
		&info->boost_src_default[WLS_REV_BOOST_TYPE]);
	mca_log_info("otg_boost_src is %d, wls_rev_boost_default is %d\n",
		     info->boost_src_default[OTG_BOOST_TYPE],
		     info->boost_src_default[WLS_REV_BOOST_TYPE]);
	ret |= platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER,
						     &info->usb_online);
	ret |= platform_class_buckchg_ops_get_otg_boost_enable_status(
		MAIN_BUCK_CHARGER, &info->otg_boost_enable_sts);
	ret |= platform_class_wireless_is_present(WIRELESS_ROLE_MASTER,
						  &info->wireless_online);
	ret |= mca_wireless_rev_get_reverse_chg(&info->wireless_rev_en);
	//TODO:add get wireless_vdd_en
	mca_log_info(
		"usb_online: %d, wireless_online: %d, otg_boost_enable_sts: %d, wireless_rev_en: %d\n",
		info->usb_online, info->wireless_online,
		info->otg_boost_enable_sts, info->wireless_rev_en);
	if (info->usb_online)
		info->condition_value |= PATH_CONTROL_USB;
	else
		info->condition_value &= ~PATH_CONTROL_USB;
	if (info->wireless_online)
		info->condition_value |= PATH_CONTROL_WLS;
	else
		info->condition_value &= ~PATH_CONTROL_WLS;
	if (info->wireless_rev_en)
		info->condition_value |= PATH_CONTROL_WLS_REV;
	else
		info->condition_value &= ~PATH_CONTROL_WLS_REV;
	if (info->otg_boost_enable_sts == OTG_ENABLE_SEQUENCE ||
	    info->otg_boost_enable_sts == OTG_ENABLE)
		info->condition_value |= PATH_CONTROL_OTG;
	else
		info->condition_value &= ~PATH_CONTROL_OTG;
	if (info->wireless_vdd_en)
		info->condition_value |= PATH_CONTROL_VDD;
	else
		info->condition_value &= ~PATH_CONTROL_VDD;
}

