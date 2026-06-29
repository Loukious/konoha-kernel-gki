#ifndef _XIAOMI_HWID_H_
#define _XIAOMI_HWID_H_

enum hw_country_version {
	CountryCN = 0,
};

int get_hw_version_platform(void);
int get_hw_country_version(void);

#endif /* _XIAOMI_HWID_H_ */
