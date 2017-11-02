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




#ifndef __gc_hal_kernel_hardware_h_
#define __gc_hal_kernel_hardware_h_

#ifdef __cplusplus
extern "C" {
#endif

/* gckHARDWARE object. */
struct _gckHARDWARE
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gctKERNEL object. */
    gckKERNEL                   kernel;

    /* Pointer to gctOS object. */
    gckOS                       os;

    /* Chip characteristics. */
    gceCHIPMODEL                chipModel;
    gctUINT32                   chipRevision;
    gctUINT32                   chipFeatures;
    gctUINT32                   chipMinorFeatures0;
    gctUINT32                   chipMinorFeatures1;
    gctUINT32                   chipMinorFeatures2;
    gctBOOL                     allowFastClear;
    gctBOOL                     allowCompression;
    gctUINT32                   powerBaseAddress;
    gctBOOL                     extraEventStates;

    gctUINT32                   streamCount;
    gctUINT32                   registerMax;
    gctUINT32                   threadCount;
    gctUINT32                   shaderCoreCount;
    gctUINT32                   vertexCacheSize;
    gctUINT32                   vertexOutputBufferSize;

    /* Big endian */
    gctBOOL                     bigEndian;

    /* Chip status */
    gctPOINTER                  powerMutex;
    gctUINT32                   powerProcess;
    gctUINT32                   powerThread;
    gceCHIPPOWERSTATE           chipPowerState;
    gctBOOL                     broadcast;
    gctBOOL                     settingPowerState;
    gctUINT32                   lastWaitLink;

    gctISRMANAGERFUNC           startIsr;
    gctISRMANAGERFUNC           stopIsr;
    gctPOINTER                  isrContext;
};

gceSTATUS
gckHARDWARE_GetBaseAddress(
    IN gckHARDWARE Hardware, 
    OUT gctUINT32_PTR BaseAddress
    );

gceSTATUS
gckHARDWARE_NeedBaseAddress(
    IN gckHARDWARE Hardware, 
    IN gctUINT32 State, 
    OUT gctBOOL_PTR NeedBase
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_hardware_h_ */

