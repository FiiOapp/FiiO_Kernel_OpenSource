/* linux/drivers/spi/spi_jz47xx.c
 *
 * SSI controller for SPI protocol,use FIFO and DMA;
 * base-to: linux/drivers/spi/spi_bitbang.c
 *
 * Copyright (c) 2010 Ingenic
 * Author:Shumb <sbhuang@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/jzsoc.h>

#include "spi_jz47xx.h"

//#define SSI_DEGUG
//#define SSI_KEY_DEGUG
//#define SSI_MSG

#ifdef SSI_DEGUG
#define print_dbg(format,arg...)			\
	printk(format,## arg)
#else
#define print_dbg(format,arg...)
#endif

#ifdef SSI_KEY_DEGUG
#define print_kdbg(format,arg...)			\
	printk(format,## arg)
#else
#define print_kdbg(format,arg...)
#endif

#ifdef SSI_MSG
#define print_msg(format,arg...)			\
	printk(format,## arg)
#else
#define print_msg(format,arg...)
#endif

#define JZ47XX_SPI_RX_BUF(type) 				\
u32 jz47xx_spi_rx_buf_##type(struct jz47xx_spi *hw)		\
{								\
	u16 data  = __ssi_receive_data(hw->chnl);		\
	type *rx = (type *)hw->rx;				\
	*rx++ = (type)(data);			  		\
	hw->rx = (u8 *)rx;					\
	return (u32)data;					\
}

#define JZ47XX_SPI_TX_BUF(type)					\
u32 jz47xx_spi_tx_buf_##type(struct jz47xx_spi *hw)		\
{								\
	u16 data;						\
	const type *tx = (type *)hw->tx;			\
	data = *tx++;						\
	hw->tx = (u8 *)tx;					\
	__ssi_transmit_data(hw->chnl,data);			\
	return (u32)data;					\
}

JZ47XX_SPI_RX_BUF(u8)
JZ47XX_SPI_TX_BUF(u8)

JZ47XX_SPI_RX_BUF(u16)
JZ47XX_SPI_TX_BUF(u16)

u32 jz47xx_spi_rx_buf_u32_lsb(struct jz47xx_spi *hw)
{
	u32 data;
	u32 data1  = (u32)__ssi_receive_data(hw->chnl);
	u32 data2  = (u32)__ssi_receive_data(hw->chnl);
	u32 * rx = (u32 *)hw->rx;
	data = (data1 | data2 << 16);
	*rx++ = (u32)(data);
	hw->rx = (u8 *)rx;
	return (u32)data;
}

u32 jz47xx_spi_rx_buf_u32_msb(struct jz47xx_spi *hw)
{
	u32 data;
	u32 data1  = (u32)__ssi_receive_data(hw->chnl);
	u32 data2  = (u32)__ssi_receive_data(hw->chnl);
	u32 * rx = (u32 *)hw->rx;
	data = (data1 << 16 | data2);
	*rx++ = (u32)(data);
	hw->rx = (u8 *)rx;
	return (u32)data;
}

u32 jz47xx_spi_tx_buf_u32_lsb(struct jz47xx_spi *hw)
{
	u32 data;
	u16 data1,data2;
	const u32 * tx = (u32 *)hw->tx;
	data = *tx++;
	hw->tx = (u8 *)tx;
	data1 = (u16)(data & 0xFFFF);
	data2 = (u16)(data >> 16);
	__ssi_transmit_data(hw->chnl, data1);
	__ssi_transmit_data(hw->chnl, data2);
	return data;
}

u32 jz47xx_spi_tx_buf_u32_msb(struct jz47xx_spi *hw)
{
	u32 data;
	u16 data1,data2;
	const u32 * tx = (u32 *)hw->tx;
	data = *tx++;
	hw->tx = (u8 *)tx;
	data1 = (u16)(data >> 16);
	data2 = (u16)(data & 0xFFFF);
	__ssi_transmit_data(hw->chnl, data1);
	__ssi_transmit_data(hw->chnl, data2);
	return data;
}

/***************************************************************
 * SSI Info dump
 **************************************************************/
void print_ssi_regs(u8 n)
{
	print_msg("\nSSI%d\n",n);
//	print_msg("REG_SSI_DR ========0x%x  \n",REG_SSI_DR(n));
	print_msg("REG_SSI_CR0========0x%x  \n",REG_SSI_CR0(n));
	print_msg("REG_SSI_CR1========0x%x  \n",REG_SSI_CR1(n));
	print_msg("REG_SSI_SR ========0x%x  \n",REG_SSI_SR(n));
	print_msg("REG_SSI_ITR========0x%x  \n",REG_SSI_ITR(n));
	print_msg("REG_SSI_ICR========0x%x  \n",REG_SSI_ICR(n));
	print_msg("REG_SSI_GR ========0x%x  \n",REG_SSI_GR(n));
}

void save_ssi_regs(u32 *regs,u8 n)
{
	regs[0]=REG_SSI_CR0(n);
	regs[1]=REG_SSI_CR1(n);
	regs[2]=REG_SSI_SR(n);
	regs[3]=REG_SSI_ITR(n);
	regs[4]=REG_SSI_ICR(n);
	regs[5]=REG_SSI_GR(n);
}

void show_ssi_regs(u32 *regs)
{
//	print_msg("REG_SSI_DR ========0x%x  \n",REG_SSI_DR(n));
	print_msg("\nREG_SSI_CR0=====0x%x  \n",regs[0]);
	print_msg("REG_SSI_CR1=====0x%x  \n",regs[1]);
	print_msg("REG_SSI_SR =====0x%x  \n",regs[2]);
	print_msg("REG_SSI_ITR=====0x%x  \n",regs[3]);
	print_msg("REG_SSI_ICR=====0x%x  \n",regs[4]);
	print_msg("REG_SSI_GR =====0x%x  \n",regs[5]);
}

void ssi_intr_stat(struct jz_intr_cnt *jz_intr)
{
	print_msg("\nSSI interrupt cnts = %d\n",jz_intr->ssi_intr_cnt);

	print_msg("TXI:%d  RXI:%d\nunderrun:%d  overrun:%d\n\n",
		jz_intr->ssi_txi,jz_intr->ssi_rxi,jz_intr->ssi_eti,jz_intr->ssi_eri);

	print_msg("DMA TX interrupt cnts = %d\nDMA RX interrupt cnts = %d\n",
		jz_intr->dma_tx_cnt,jz_intr->dma_rx_cnt);

	print_msg("dma_tx_err:%d  dma_tx_end:%d\ndma_rx_err:%d  dma_rx_end:%d\n\n",
		jz_intr->dma_tx_err,jz_intr->dma_tx_end,jz_intr->dma_rx_err,jz_intr->dma_rx_end);
}

#ifdef SSI_DEGUG
void ssi_dump_jz_ddma_channel(unsigned int dmanr)
{
	printk("\n----------chan%d----------------\n",dmanr);
	printk("  DSAR\t= 0x%08x\n", REG_DMAC_DSAR(dmanr));
	printk("  DTAR\t= 0x%08x\n", REG_DMAC_DTAR(dmanr));
	printk("  DTCR\t= 0x%08x\n", REG_DMAC_DTCR(dmanr));
	printk("  DRSR\t= 0x%08x\n", REG_DMAC_DRSR(dmanr));
	printk("  DCCSR\t= 0x%08x\n", REG_DMAC_DCCSR(dmanr));
	printk("  DCMD\t= 0x%08x\n", REG_DMAC_DCMD(dmanr));
	printk("  DDA\t= 0x%08x\n", REG_DMAC_DDA(dmanr));
	printk("-------------------------\n");
}
#endif

