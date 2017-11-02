/****************************************************************************
*  
*    Copyright (c) 2005 - 2012 by Vivante Corp.  All rights reserved.
*  
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed, 
*    reproduced, copied, transmitted, or used in any way for any purpose, 
*    without the express written permission of Vivante Corporation.
*  
*****************************************************************************/




#ifndef __gc_hal_kernel_device_h_
#define __gc_hal_kernel_device_h_

#define gcdkUSE_MEMORY_RECORD		1

#ifndef gcdkREPORT_VIDMEM_USAGE
#define gcdkREPORT_VIDMEM_USAGE		0
#endif

#ifdef ANDROID
#define gcdkREPORT_VIDMEM_LEAK		0
#else
#define gcdkREPORT_VIDMEM_LEAK		1
#endif

/******************************************************************************\
******************************* gckGALDEVICE Structure *******************************
\******************************************************************************/

typedef struct _gckGALDEVICE
{
	/* Objects. */
	gckOS				os;
	gckKERNEL			kernel;

	/* Attributes. */
	gctSIZE_T			internalSize;
	gctPHYS_ADDR		internalPhysical;
	gctPOINTER			internalLogical;
	gckVIDMEM			internalVidMem;
	gctSIZE_T			externalSize;
	gctPHYS_ADDR		externalPhysical;
	gctPOINTER			externalLogical;
	gckVIDMEM			externalVidMem;
	gckVIDMEM			contiguousVidMem;
	gctPOINTER			contiguousBase;
	gctPHYS_ADDR		contiguousPhysical;
	gctSIZE_T			contiguousSize;
	gctBOOL				contiguousMapped;
	gctPOINTER			contiguousMappedUser;
	gctSIZE_T			systemMemorySize;
	gctUINT32			systemMemoryBaseAddress;
	gctPOINTER			registerBase;
	gctSIZE_T			registerSize;
	gctUINT32			baseAddress;

	/* IRQ management. */
	gctINT				irqLine;
	gctBOOL				isrInitialized;
	gctBOOL				dataReady;

	/* Thread management. */
	struct task_struct	*threadCtxt;
	struct semaphore	sema;
	gctBOOL				threadInitialized;
	gctBOOL				killThread;

	/* Signal management. */
	gctINT				signal;
}
* gckGALDEVICE;

#if gcdkUSE_MEMORY_RECORD
typedef enum _gceMEMORY_TYPE
{
    gcvNON_PAGED_MEMORY     = 0,
    gcvCONTIGUOUS_MEMORY,
    gcvVIDEO_MEMORY
}
gceMEMORY_TYPE;

typedef struct MEMORY_RECORD
{
    gceMEMORY_TYPE          type;

    union
    {
        struct
        {
	        gctSIZE_T               bytes;
	        gctPHYS_ADDR            physical;
	        gctPOINTER              logical;
        }
        Memory;

        struct
        {
	        gcuVIDMEM_NODE_PTR		node;
	        gceSURF_TYPE            type;
	        gctSIZE_T				bytes;
        }
        VideoMemory;
    }
    u;

	struct MEMORY_RECORD *	prev;
	struct MEMORY_RECORD *	next;
}
MEMORY_RECORD, * MEMORY_RECORD_PTR;
#endif

typedef struct _gcsHAL_PRIVATE_DATA
{
    gckGALDEVICE		device;
    gctPOINTER			mappedMemory;
	gctPOINTER			contiguousLogical;

#if gcdkUSE_MEMORY_RECORD
	MEMORY_RECORD		memoryRecordList;

#if gcdkREPORT_VIDMEM_USAGE
    gctUINT64           allocatedMem[gcvSURF_NUM_TYPES];
    gctUINT64           maxAllocatedMem[gcvSURF_NUM_TYPES];
    gctUINT64           totalAllocatedMem;
    gctUINT64           maxTotalAllocatedMem;
#endif
#endif
}
gcsHAL_PRIVATE_DATA, * gcsHAL_PRIVATE_DATA_PTR;

gceSTATUS gckGALDEVICE_Setup_ISR(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Release_ISR(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Start_Thread(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Stop_Thread(
	gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Start(
	IN gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Stop(
	gckGALDEVICE Device
	);

gceSTATUS gckGALDEVICE_Construct(
	IN gctINT IrqLine,
	IN gctUINT32 RegisterMemBase,
	IN gctSIZE_T RegisterMemSize,
	IN gctUINT32 ContiguousBase,
	IN gctSIZE_T ContiguousSize,
	IN gctSIZE_T BankSize,
	IN gctINT FastClear,
	IN gctINT Compression,
	IN gctUINT32 BaseAddress,
	IN gctINT Signal,
	OUT gckGALDEVICE *Device
	);

gceSTATUS gckGALDEVICE_Destroy(
	IN gckGALDEVICE Device
	);

#endif /* __gc_hal_kernel_device_h_ */
