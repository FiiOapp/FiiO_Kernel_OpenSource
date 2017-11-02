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




#ifndef __gc_hal_kernel_os_h_
#define __gc_hal_kernel_os_h_

typedef struct _LINUX_MDL_MAP
{
	gctINT					pid;
	gctPOINTER				vmaAddr;
	struct vm_area_struct *	vma;
	struct _LINUX_MDL_MAP *	next;
}
LINUX_MDL_MAP, *PLINUX_MDL_MAP;

typedef struct _LINUX_MDL
{
	gctINT					pid;
	char *					addr;

#ifdef NO_DMA_COHERENT
	gctPOINTER				kaddr;
#endif /* NO_DMA_COHERENT */

	gctINT					numPages;
	gctINT					pagedMem;
	gctBOOL					contiguous;
	dma_addr_t				dmaHandle;
	PLINUX_MDL_MAP			maps;
	struct _LINUX_MDL *		prev;
	struct _LINUX_MDL *		next;
}
LINUX_MDL, *PLINUX_MDL;

extern PLINUX_MDL_MAP
FindMdlMap(
	IN PLINUX_MDL Mdl,
	IN gctINT PID
	);

typedef struct _DRIVER_ARGS
{
	gctPOINTER 				InputBuffer;
	gctUINT32  				InputBufferSize;
	gctPOINTER 				OutputBuffer;
	gctUINT32				OutputBufferSize;
}
DRIVER_ARGS;

/* Destroy all user signals of the current process */
gceSTATUS
gckOS_DestroyAllUserSignals(
     IN gckOS Os
     );
	
#ifdef gcdkUSE_MEMORY_RECORD
MEMORY_RECORD_PTR
CreateMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List,
    gceMEMORY_TYPE Type,
	gctSIZE_T Bytes,
	gctPHYS_ADDR Physical,
	gctPOINTER Logical
    );

void
DestroyMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR Mr
    );

MEMORY_RECORD_PTR
FindMemoryRecord(
    gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
    MEMORY_RECORD_PTR List,
    gceMEMORY_TYPE Type,
	gctSIZE_T Bytes,
	gctPHYS_ADDR Physical,
	gctPOINTER Logical
    );

MEMORY_RECORD_PTR
CreateVideoMemoryRecord(
	gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
	MEMORY_RECORD_PTR List,
	gcuVIDMEM_NODE_PTR Node,
	gceSURF_TYPE Type,
	gctSIZE_T Bytes
	);

void
DestroyVideoMemoryRecord(
	gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
	MEMORY_RECORD_PTR Mr
	);
	
MEMORY_RECORD_PTR
FindVideoMemoryRecord(
	gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
	MEMORY_RECORD_PTR List,
	gcuVIDMEM_NODE_PTR Node
	);
	
void 
FreeAllMemoryRecord(
	gckOS Os,
    gcsHAL_PRIVATE_DATA_PTR private,
	MEMORY_RECORD_PTR List
	);
#endif
	
#endif /* __gc_hal_kernel_os_h_ */