/* the spi->mode bits understood by this driver: */
#define MODEBITS (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST | SPI_LOOP)
#define SPI_BITS_SUPPORT  (SPI_BITS_8 | SPI_BITS_16 | SPI_BITS_32)

//static int jz_spi_dma_init(struct jz47xx_spi *hw,int flag);

static void jz47xx_spi_cs(struct jz47xx_spi_info *spi, u8 cs, unsigned int pol)
{
	u32 pin_value = *(spi->chipselect + cs);
	gpio_direction_output(pin_value, pol ? 1 : 0);
}

static void jz47xx_spi_chipsel(struct spi_device *spi, int value)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	unsigned int cspol = spi->mode & SPI_CS_HIGH ? 1 : 0;

	switch (value) {
	case BITBANG_CS_INACTIVE:
		/* chip disable selected */
		if (hw->set_cs && hw->pdata)
			hw->set_cs(hw->pdata, spi->chip_select, cspol^1);
		break;

	case BITBANG_CS_ACTIVE:
		if (spi->mode & SPI_CPHA)
			__ssi_set_spi_clock_phase(hw->chnl, 1);
		else
			__ssi_set_spi_clock_phase(hw->chnl, 0);

		if (spi->mode & SPI_CPOL)
			__ssi_set_spi_clock_polarity(hw->chnl, 1);
		else
			__ssi_set_spi_clock_polarity(hw->chnl, 0);

		if (!(spi->mode & SPI_LSB_FIRST))
			__ssi_set_msb(hw->chnl);
		else
			__ssi_set_lsb(hw->chnl);

		if (spi->mode & SPI_LOOP)
			__ssi_enable_loopback(hw->chnl);
		else
			__ssi_disable_loopback(hw->chnl);

		/* chip enable selected */
		if (hw->set_cs && hw->pdata)
			hw->set_cs(hw->pdata, spi->chip_select, cspol);

		break;
	default:
		break;
	}
}

static u32 jz_spi_get_clk(struct spi_device *spi)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	int ssicdr, cgv;
	unsigned long clk;

	ssicdr 	= __cpm_get_ssidiv();
	cgv 	= __ssi_get_grdiv(hw->chnl);
#ifdef JZ_NEW_CODE_TYPE
	if(!hw->pdata->is_pllclk)
		ssicdr = 0;
#endif
	clk = hw->src_clk / ((ssicdr + 1) * (2 * (cgv + 1)));
	hw->spi_clk = clk;
	return clk;
}

static int jz_spi_set_clk(struct spi_device *spi, u32 hz)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	int ssi_cdr = 0, cgv = 0;
	u32 ssi_clk;

	if (hw->src_clk < hz) {
		dev_info(&spi->dev,"Warning:invalid clock(%d Hz) be set to source clk(%d Hz)!\n",
									hz,(uint)hw->src_clk);
		hz = hw->src_clk;
	}

	/* clk = (exclk or pllclk)/( (SSICDR +1) * (2*(CGV+1)) )	*/
	/* 4750: SSICDR (0-15)   CGV (0-255)   16*(2*256)     		*/
	/* 4760: SSICDR (0-63)   CGV (0-255)   64*(2*256)	  	*/
	if (hw->pdata->is_pllclk) { /* source clock is PLLCLK */
		if (hz >= (10 * 1000000)) {
			ssi_cdr = hw->src_clk / (2 * hz) - 1;
		} else {
			ssi_cdr = hw->src_clk / (24 * 1000000) - 1;
		}

		if (ssi_cdr < 0)
			ssi_cdr = 0;
		ssi_clk = hw->src_clk / (ssi_cdr + 1);
	} else { /* source clock is EXCLK */
		ssi_cdr = 0;
		ssi_clk = hw->src_clk;
	}

	cgv = ssi_clk / (2 * hz) - 1;
	if (cgv < 0)
		cgv = 0;
	if (cgv > MAX_SSIGR_CGV)
		cgv = MAX_SSIGR_CGV;
	
	dev_dbg(&spi->dev,"SSICLK:%lu setting pre-scaler to %dHz SSICDR:%d  CGV:%d\n",
		hw->src_clk, hz, ssi_cdr, cgv);

	__ssi_disable(hw->chnl);
	__cpm_set_ssidiv(ssi_cdr);
	__ssi_set_grdiv(hw->chnl, cgv);
	__ssi_enable(hw->chnl);

	return 0;
}

static int spi_start_dma(struct jz47xx_spi *hw, u32 count, int mode)
{
	u8 bpw = hw->transfer_unit_size * 8;
	u8 dma_unit, src_bit, dest_bit;
	u32 phyaddr;
	int chan;
	int is_dummy = 0;

	mode &= DMA_MODE_MASK;
	if (mode == DMA_MODE_READ) { /* dma read rxfifo */
		chan = hw->dma_rx_chnl;
		src_bit  = hw->rxfifo_width;
		dest_bit = bpw;
		dma_unit = hw->dma_rx_unit;

		if (hw->rw_mode & R_DMA) {
			phyaddr = hw->rx_dma;
			is_dummy = 0;
		} else {
			dma_cache_inv((u32)hw->dma_dummy, 4);
			phyaddr = CPHYSADDR((u32)hw->dma_dummy);
			is_dummy = 1;
		}

		print_dbg("SPI DMA Rx fifo_width = %d bits\n", hw->rxfifo_width);
	} else if (mode == DMA_MODE_WRITE) {  /* dma write txfifo */
		chan = hw->dma_tx_chnl;
		src_bit  = bpw;
		dest_bit = hw->txfifo_width;
		dma_unit = hw->dma_tx_unit;

		if (hw->rw_mode & W_DMA) {
			phyaddr = hw->tx_dma;
			is_dummy = 0;
		} else {
			dma_cache_wback_inv((u32)hw->dma_dummy, 4);
			phyaddr = CPHYSADDR((u32)hw->dma_dummy);
			is_dummy = 1;
		}

		print_dbg("SPI DMA Tx fifo_width = %d bits\n", hw->txfifo_width);
	} else {
		dev_err(hw->dev,"SPI Start DMA Fail(Mode Error) !!!\n");
		return SPI_DMA_ERROR;
	}

	if (chan < 0)
		return chan;

	disable_dma(chan);
	jz_set_dma_src_width(chan, src_bit);
	jz_set_dma_dest_width(chan, dest_bit);
	jz_set_dma_block_size(chan, dma_unit);	 /* n byte burst */

	jz_set_ssi_dma_mode(chan, mode, is_dummy);
	set_dma_addr(chan, phyaddr);
	set_dma_count(chan, count); /* ensure dma count align */
	enable_dma(chan);

	return 0;
}

