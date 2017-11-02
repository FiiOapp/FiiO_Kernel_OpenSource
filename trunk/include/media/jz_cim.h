/*
 * linux/include/media/jz_cim.h -- Ingenic CIM driver
 *
 * Copyright (C) 2005-2010, Ingenic Semiconductor Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __JZ_CIM_H__
#define __JZ_CIM_H__

#include <linux/i2c.h>
#include <linux/videodev2.h>

struct cim_reg_info {
	unsigned int reg;
	unsigned int val;
#define CIM_OPS_REG	0x1
#define CIM_WR_REG	0
#define CIM_RD_REG	1
	int ops;
};

struct cim_scale {
	int hs;		//horizontical scale
	int vs;		//vertical scale
	int ignore;	//ignore reminder or not
	int pre_w;	//preview
	int pre_h;
	int cap_w;	//capture
	int cap_h;
};

struct cim_buf_info {
	unsigned int vaddr;
	unsigned int paddr;
	unsigned int size;
};
struct cim_v_p_addr_info{
	unsigned long vaddr;
	unsigned long paddr;
};
/*
 * private ioctl
 */
#define VIDIOC_CIM_S_MEM	_IOW('V', BASE_VIDIOC_PRIVATE+0, struct v4l2_buffer)
#define VIDIOC_CIM_RW_REG	_IOWR('V', BASE_VIDIOC_PRIVATE+1, struct cim_reg_info)
#define VIDIOC_CIM_SCALE	_IOW('V', BASE_VIDIOC_PRIVATE+2, struct cim_scale)
#define VIDIOC_CIM_SET_FOUCS    _IOW('V', BASE_VIDIOC_PRIVATE+3, struct v4l2_buffer)
#define VIDIOC_CIM_GET_V_P_ADDR _IOWR('V', BASE_VIDIOC_PRIVATE+4, struct cim_v_p_addr_info)

#define V4L2_FRM_FMT_PLANAR     ('S')         /* planar frame format */
#define V4L2_FRM_FMT_TILE       ('T')         /* YUV420 tile(macro block) mode */
#define V4L2_FRM_FMT_PACK       ('P')         /* package frame format */

/*
 * [31:24]: 'S'/'P'/'T' => CIMCFG.SEP,	planar(separate) mode / package mode / tile mode(only for YUV420)
 * [23:16]: 0 ~ 7 => CIMCFG.PACK
 * [15:8] : reserved
 * [7:0]  : reserved
 */
#define cim_set_fmt_priv(a, b, c, d)	\
	((__u32)(a) | ((__u32)(b) << 8) | ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define cim_get_fmt_priv(priv, a, b, c, d)	\
do {						\
	a = priv & 0xff;			\
	b = (priv>>8) & 0xff;			\
	c = (priv>>16) & 0xff;			\
	d = (priv>>24) & 0xff;			\
}while(0)

struct resolution_info {
	unsigned int 	width;
	unsigned int 	height;
	unsigned int 	bpp;
};

enum sensor_mode_t {
	SENSOR_MODE_PREVIEW = 0,
	SENSOR_MODE_CAPTURE,
}; 

#ifdef __KERNEL__

struct jz_sensor_desc;

struct jz_sensor_ops
{
	int (*sensor_init)(struct jz_sensor_desc *desc);

	/* camera_sensor_probe use for probe weather the sensor present or not
	 * return 0 means success else means fail
	 */
	int (*sensor_probe)(struct jz_sensor_desc *desc);

	/* sensor_set_function use for init preview or capture.
	 * there may be some difference between preview and capture.
	 * so we divided it into two sequences.
	 * param: function indicated which function
	 */
	int (*sensor_set_function)(struct jz_sensor_desc *desc, enum sensor_mode_t mode);

	int (*sensor_set_resolution)(struct jz_sensor_desc *desc, struct resolution_info *res);
	int (*sensor_set_parameter)(struct jz_sensor_desc *desc, int cmd, int mode, int arg);
	
	//void (*jz_cim_set_foucs)(struct jz_sensor_desc *desc);
	int (*sensor_set_foucs)(struct jz_sensor_desc *desc);
	/* sensor_set_power use for change sensor power state
	 */
	int (*sensor_set_power)(struct jz_sensor_desc *desc, int state);

	/* camera_fill_buffer only for fake sensor test
	 */
	int (*camera_fill_buffer)(struct jz_sensor_desc *desc, unsigned int addr);
};

struct jz_sensor_desc
{
	int			cim_id;		/* the host controller the sensor is attached to */
	int 			sensor_id;	/* for multi-sensor. Useless right now. */
	char 			name[32];
	u32			fourcc;		/* sensor output format */

	unsigned int		mclk;		/* master clock */
	unsigned int		i2c_clk;	/* I2C bus clock frequency */
	struct i2c_client	*client;
	struct list_head	list;
	
	int			rst_pin;	/* reset */
	int			pd_pin;		/* power down */

	unsigned int		wait_frames;
	int 			no_dma;		/* 1: PIO mode, 0: DMA mode, always 0 right now */
	int			bus_width;	/* 8-bit I/O */

	struct resolution_info 	*resolution_table;
	int			table_nr;
	struct resolution_info	pre_res;	/* preview */
	struct resolution_info	cap_res;	/* capture */

	struct jz_sensor_ops *ops;
};

struct jz_sensor_platform_data {
	int	cim_id;
	int	rst_pin;	/* reset */
	int	pd_pin;		/* power down */
};

/* drivers/media/video/jz4775_cim.c */
int jz_sensor_register(struct jz_sensor_desc *desc);

/* drivers/media/video/jz_sensor.c */
int sensor_write_reg(struct i2c_client *client,unsigned char reg, unsigned char val);
unsigned char sensor_read_reg(struct i2c_client *client,unsigned char reg);
int sensor_write_reg16(struct i2c_client *client,unsigned short reg, unsigned char val);
unsigned char sensor_read_reg16(struct i2c_client *client,unsigned short reg);
void sensor_set_i2c_speed(struct i2c_client *client,unsigned long speed);

#endif // __KERNEL__

#endif // __JZ_CIM_H__

