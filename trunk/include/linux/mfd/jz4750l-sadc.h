#ifndef MFD_JZ4750L_SADC_H
#define MFD_JZ4750L_SADC_H

struct adc_cell_platform_data {
	struct mfd_cell *cell;
	const char      *name;
	const char      *dev_name;
	void            *dev_data;
};

struct jz_adc_platform_data {
	struct adc_cell_platform_data *cell_pdata[3];
};

#endif /* MFD_JZ4750L_SADC_H */