static inline int spi_dma_setup(struct jz47xx_spi *hw,unsigned int len)
{
	int err = 0;

	if (hw->rx_dma) {
		if ((u32)hw->rx_dma % hw->dma_rx_unit) {
			dev_err(hw->dev, "Rx DMA buffer address NOT Align: 0x%08x\n", (u32)hw->rx_dma);
			dev_err(hw->dev, "Buffer need to Aligned to 32Byte\n");
			err = -EINVAL;
			goto dma_err_out;
		}
	}

	if (hw->tx_dma) {
		if ((u32)hw->tx_dma % hw->dma_tx_unit) {
			dev_err(hw->dev, "Tx DMA buffer address NOT Align: 0x%08x\n", (u32)hw->tx_dma);
			dev_err(hw->dev, "Buffer need to Aligned to 32Byte\n");
			err = -EINVAL;
			goto dma_err_out;
		}
	}

	hw->dma_dummy[0] = 0;

	/* Start Rx DMA read */
	err = spi_start_dma(hw, len, DMA_MODE_READ);
	if (err < 0)
		goto dma_err_out;

	/* Start Tx DMA write */
	err = spi_start_dma(hw, len, DMA_MODE_WRITE);
	if (err < 0) {
		disable_dma(hw->dma_rx_chnl);
		goto dma_err_out;
	}

#ifdef SSI_DEGUG
	ssi_dump_jz_ddma_channel(hw->dma_tx_chnl);
	ssi_dump_jz_ddma_channel(hw->dma_rx_chnl);
#endif
	print_kdbg("DMA transfer Start...\n");

dma_err_out:
	return err;
}

/**
 * jz_spi_dma_transfer -
 *
 * NOTE
 *	change SSI trigger for DMA transfer.
 *	Important!!! it probable die in waitting for DMA_RX_END intr
 *	if configure improper.
 */
static int jz_spi_dma_transfer(struct jz47xx_spi *hw)
{
	int dma_ds[] = {16, 4, 2, 1};
	//int dma_ds[] = {32, 16, 4, 2, 1};
	int i;
	int ret, tmp;

	for (i = 0; i < ARRAY_SIZE(dma_ds); i++) {
		if (!(hw->len % dma_ds[i]))
			break;
	}

	/* Is it DMA Hardware error ??? I don't known.
	 * So to avoid it as below.
	 * Seem 8bit width can NOT used 32Byte burst length.
	 */
	if (i < ARRAY_SIZE(dma_ds)) {
		tmp = dma_ds[i];

		hw->dma_tx_unit = tmp;
		/* Due to SSI TX/RX trigger is n * 8 fifo length;
		 * TX/RX trigger 0 indecates 1 fifo length.
		 * rx trigger and DMA Burst need align to 8 * fifo width.
		 */
		if (tmp / (hw->rxfifo_width >> 3) < 8)
			hw->dma_rx_unit = 1;
		else
			hw->dma_rx_unit = tmp;
	} else {
		print_msg("DMA block_size force to defaut set!!!");
		hw->dma_tx_unit = JZ_SSI_DMA_BURST_LENGTH;
		hw->dma_rx_unit = JZ_SSI_DMA_BURST_LENGTH;
	}

	hw->tx_trigger = hw->dma_tx_unit / (hw->txfifo_width >> 3);
	hw->rx_trigger = hw->dma_rx_unit / (hw->rxfifo_width >> 3);

	dev_dbg(hw->dev, "dma_tx_unit:\t%d\tdma_rx_unit:\t%d\n"
			"tx_trigger:\t%d\trx_trigger:\t%d\n",
			hw->dma_tx_unit, hw->dma_rx_unit,
			hw->tx_trigger, hw->rx_trigger);

	__ssi_set_tx_trigger(hw->chnl, hw->tx_trigger);
	__ssi_set_rx_trigger(hw->chnl, hw->rx_trigger);

	start_transmit(hw);

	ret = spi_dma_setup(hw, hw->len);
	if (ret < 0) {
		dev_info(hw->dev,"DMA setup fail!(rw_mode = %d, status = %d)\n", hw->rw_mode, ret);
		return ret;
	}

	__ssi_enable_rx_error_intr(hw->chnl);

	hw->count = hw->len;

	return 0;
}

/**
 * jz47xx_spi_dma_txrx -
 */
static int jz47xx_spi_dma_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;
	int ret;
	unsigned long flags;

	print_kdbg("mname: %s  txrx: tx %p, rx %p, len %d ,tx_dma 0x%08X ,rx_dma 0x%08X\n",
		spi->modalias,t->tx_buf, t->rx_buf, t->len, t->tx_dma ,t->rx_dma);

	__ssi_disable_tx_intr(hw->chnl);
	__ssi_disable_rx_intr(hw->chnl);

	/* flush TX FIFO and fill FIFO */
	__ssi_flush_fifo(hw->chnl);

	__ssi_enable_receive(hw->chnl);
	__ssi_clear_errors(hw->chnl);

	memset(g_jz_intr, 0, sizeof *g_jz_intr);

	spin_lock_irqsave(&hw->txrx_lock, flags);
	ret = jz_spi_dma_transfer(hw);
	if (ret < 0) {
		dev_err(hw->dev,"ERROR:spi_transfer error(%d)!\n", ret);
		__ssi_finish_transmit(hw->chnl);
		__ssi_clear_errors(hw->chnl);
		spin_unlock_irqrestore(&hw->txrx_lock, flags);
		return ret;
	}
	spin_unlock_irqrestore(&hw->txrx_lock, flags);

	/* wait the interrupt finish the transfer( one spi_transfer be sent ) */
	wait_for_completion(&hw->done);

	__ssi_finish_transmit(hw->chnl);
	__ssi_clear_errors(hw->chnl);

#ifdef SSI_MSG
	/* ------- for debug --------- */
	ssi_intr_stat(g_jz_intr);
	/* --------------------------- */
#endif

	if (hw->dma_flag == SPI_DMA_ACK && !g_jz_intr->ssi_eri)
		hw->rlen += hw->count;
	else
		dev_info(hw->dev,"DMA data transfer ERROR!\n");

	if (hw->rlen != t->len) {
		dev_info(hw->dev,"Length error:hw->rlen=%d  t->len=%d\n",hw->rlen,t->len);
		if (hw->rlen > hw->len)
			hw->rlen = hw->len;
	}

	return hw->rlen;
}

static irqreturn_t jz47xx_spi_dma_irq_callback(struct jz47xx_spi *hw)
{
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;

	g_jz_intr->ssi_intr_cnt++;
	if (__ssi_underrun(hw->chnl) &&
	    __ssi_tx_error_intr(hw->chnl)) {
		print_dbg("UNDR:\n");
		dev_info(hw->dev, "UNDR:\n");
		g_jz_intr->ssi_eti++;

		__ssi_disable_tx_error_intr(hw->chnl);
		__ssi_clear_errors(hw->chnl);

		goto irq_done;
	}

	if (__ssi_overrun(hw->chnl) &&
	    __ssi_rx_error_intr(hw->chnl)) {
		print_dbg(" overrun:\n");
		g_jz_intr->ssi_eri++;
		disable_dma(hw->dma_tx_chnl);
		disable_dma(hw->dma_rx_chnl);
		__ssi_disable_rx_intr(hw->chnl);
		__ssi_clear_errors(hw->chnl);

		dev_err(hw->dev, "DMA Rxfifo overrun.\n");
		complete(&hw->done);
	}

irq_done:
	return IRQ_HANDLED;
}

