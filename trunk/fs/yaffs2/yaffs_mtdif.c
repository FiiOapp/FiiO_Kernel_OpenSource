/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2007 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

const char *yaffs_mtdif_c_version =
    "$Id: yaffs_mtdif.c,v 1.1.1.1 2008-03-28 04:29:21 jlwei Exp $";

#include "yportenv.h"
#include "yaffs_mtdif.h"
#include "linux/mtd/mtd.h"
#include "linux/types.h"
#include "linux/time.h"
#include "linux/mtd/nand.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
static struct nand_oobinfo yaffs_oobinfo = {
	.useecc = 1,
	.eccbytes = 6,
	.eccpos = {8, 9, 10, 13, 14, 15}
};

static struct nand_oobinfo yaffs_noeccinfo = {
	.useecc = 0,
};
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
static inline void translate_spare2oob(const yaffs_Spare *spare, __u8 *oob)
{
	memcpy(oob,spare,sizeof(yaffs_Spare));
}

static inline void translate_oob2spare(yaffs_Spare *spare, __u8 *oob)
{
	struct yaffs_NANDSpare *nspare = (struct yaffs_NANDSpare *)spare;
	memcpy(spare, oob, sizeof(yaffs_Spare));
	nspare->eccres1 = nspare->eccres2 = 0; /* FIXME */
}
#endif

int nandmtd_WriteChunkToNAND(yaffs_Device * dev, int chunkInNAND,
			     const __u8 * data, const yaffs_Spare * spare)
{
	struct mtd_info *mtd = (struct mtd_info *)(dev->genericDevice);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	struct mtd_oob_ops ops;
#endif
	size_mtd_t dummy;
	int retval = 0;

	loff_mtd_t addr = ((loff_mtd_t) chunkInNAND) * dev->nDataBytesPerChunk;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	__u8 spareAsBytes[YAFFS_BYTES_PER_SPARE]; /* OOB */
	if (data && !spare)
		retval = mtd->write(mtd, addr, dev->nDataBytesPerChunk,
				&dummy, data);
	else if (spare) {
		if (dev->useNANDECC) {
			translate_spare2oob(spare, spareAsBytes);
			ops.mode = MTD_OOB_PLACE;
			ops.ooblen = YAFFS_BYTES_PER_SPARE;
			ops.oobbuf = spareAsBytes;
		} else {
			ops.mode = MTD_OOB_RAW;
			ops.ooblen = YAFFS_BYTES_PER_SPARE;
			ops.oobbuf = (__u8 *)spare;
		}
		ops.len = data ? dev->nDataBytesPerChunk : ops.ooblen;
		ops.datbuf = (u8 *)data;
		ops.ooboffs = 0;
		retval = mtd->write_oob(mtd, addr, &ops);
	}
#else
	__u8 *spareAsBytes = (__u8 *) spare;

	if (data && spare) {
		if (dev->useNANDECC)
			retval =
			    mtd->write_ecc(mtd, addr, dev->nDataBytesPerChunk,
					   &dummy, data, spareAsBytes,
					   &yaffs_oobinfo);
		else
			retval =
			    mtd->write_ecc(mtd, addr, dev->nDataBytesPerChunk,
					   &dummy, data, spareAsBytes,
					   &yaffs_noeccinfo);
	} else {
		if (data)
			retval =
			    mtd->write(mtd, addr, dev->nDataBytesPerChunk, &dummy,
				       data);
		if (spare)
			retval =
			    mtd->write_oob(mtd, addr, YAFFS_BYTES_PER_SPARE,
					   &dummy, spareAsBytes);
	}
#endif
	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

int nandmtd_ReadChunkFromNAND(yaffs_Device * dev, int chunkInNAND, __u8 * data,
			      yaffs_Spare * spare)
{
	struct mtd_info *mtd = (struct mtd_info *)(dev->genericDevice);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	struct mtd_oob_ops ops;
#endif
	size_mtd_t dummy;
	int retval = 0;

	loff_mtd_t addr = ((loff_mtd_t) chunkInNAND) * dev->nDataBytesPerChunk;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17))
	__u8 spareAsBytes[YAFFS_BYTES_PER_SPARE]; /* OOB */

	if (data && !spare)
		retval = mtd->read(mtd, addr, dev->nDataBytesPerChunk,
				&dummy, data);
	else if (spare) {
		if (dev->useNANDECC) {
			ops.mode = MTD_OOB_PLACE;
			ops.ooblen = YAFFS_BYTES_PER_SPARE;
			ops.oobbuf = spareAsBytes;
		} else {
			ops.mode = MTD_OOB_RAW;
			ops.ooblen = YAFFS_BYTES_PER_SPARE;
			ops.oobbuf = (__u8 *)spare;
		}
		ops.len = data ? dev->nDataBytesPerChunk : ops.ooblen;
		ops.datbuf = data;
		ops.ooboffs = 0;
		retval = mtd->read_oob(mtd, addr, &ops); 

		if (dev->useNANDECC)
			translate_oob2spare(spare, spareAsBytes);
	}
#else
	__u8 *spareAsBytes = (__u8 *) spare;

	if (data && spare) {
		if (dev->useNANDECC) {	
			/* Careful, this call adds 2 ints */
			/* to the end of the spare data.  Calling function */
			/* should allocate enough memory for spare, */
			/* i.e. [YAFFS_BYTES_PER_SPARE+2*sizeof(int)]. */
			retval =
			    mtd->read_ecc(mtd, addr, dev->nDataBytesPerChunk,
					  &dummy, data, spareAsBytes,
					  &yaffs_oobinfo);
		} else {
			retval =
			    mtd->read_ecc(mtd, addr, dev->nDataBytesPerChunk,
					  &dummy, data, spareAsBytes,
					  &yaffs_noeccinfo);
		}
	} else {
		if (data)
			retval =
			    mtd->read(mtd, addr, dev->nDataBytesPerChunk, &dummy,
				      data);
		if (spare)
			retval =
			    mtd->read_oob(mtd, addr, YAFFS_BYTES_PER_SPARE,
					  &dummy, spareAsBytes);
	}
#endif

	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

int nandmtd_EraseBlockInNAND(yaffs_Device * dev, int blockNumber)
{
	struct mtd_info *mtd = (struct mtd_info *)(dev->genericDevice);

	__u64 addr =
	    ((loff_mtd_t) blockNumber) * dev->nDataBytesPerChunk
		* dev->nChunksPerBlock;

	struct erase_info ei;
	int retval = 0;

	ei.mtd = mtd;
	ei.addr = addr;
	ei.len = dev->nDataBytesPerChunk * dev->nChunksPerBlock;
	ei.time = 1000;
	ei.retries = 2;
	ei.callback = NULL;
	ei.priv = (u_long) dev;

	/* Todo finish off the ei if required */

	sema_init(&dev->sem, 0);

	retval = mtd->erase(mtd, &ei);

	if (retval == 0)
		return YAFFS_OK;
	else
		return YAFFS_FAIL;
}

int nandmtd_InitialiseNAND(yaffs_Device * dev)
{
	return YAFFS_OK;
}

