#ifndef __ST7715R_H__
#define __ST7715R_H__

#include <asm/jzsoc.h>

#if defined(CONFIG_JZ4750_SLCD_POWERTIP_TFT_PH128128T)

#define WR_GRAM_CMD	0x2c

#if defined(CONFIG_JZ4750D_CETUS)
//#define PIN_CS_N 	(32*N+M)	// Chip select
#define PIN_RESET_N 	(32*3+22)	/* GPD 22 */
#endif

/* Sent a data (8-bit bus) */
static void Mcupanel_Data(unsigned int data) {
	while (REG_SLCD_STATE & SLCD_STATE_BUSY);
	REG_SLCD_DATA = SLCD_DATA_RS_DATA | (data & 0xffffff);
}

/* Sent a command without data  (8-bit bus) */
static void Mcupanel_Command(unsigned int cmd) {
	while (REG_SLCD_STATE & SLCD_STATE_BUSY);
	REG_SLCD_DATA = SLCD_DATA_RS_COMMAND | ((cmd&0xff00) >> 8);
	while (REG_SLCD_STATE & SLCD_STATE_BUSY);
	REG_SLCD_DATA = SLCD_DATA_RS_COMMAND | ((cmd&0xff) >> 0);
}

static void Mcupanel_RegSet(unsigned int cmd, unsigned int data)
{
	Mcupanel_Command(cmd);
	Mcupanel_Data(data);
}

#undef __lcd_slcd_pin_init
#define __lcd_slcd_pin_init() \
do {	\
	__gpio_as_output(PIN_RESET_N);	\
	mdelay(100);	\
	__gpio_set_pin(PIN_RESET_N);	\
	mdelay(10);	\
	__gpio_clear_pin(PIN_RESET_N);	\
	mdelay(10);	\
	__gpio_set_pin(PIN_RESET_N);	\
	mdelay(100);	\
} while(0)

/* 100 */
//	Mcupanel_RegSet(0xb1, 0x011516);			\
/* 120 */
//	Mcupanel_RegSet(0xb1, 0x001516);			\
/* max */
//	Mcupanel_RegSet(0xb1, 0x000101);			\
/* min */
//	Mcupanel_RegSet(0xb1, 0x0f3f3f);			\

#define SlcdInit()	\
do {      \
	Mcupanel_Command(0x11);						\
	Mcupanel_Command(0x29);						\
	Mcupanel_RegSet(0x36, 0xd80000);					\
	Mcupanel_RegSet(0x2a, 0x000200);					\
	Mcupanel_Data(0x810000);							\
	Mcupanel_RegSet(0x2b, 0x000300);					\
	Mcupanel_Data(0x820000);							\
	while (REG_SLCD_STATE & SLCD_STATE_BUSY);			\
} while(0)

#define __lcd_slcd_special_on()						\
	do {								\
		__lcd_slcd_pin_init();					\
		SlcdInit();						\
		REG_SLCD_CTRL &= ~(1 << 2); /* slcdc dma enable */		\
	} while (0)

#define __init_slcd_bus()\
do{\
	__slcd_set_data_8bit_x3();\
	__slcd_set_cmd_8bit();\
	__slcd_set_cs_low();\
	__slcd_set_rs_low();\
	__slcd_set_clk_falling();\
	__slcd_set_parallel_type();\
}while(0)
#endif	/* #if CONFIG_JZ4750_SLCD_POWERTIP_TFT_PH128128T */

#endif	/* __ST7715R_H__ */