/**
 * cpu_read_rxfifo -
 *
 * BUGS:
 *	if fifo width is 32bit, R_MODE or RW_MODE,
 *	the rxfifo may be left odd data, but readed
 *	data is even by hw->get_rx(hw), because the old
 *	SSI's fifo max width is 16bit.
 */
static u32 cpu_read_rxfifo(struct jz47xx_spi *hw)
{
	u8 unit_size = hw->transfer_unit_size;
	u32 cnt, dat;
	int dummy_read = 0;

	print_dbg("The count of TxFIFO is %d \n", __ssi_get_rxfifo_count(hw->chnl));
	if (__ssi_get_rxfifo_count(hw->chnl) < 1)
		return 0;

	cnt = hw->rlen;
	if ((hw->rw_mode & RW_MODE) == W_MODE) {
		print_dbg("W_MODE\n");
		unit_size = unit_size == SPI_32BITS ? unit_size / 2 : unit_size;
		dummy_read = 1;
	}

	spin_lock(&hw->lock);
	while (!__ssi_rxfifo_empty(hw->chnl)) {
		hw->rlen += unit_size;

		if (dummy_read)
			dat = __ssi_receive_data(hw->chnl);
		else
			dat = hw->get_rx(hw);
	}
	spin_unlock(&hw->lock);

	return (hw->rlen - cnt);
}

/**
 * cpu_write_txfifo -
 */
static u32 cpu_write_txfifo(struct jz47xx_spi *hw, u32 entries)
{
	u8 unit_size = hw->transfer_unit_size;
	u32 i, cnt, count;
	u32 dat;

	if ((!entries) || (!(hw->rw_mode & RW_MODE)))
		return 0;

	/* because SSI FIFO width is 16(17) bits */
	cnt = (unit_size == SPI_32BITS) ? (entries >> 1) : entries;
	count = cnt * unit_size;

	spin_lock(&hw->lock);
	if (hw->rw_mode & W_MODE) {
		for(i = 0; i < cnt; i++)
			dat = (u32)(hw->get_tx(hw));
	} else { /* read, fill txfifo with 0 */
		for(i = 0; i < cnt; i++)
			__ssi_transmit_data(hw->chnl, 0);
	}
	spin_unlock(&hw->lock);

	hw->count += count;

	dev_dbg(hw->dev, "hw->count:%d. %s LINE %d: %s\n", hw->count, __func__, __LINE__, __FILE__);
	return count;
}

/**
 * jz_spi_cpu_transfer -
 */
static int jz_spi_cpu_transfer(struct jz47xx_spi *hw, long length)
{
	unsigned char int_flag = 0, last_flag = 0;
	u32 entries = 0, send_entries = 0;
	u32 unit_size, trigger;
	long leave_len_bytes;
	u32 retlen;

	/* calculate the left entries */
	leave_len_bytes = hw->len - hw->count;

	if (hw->len < hw->count) {
		dev_err(hw->dev,"Fill data len error!!!(len < count)\n");
		return -1;
	}

	if (leave_len_bytes == 0) { /* --- End or ??? --- */
		print_dbg("leave_len_bytes = 0\n");
		return 0;
	}

	if (hw->len % hw->transfer_unit_size) {
		pr_err("The length of tranfer data is error\n");
		return -EFAULT;
	}

	unit_size = hw->transfer_unit_size;
	if (unit_size == SPI_8BITS) {
		entries = leave_len_bytes;
	} else if (unit_size == SPI_32BITS) {
		/* because SSI FIFO width is 16(17) bits */
		entries = leave_len_bytes >> 1;
	} else if (unit_size == SPI_16BITS) {
		entries = leave_len_bytes >> 1;
	} else {
		dev_err(hw->dev,"transfer_unit_size error!\n");
		return -1;
	}

	print_dbg("%s unit_size:%d, entries:%d\n", __func__, unit_size, entries);

	/* calculate the entries which will be sent currently
	 * distinguish between the first and interrupt
	 */
	if (hw->is_first) {
		/* CPU Mode should reset SSI triggers at first */
		hw->tx_trigger = SSI_TX_FIFO_THRESHOLD * 8;
		hw->rx_trigger = (SSI_RX_FIFO_THRESHOLD - SSI_SAFE_THRESHOLD) * 8;

		__ssi_set_tx_trigger(hw->chnl, hw->tx_trigger);
		__ssi_set_rx_trigger(hw->chnl, hw->rx_trigger);

		if(entries <= JZ_SSI_MAX_FIFO_ENTRIES) {
			send_entries = entries;
		} else {
			/* need enable half_intr, left entries will be sent in SSI interrupt
			 * and receive the datas
			 */
			send_entries = JZ_SSI_MAX_FIFO_ENTRIES;
			int_flag = 1;
		}
		start_transmit(hw);

		hw->is_first = 0;
	} else { /* happen in interrupts */
		trigger = JZ_SSI_MAX_FIFO_ENTRIES - hw->tx_trigger;
		if (entries <= trigger) {
			send_entries = entries;
			/* the last part of data shouldn't disable RXI_intr
			 * at once !!!
			 */
			last_flag = 1;	
		} else {
			/* need enable half_intr, left entries will be sent in SSI interrupt
			 * and receive the datas
			 */
			send_entries = CPU_ONCE_BLOCK_ENTRIES;
			int_flag = 1;
		}
	}

	if (length > 0) {
		length = length/hw->transfer_unit_size;
		if(length < send_entries)
			send_entries = length;
	}

	/* fill the txfifo with CPU Mode */
	retlen = cpu_write_txfifo(hw, send_entries);
	if (!retlen) {
		dev_info(hw->dev, "cpu_write_txfifo error!\n");
		return -1;
	}
	print_kdbg("+:(%d)\n",cnt);

	__ssi_enable_tx_error_intr(hw->chnl);
	__ssi_enable_rx_error_intr(hw->chnl);

	/* every time should control the SSI half_intrs */
	if (int_flag) {
		__ssi_enable_txfifo_half_empty_intr(hw->chnl);
		__ssi_enable_rxfifo_half_full_intr(hw->chnl);
	} else {
		__ssi_disable_txfifo_half_empty_intr(hw->chnl);
		__ssi_disable_rxfifo_half_full_intr(hw->chnl);
	}

	/* to avoid RxFIFO overflow when CPU Mode at last time to fill */
	if (last_flag) {
		last_flag = 0;
		__ssi_enable_rxfifo_half_full_intr(hw->chnl);
	}

	return 0;
}

