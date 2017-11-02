#ifndef SADC_KEYS_H
#define SADC_KEYS_H

struct sadc_keys_gpio {
	short num;
#define LOW_ENABLE  0
#define HIGH_ENABLE 1
	short enable_level;
	unsigned int debounce_interval;
};

struct sadc_keys_info {
	unsigned int code;
	unsigned int voltage;
	unsigned int fuzz;
};

struct sadc_keys_devdata {
	unsigned int repeat:1;
	struct sadc_keys_gpio *gpio;
	struct sadc_keys_info *info;
	unsigned int info_num;
};

#endif /* SADC_KEYS_H */
