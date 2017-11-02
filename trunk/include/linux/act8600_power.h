/*
 * drivers/power/act8600_power.h -- Core interface for ACT8600
 *
 * Copyright 2010 Ingenic Semiconductor LTD.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __ACT8600_POWER_H__
#define __ACT8600_POWER_H__

/*
 * Register values.
 */
#define ACT8600_OUT1		1
#define ACT8600_OUT2		2
#define ACT8600_OUT3		3	
#define ACT8600_OUT4		4
#define ACT8600_OUT5		5
#define ACT8600_OUT6		6
#define ACT8600_OUT7		7
#define ACT8600_OUT8		8

#define ACT8600_OUT_ON		1
#define ACT8600_OUT_OFF		0

#define ACT8600_REG1_VSET    	0x10
#define ACT8600_REG2_VSET    	0x20
#define ACT8600_REG3_VSET    	0x30
#define ACT8600_REG4_VSET    	0x40
#define ACT8600_REG5_VSET    	0x50
#define ACT8600_REG6_VSET    	0x60
#define ACT8600_REG7_VSET    	0x70
#define ACT8600_REG8_VSET    	0x80

#define ACT8600_REG1_VCON	0x12
#define ACT8600_REG2_VCON	0x22
#define ACT8600_REG3_VCON	0x32
#define ACT8600_REG4_VCON	0x41
#define ACT8600_REG5_VCON	0x51
#define ACT8600_REG6_VCON	0x61
#define ACT8600_REG7_VCON	0x71
#define ACT8600_REG8_VCON	0x81

#define ACT8600_NAME		"act8600"

struct act8600_outputs_t{
	int no;
	int value;
	int active_on;
};
  
struct act8600_platform_pdata_t{
	struct act8600_outputs_t *outputs;
	int nr_outputs;
};

//act8600_output_enable(ACT8600_OUT8,ACT8600_OUT_ON);
int act8600_output_enable(int outnum,int enable);
/*
 * voltage control
 */

#endif  /* __ACT8600_POWER_H__ */