static int jz47xx_spi_pio_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;
	u32 entries;
	int ret;
	unsigned long flags;

	__ssi_disable_tx_intr(hw->chnl);
	__ssi_disable_rx_intr(hw->chnl);

	/* flush TX FIFO and fill FIFO */
	__ssi_flush_fifo(hw->chnl);

	__ssi_enable_receive(hw->chnl);
	__ssi_clear_errors(hw->chnl);

	memset(g_jz_intr, 0, sizeof *g_jz_intr);
	entries = hw->len * 8 / hw->bits_per_word;
	g_jz_intr->max_ssi_intr = (entries + JZ_SSI_MAX_FIFO_ENTRIES - 1) /
				   JZ_SSI_MAX_FIFO_ENTRIES * 2 + 2;

	/* This start SSI transfer, write data or 0 to txFIFO.
	 * irq is locked to protect SSI config registers */
	spin_lock_irqsave(&hw->txrx_lock, flags);
	hw->is_first = 1;
	ret = jz_spi_cpu_transfer(hw, 0);
	if (ret < 0) {
		dev_err(hw->dev,"ERROR:spi_transfer error(%d)!\n", ret);
		__ssi_disable_tx_intr(hw->chnl);
		__ssi_disable_rx_intr(hw->chnl);
		__ssi_finish_transmit(hw->chnl);
		__ssi_clear_errors(hw->chnl);
		spin_unlock_irqrestore(&hw->txrx_lock, flags);

		return ret;
	}
	spin_unlock_irqrestore(&hw->txrx_lock, flags);

	/* wait the interrupt finish the transfer( one spi_transfer be sent ) */
	wait_for_completion(&hw->done);

	__ssi_finish_transmit(hw->chnl);
	__ssi_clear_errors(hw->chnl);

	if (hw->rlen != t->len) {
		dev_info(hw->dev, "Length error:hw->rlen=%d  t->len=%d\n", hw->rlen,t->len);

		if(hw->rlen > hw->len)
			hw->rlen = hw->len;
	}

	return hw->rlen;
}

static irqreturn_t jz47xx_spi_pio_irq_callback(struct jz47xx_spi *hw)
{
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;
	long left_count= hw->len - hw->count;
	u8 flag = 0;
	u32 cnt;
	int status;

	g_jz_intr->ssi_intr_cnt++;
	/* to avoid die in interrupt if some error occur */
	if (g_jz_intr->ssi_intr_cnt > g_jz_intr->max_ssi_intr) {
		__ssi_disable_tx_intr(hw->chnl);
		__ssi_disable_rx_intr(hw->chnl);
		dev_err(hw->dev, "\nERROR:SSI interrupts too many count(%d)!\n",
			g_jz_intr->ssi_intr_cnt);

		complete(&hw->done);
		goto irq_done;
	}

	if (__ssi_underrun(hw->chnl) &&
	    __ssi_tx_error_intr(hw->chnl)) {
		print_kdbg("UNDR:");
		g_jz_intr->ssi_eti++;
		__ssi_disable_tx_error_intr(hw->chnl);

		if (left_count == 0) {
			cnt = cpu_read_rxfifo(hw);
			print_dbg("-:(%d)\n",cnt);

			__ssi_disable_tx_intr(hw->chnl);
			__ssi_disable_rx_intr(hw->chnl);

			complete(&hw->done);
		} else {
			__ssi_clear_errors(hw->chnl);
			__ssi_enable_tx_error_intr(hw->chnl);
		}

		flag++;

	}

	if (__ssi_overrun(hw->chnl) &&
	    __ssi_rx_error_intr(hw->chnl)) {
		print_kdbg(" overrun:");
		g_jz_intr->ssi_eri++;
		cnt = cpu_read_rxfifo(hw);
		print_dbg("-:(%d)\n",cnt);

		flag++;
	}

	if (__ssi_rxfifo_half_full(hw->chnl) &&
	    __ssi_rxfifo_half_full_intr(hw->chnl)) {
		print_kdbg("RXI:");
		g_jz_intr->ssi_rxi++;
		cnt = cpu_read_rxfifo(hw);
		print_dbg("-:(%d)\n",cnt);

		flag++;
	}

	if (__ssi_txfifo_half_empty_intr(hw->chnl) &&
	    __ssi_txfifo_half_empty(hw->chnl)) {
		print_kdbg("TXI:");
		g_jz_intr->ssi_txi++;

		status = jz_spi_cpu_transfer(hw, 0);
		if (status < 0) {
			dev_err(hw->dev, "jz_spi_cpu_transfer error!!!!!\n");
			__ssi_disable_tx_intr(hw->chnl);
			__ssi_disable_rx_intr(hw->chnl);
			//__ssi_disable(hw->chnl);
			complete(&hw->done);

			goto irq_done;
		}
		flag++;
	}

	if (!flag) {
		dev_info(hw->dev, "\nERROR:SSI interrupt Type error\n");
		complete(&hw->done);
	}

irq_done:
	__ssi_clear_errors(hw->chnl);
	return IRQ_HANDLED;
}

/**
 * jz47xx_spi_setupxfer -
 *
 * every spi_transfer could call this routine to setup itself
 */
static int jz47xx_spi_setupxfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	u8  bpw,fifo_width;
	u32 hz;

	bpw = t ? t->bits_per_word : spi->bits_per_word;
	hz  = t ? t->speed_hz : spi->max_speed_hz;

	if (t) {
		if (!bpw)
			bpw = spi->bits_per_word;
		if (!hz)
			hz= spi->max_speed_hz;
	}

	if (bpw < 2 || bpw > 32) {
		dev_err(&spi->dev, "invalid bits-per-word (%d)\n", bpw);
		return -EINVAL;
	}

	hw->bits_per_word = bpw;
	if (bpw <= 8 ) {
		hw->transfer_unit_size = SPI_8BITS;
		hw->get_rx = jz47xx_spi_rx_buf_u8;
		hw->get_tx = jz47xx_spi_tx_buf_u8;
		fifo_width = FIFO_W8;
	} else if(bpw <= 16) {
		hw->transfer_unit_size = SPI_16BITS;
		hw->get_rx = jz47xx_spi_rx_buf_u16;
		hw->get_tx = jz47xx_spi_tx_buf_u16;
		fifo_width = FIFO_W16;
	} else {
		hw->transfer_unit_size = SPI_32BITS;
		fifo_width = FIFO_W16;
		if (spi->mode & SPI_LSB_FIRST) {
			hw->get_rx = jz47xx_spi_rx_buf_u32_lsb;
			hw->get_tx = jz47xx_spi_tx_buf_u32_lsb;
		} else {
			hw->get_rx = jz47xx_spi_rx_buf_u32_msb;
			hw->get_tx = jz47xx_spi_tx_buf_u32_msb;
		}

		/* SSI DR register has 16 bits significance bit,
		 * can not use DMA for 32bit buswidth.
		 */
		if (hw->use_dma)
			hw->use_dma = 0;
	}

	hw->txfifo_width = fifo_width;
	hw->rxfifo_width = fifo_width;
	__ssi_set_frame_length(hw->chnl, fifo_width);

	if (spi->mode & SPI_LSB_FIRST)
		__ssi_set_lsb(hw->chnl);
	else
		__ssi_set_msb(hw->chnl);

	jz_spi_set_clk(spi,hz);
	dev_dbg(&spi->dev, "The real SPI CLK is %d Hz\n", jz_spi_get_clk(spi));

	spin_lock(&hw->bitbang.lock);
	if (!hw->bitbang.busy) {
		hw->bitbang.chipselect(spi, BITBANG_CS_INACTIVE);
		/* need to ndelay for 0.5 clocktick ? */
	}
	spin_unlock(&hw->bitbang.lock);

	return 0;
}

static int jz47xx_spi_setup(struct spi_device *spi)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);

	if (spi->chip_select >= spi->master->num_chipselect) {
		dev_err(&spi->dev, "cs%d >= max %d\n",
			spi->chip_select,
			spi->master->num_chipselect);
		return -EINVAL;
	}

	if (spi->chip_select == 0) {
		__ssi_select_ce(hw->chnl);
	} else if (spi->chip_select == 1) {
		__ssi_select_ce2(hw->chnl);
	} else
		return -EINVAL;

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	if (spi->mode & ~MODEBITS) {
		dev_info(&spi->dev, "Warning: unsupported mode bits %x\n",
			spi->mode & ~MODEBITS);
		return -EINVAL;
	}
	hw->spi_mode = spi->mode;

	if (spi->mode & SPI_LSB_FIRST)
		__ssi_set_lsb(hw->chnl);
	else
		__ssi_set_msb(hw->chnl);

	if (spi->bits_per_word & ~SPI_BITS_SUPPORT) {
		dev_info(&spi->dev, "Warning: unsupported bits_per_word: %d\n",
			spi->bits_per_word);
		return -EINVAL;
	}

	if (!spi->max_speed_hz)
		return -EINVAL;

	if (hw->src_clk < spi->max_speed_hz) {
		dev_info(&spi->dev,"Warning:invalid clock(%d Hz) be set to source clk(%d Hz)!\n",
		spi->max_speed_hz,(uint)hw->src_clk);
		spi->max_speed_hz = hw->src_clk;
	}

	spin_lock(&hw->bitbang.lock);
	if (!hw->bitbang.busy) {
		hw->bitbang.chipselect(spi, BITBANG_CS_INACTIVE);
		/* need to ndelay for 0.5 clocktick ? */
	}
	spin_unlock(&hw->bitbang.lock);

	return 0;
}

static int jz47xx_spi_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct jz47xx_spi *hw = spi_master_get_devdata(spi->master);
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&hw->lock, flags);
	if (hw->state & SUSPND) {
		hw->state &= ~SPIBUSY;
		spin_unlock_irqrestore(&hw->lock, flags);
		printk("Now enter suspend, so cann't tranfer data\n");
		return -ESHUTDOWN;
	}
	hw->state |= SPIBUSY;
	spin_unlock_irqrestore(&hw->lock, flags);

	if (t->len == 0)
		return 0;

	if (t->len % (t->bits_per_word / 8)) {
		dev_err(hw->dev, "Data length NOT align to bits_per_word in bytes: %d.\n", t->len);
		return -EINVAL;
	}

	hw->tx = t->tx_buf;
	hw->rx = t->rx_buf;
	hw->tx_dma = t->tx_dma;
	hw->rx_dma = t->rx_dma;
	hw->len = t->len;
	hw->count = 0;
	hw->rlen = 0;
	hw->dma_flag &= ~SPI_DMA_ACK;
	g_jz_intr->ssi_intr_cnt = 0;

	hw->rw_mode = 0;
	if (hw->tx_dma)
		hw->rw_mode |= W_DMA;
	if (hw->rx_dma)
		hw->rw_mode |= R_DMA;

	if (hw->tx)
		hw->rw_mode |= W_MODE;
	if (hw->rx)
		hw->rw_mode |= R_MODE;

	if (hw->use_dma && (hw->rw_mode & RW_DMA)) { /* SSI use DMA mode */
		hw->txrx_bufs		= jz47xx_spi_dma_txrx;
		hw->irq_callback	= jz47xx_spi_dma_irq_callback;
	} else if (hw->rw_mode & RW_MODE) { /* SSI use PIO mode */
		hw->txrx_bufs		= jz47xx_spi_pio_txrx;
		hw->irq_callback	= jz47xx_spi_pio_irq_callback;
	} else {
		dev_err(hw->dev, "write/read buffer address is incorrect.\n");
		return -ENXIO;
	}

	retval = hw->txrx_bufs(spi, t);

	spin_lock_irqsave(&hw->lock, flags);
	hw->state &= ~SPIBUSY;
	spin_unlock_irqrestore(&hw->lock, flags);

	return retval;
}

static irqreturn_t jz47xx_spi_irq(int irq, void *dev)
{
	struct jz47xx_spi *hw = dev;
	return hw->irq_callback(hw); 
}

static irqreturn_t spi_dma_tx_irq(int irq, void *dev)
{
	struct jz47xx_spi *hw = dev;
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;
	int chan = hw->dma_tx_chnl;

	g_jz_intr->dma_tx_cnt++;
	disable_dma(chan);
	if (__dmac_channel_address_error_detected(chan)) {
		__dmac_channel_clear_address_error(chan);
		g_jz_intr->dma_tx_err++;

		dev_err(hw->dev,"DMA address error\n");
		complete(&hw->done);
	}

	if (__dmac_channel_transmit_end_detected(chan)) {
		__dmac_channel_clear_transmit_end(chan);
		__ssi_disable_tx_error_intr(hw->chnl); /* disable underrun irq here ??? */
		g_jz_intr->dma_tx_end++;

		print_msg("DMA Write End\n");
		print_msg("DMA Tx FIFO = %d\n",__ssi_get_txfifo_count(hw->chnl));
	}

	return IRQ_HANDLED;
}

static irqreturn_t spi_dma_rx_irq(int irq, void *dev)
{
	struct jz47xx_spi *hw = dev;
	struct jz_intr_cnt *g_jz_intr = hw->g_jz_intr;
	int chan = hw->dma_rx_chnl;

	g_jz_intr->dma_rx_cnt++;
	disable_dma(chan);
	if (__dmac_channel_address_error_detected(chan)) {
		dev_err(hw->dev,"%s: DMAC address error.\n", __FUNCTION__);
		g_jz_intr->dma_rx_err++;
		__dmac_channel_clear_address_error(chan);
		complete(&hw->done);
	}

	if (__dmac_channel_transmit_end_detected(chan)) {
		g_jz_intr->dma_rx_end++;
		__dmac_channel_clear_transmit_end(chan);

		print_msg("DMA Read End\n");

		print_msg("DMA Rx FIFO = %d\n",__ssi_get_rxfifo_count(hw->chnl));

		if (!(hw->dma_flag & SPI_DMA_ACK)) {
			hw->dma_flag |= SPI_DMA_ACK;
			complete(&hw->done);
		}
	}

	return IRQ_HANDLED;
}

static int jz_spi_dma_init(struct jz47xx_spi *hw)
{
	int tx_dma_id, rx_dma_id;
	int err;

	print_dbg("SPI Master %d request DMA!\n",hw->chnl);
	if (hw->chnl == 0) {
		tx_dma_id = DMA_ID_SSI0_TX;
		rx_dma_id = DMA_ID_SSI0_RX;
	} else if(hw->chnl == 1) {
		tx_dma_id = DMA_ID_SSI1_TX;
		rx_dma_id = DMA_ID_SSI1_RX;
	} else {
		dev_err(hw->dev, "SSI %d DMA request Unknown channel.\n", hw->chnl);
		return -EINVAL;
	}

	hw->dma_tx_chnl = jz_request_dma(tx_dma_id, "SSI Tx DMA",
					spi_dma_tx_irq, 0, hw);
	if (hw->dma_tx_chnl < 0 ) {
		dev_dbg(hw->dev, "SSI %d Tx DMA request failed(ENO=%d)!\n",hw->chnl, hw->dma_tx_chnl);
		err = -ENXIO;
		goto err_tx_dma;
	}

	hw->dma_rx_chnl = jz_request_dma(rx_dma_id, "SSI Rx DMA",
					spi_dma_rx_irq, 0, hw);
	if (hw->dma_rx_chnl < 0 ) {
		dev_dbg(hw->dev, "SSI %d Rx DMA request failed(ENO=%d)!\n",hw->chnl, hw->dma_rx_chnl);
		err = -ENXIO;
		goto err_rx_dma;
	}

	return 0;

err_rx_dma:
	jz_free_dma(hw->dma_tx_chnl);

err_tx_dma:
	return err;
}

int jz_spi_pinconfig(struct jz47xx_spi *hw)
{
	struct jz47xx_spi_info 	*pdata = hw->pdata;

#ifdef CONFIG_JZ_SPI_BOARD_INFO_REGISTER
	if (pdata->board_size > MAX_SPI_DEVICES) {
		pdata->board_size = MAX_SPI_DEVICES;
		dev_err(hw->dev,"SPI devices exceed defined max_num!!!\n");
		return -EIO;
	}
#endif

	if (pdata->pins_config) { /* if user want to configure by themself */
		dev_dbg(hw->dev, "Use user's IO configure\n");
		dev_info(hw->dev, "Use user's IO configure\n");
		pdata->pins_config();
		return 0;
	}

#ifdef CONFIG_JZ_SPI_PIO_CE
	GPIO_AS_SSI_EX(hw->chnl);
#else
	GPIO_AS_SSI(hw->chnl);
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(jz_spi_pinconfig);

static int jz47xx_spi_init_setup(struct jz47xx_spi *hw)
{
	/* for the moment,open the SSI clock gate */
	CPM_SSI_START(hw->chnl);

	/* disable the SSI controller */
	__ssi_disable(hw->chnl);

	/* set default half_intr trigger */
	hw->tx_trigger = SSI_TX_FIFO_THRESHOLD * 8;
	hw->rx_trigger = SSI_RX_FIFO_THRESHOLD * 8;
	__ssi_set_tx_trigger(hw->chnl, hw->tx_trigger);
	__ssi_set_rx_trigger(hw->chnl, hw->rx_trigger);

	/* First,mask the interrupt, while verify the status ? */
	__ssi_disable_tx_intr(hw->chnl);
	__ssi_disable_rx_intr(hw->chnl);

	__ssi_disable_receive(hw->chnl);

	__ssi_set_spi_clock_phase(hw->chnl, 0);
	__ssi_set_spi_clock_polarity(hw->chnl, 0);
	__ssi_set_msb(hw->chnl);
	__ssi_spi_format(hw->chnl);
	__ssi_set_frame_length(hw->chnl, 8);
	__ssi_disable_loopback(hw->chnl);
	__ssi_flush_fifo(hw->chnl);

	__ssi_underrun_auto_clear(hw->chnl);
	__ssi_clear_errors(hw->chnl);

	__ssi_enable(hw->chnl);

	return 0;
}


static int jz_ssi_clk_setup(struct jz47xx_spi *hw)
{
#ifdef JZ_NEW_CODE_TYPE
	if (hw->pdata->is_pllclk) {
		__ssi_select_pllclk();
	} else {
		__ssi_select_exclk();
	}
	__cpm_set_ssidiv(0);
	hw->src_clk = cpm_get_clock(CGU_SSICLK);
#else
	hw->src_clk = __cpm_get_pllout();
	hw->pdata->is_pllclk = 1;
#endif

	return 0;
}

static int __init jz47xx_spi_probe(struct platform_device *pdev)
{
	struct jz47xx_spi *hw;
	struct spi_master *master;

	struct resource *res;
	int err = 0;

#if	defined(CONFIG_JZ_SPI_BOARD_INFO_REGISTER) || \
	defined(CONFIG_JZ_SPI_PIO_CE)
	int i;
#endif

#ifdef CONFIG_JZ_SPI_BOARD_INFO_REGISTER
	struct spi_board_info *bi;
#endif
#ifdef CONFIG_JZ_SPI_PIO_CE
	int num_cs_got = 0;
#endif

	master = spi_alloc_master(&pdev->dev, sizeof(struct jz47xx_spi));
	if (master == NULL) {
		dev_err(&pdev->dev, "No memory for spi_master\n");
		err = -ENOMEM;
		goto err_nomem;
	}

	hw = spi_master_get_devdata(master);
	memset(hw, 0, sizeof(struct jz47xx_spi));

	hw->g_jz_intr = kzalloc(sizeof(struct jz_intr_cnt), GFP_KERNEL);
	if (hw->g_jz_intr == NULL) {
		dev_err(&pdev->dev, "No memory for jz_intr_cnt\n");
		err = -ENOMEM;
		goto err_g_jz_intr;
	}

	hw->dma_dummy = kzalloc(sizeof(u32), GFP_KERNEL);
	if (hw->dma_dummy == NULL) {
		dev_err(&pdev->dev, "No memory for dma_dummy\n");
		err = -ENOMEM;
		goto err_dma_dummy;
	}

	hw->master = spi_master_get(master);
	hw->dev = &pdev->dev;

	hw->pdata = pdev->dev.platform_data;
	if (hw->pdata == NULL) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		err = -ENOENT;
		goto err_no_pdata;
	}

	hw->chnl	= hw->pdata->chnl;
	hw->use_dma	= hw->pdata->use_dma;

	master->bus_num = (s16)hw->pdata->bus_num;
	if(master->bus_num != 0 && master->bus_num != 1){
		dev_err(&pdev->dev, "No this channel, bus_num= %d.\n", master->bus_num);
		err = -ENOENT;
		goto err_no_pdata;
	}

	/* find and map our resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
		err = -ENOENT;
		goto err_no_iores;
	}
	hw->ioarea = request_mem_region(res->start, resource_size(res),
					pdev->name);

	if (hw->ioarea == NULL) {
		dev_err(&pdev->dev, "Cannot reserve region\n");
		err = -ENXIO;
		goto err_no_iores;
	}
	hw->regs = ioremap(res->start, (res->end - res->start) + 1);
	if (hw->regs == NULL) {
		dev_err(&pdev->dev, "Cannot map IO\n");
		err = -ENXIO;
		goto err_no_iomap;
	}

	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq < 0) {
		dev_err(&pdev->dev, "No IRQ specified\n");
		err = -ENOENT;
		goto err_no_irq;
	}

	/* request SSI irq */
	err = request_irq(hw->irq, jz47xx_spi_irq, 0, pdev->name, hw);
	if (err) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		goto err_no_irq;
	}

	platform_set_drvdata(pdev, hw);
	init_completion(&hw->done);
	spin_lock_init(&hw->lock);
	spin_lock_init(&hw->txrx_lock);

	/* setup the state for the bitbang driver */
	hw->bitbang.master         = hw->master;
	hw->bitbang.setup_transfer = jz47xx_spi_setupxfer;
	hw->bitbang.chipselect     = jz47xx_spi_chipsel;
	hw->bitbang.txrx_bufs      = jz47xx_spi_txrx;
	hw->bitbang.master->setup  = jz47xx_spi_setup;

	dev_dbg(hw->dev, "bitbang at %p\n", &hw->bitbang);

	/* get controller associated params */
	master->bus_num = hw->pdata->bus_num;
	master->num_chipselect = hw->pdata->num_chipselect;

	/* the spi->mode bits understood by this drivers: */
	master->mode_bits = MODEBITS;

	hw->dma_tx_chnl = DMA_INVALID;
	hw->dma_rx_chnl = DMA_INVALID;
	if (hw->use_dma) {
		err = jz_spi_dma_init(hw);
		if (err) {
			dev_err(&pdev->dev, "Cannot claim DMA IRQ\n");
			if (hw->dma_rx_chnl >= 0)
				jz_free_dma(hw->dma_rx_chnl);

			if (hw->dma_tx_chnl >= 0)
				jz_free_dma(hw->dma_tx_chnl);
			hw->use_dma = 0;
		}
	}

#ifdef CONFIG_JZ_SPI_PIO_CE
	/* setup chipselect */
	if (hw->pdata->set_cs)
		hw->set_cs = hw->pdata->set_cs;
	else
		hw->set_cs = jz47xx_spi_cs;

	for (i = 0; i < hw->pdata->num_chipselect; i++, num_cs_got = i) {
		err = gpio_request(hw->pdata->chipselect[i], "JZ47XX_SPI_CS");
		if (err) {
			dev_err(&pdev->dev, "Request cs_gpio: %d is occupied\n",
					hw->pdata->chipselect[i]);
			goto err_cs_gpio;
		}
	}
#else
	hw->set_cs = NULL;
#endif

	/* SSI controller clock configure */
	jz_ssi_clk_setup(hw);
	/* SSI controller initializations for SPI */
	jz47xx_spi_init_setup(hw);
	/* SPI_PINs and chipselect configure */
	err = jz_spi_pinconfig(hw);
	if (err) {
		dev_err(&pdev->dev, "PINs configure error\n");
		goto err_register;
	}

	/* register our spi controller */
	err = spi_bitbang_start(&hw->bitbang);
	if (err) {
		dev_err(&pdev->dev, "Failed to register SPI master ERR_NO:%d\n",err);
		goto err_register;
	}

#ifdef CONFIG_JZ_SPI_BOARD_INFO_REGISTER
	/* register all the devices associated */
	bi = &hw->pdata->board_info[0];
	if(bi){
		for (i = 0; i < hw->pdata->board_size; i++, bi++) {
			dev_info(hw->dev, "registering %s\n", bi->modalias);

			bi->controller_data = hw;
			spi_new_device(master, bi);
		}
	}
#endif

	printk(KERN_INFO
			"JZ47xx SSI Controller for SPI channel %d driver register\n",hw->chnl);

	return 0;

err_register:
	CPM_SSI_STOP(hw->chnl);

	free_irq(hw->irq, hw);

#ifdef CONFIG_JZ_SPI_PIO_CE
err_cs_gpio:
	for (i = 0; i < num_cs_got; i++)
		gpio_free(hw->pdata->chipselect[i]);
#endif

	if (hw->dma_rx_chnl >= 0)
		jz_free_dma(hw->dma_rx_chnl);

	if (hw->dma_tx_chnl >= 0)
		jz_free_dma(hw->dma_tx_chnl);

err_no_irq:
	iounmap(hw->regs);

err_no_iomap:
	release_resource(hw->ioarea);
	kfree(hw->ioarea);

err_no_iores:
err_no_pdata:
	kfree(hw->dma_dummy);

err_dma_dummy:
	kfree(hw->g_jz_intr);

err_g_jz_intr:
	spi_master_put(hw->master);;

err_nomem:
	return err;
}

static int __exit jz47xx_spi_remove(struct platform_device *dev)
{
	struct jz47xx_spi *hw = platform_get_drvdata(dev);

	spi_master_put(hw->master);
	spi_bitbang_stop(&hw->bitbang);

	__ssi_disable_tx_intr(hw->chnl);
	__ssi_disable_rx_intr(hw->chnl);
	__ssi_disable(hw->chnl);
	CPM_SSI_STOP(hw->chnl);

	platform_set_drvdata(dev, NULL);

	free_irq(hw->irq, hw);
	iounmap(hw->regs);

	release_resource(hw->ioarea);
	kfree(hw->ioarea);

	/* release DMA channel */
	if (hw->dma_rx_chnl >= 0) {
		jz_free_dma(hw->dma_rx_chnl);
		printk("dma_rx_chnl release\n");
	}
	if (hw->dma_tx_chnl >= 0) {
		jz_free_dma(hw->dma_tx_chnl);
		printk("dma_tx_chnl release\n");
	}

	kfree(hw->dma_dummy);
	kfree(hw->g_jz_intr);
	kfree(hw);
	printk(KERN_INFO
			"JZ47xx SSI Controller for SPI channel %d driver removed\n",hw->chnl);

	return 0;
}

#ifdef CONFIG_PM
static int jz47xx_spi_suspend(struct platform_device *pdev, pm_message_t msg)
{
	struct jz47xx_spi *hw = platform_get_drvdata(pdev);
	unsigned long flags;

        spin_lock_irqsave(&hw->lock, flags);
	hw->state |= SUSPND;
	spin_unlock_irqrestore(&hw->lock, flags);

	while (hw->state & SPIBUSY)
		printk("Now spi is busy, waitting!\n");

	CPM_SSI_STOP(hw->chnl);

	return 0;
}

static int jz47xx_spi_resume(struct platform_device *pdev)
{
	struct jz47xx_spi *hw = platform_get_drvdata(pdev);
	unsigned long flags;

	CPM_SSI_START(hw->chnl);

	spin_lock_irqsave(&hw->lock, flags);
	hw->state &= ~SUSPND;
	spin_unlock_irqrestore(&hw->lock, flags);

	return 0;
}
#else
#define jz47xx_spi_suspend NULL
#define jz47xx_spi_resume  NULL
#endif

MODULE_ALIAS("jz47xx_spi");			/* for platform bus hotplug */
static struct platform_driver jz47xx_spidrv = {
	.remove		= __exit_p(jz47xx_spi_remove),
	.suspend	= jz47xx_spi_suspend,
	.resume		= jz47xx_spi_resume,
	.driver		= {
		.name	= "jz47xx-spi",
		.owner	= THIS_MODULE,
	},
};
static int __init jz47xx_spi_init(void)
{
        return platform_driver_probe(&jz47xx_spidrv, jz47xx_spi_probe);
}

static void __exit jz47xx_spi_exit(void)
{
	platform_driver_unregister(&jz47xx_spidrv);
	printk(KERN_INFO "JZ47xx SSI Controller Module EXIT\n");

}
module_init(jz47xx_spi_init);
module_exit(jz47xx_spi_exit);

MODULE_DESCRIPTION("JZ47XX SPI Driver");
MODULE_LICENSE("GPL");

