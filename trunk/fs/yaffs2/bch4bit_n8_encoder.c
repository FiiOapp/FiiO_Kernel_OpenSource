/*******************************************************************************
*
*    File Name:  bch_encoder.c
*     Revision:  1.0
*         Date:  August, 2006
*        Email:  nandsupport@micron.com
*      Company:  Micron Technology, Inc.
*
*  Description:  Micron NAND BCH Encoder
*
*   References: 
* 		  1. Error Control Coding, Lin & Costello, 2nd Ed., 2004
* 		  2. Error Control Codes, Blahut, 1983
**
*   Disclaimer   This software code and all associated documentation, comments or other 
*  of Warranty:  information (collectively "Software") is provided "AS IS" without 
*                warranty of any kind. MICRON TECHNOLOGY, INC. ("MTI") EXPRESSLY 
*                DISCLAIMS ALL WARRANTIES EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
*                TO, NONINFRINGEMENT OF THIRD PARTY RIGHTS, AND ANY IMPLIED WARRANTIES 
*                OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE. MTI DOES NOT 
*                WARRANT THAT THE SOFTWARE WILL MEET YOUR REQUIREMENTS, OR THAT THE 
*                OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE. 
*                FURTHERMORE, MTI DOES NOT MAKE ANY REPRESENTATIONS REGARDING THE USE OR 
*                THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS, 
*                ACCURACY, RELIABILITY, OR OTHERWISE. THE ENTIRE RISK ARISING OUT OF USE 
*                OR PERFORMANCE OF THE SOFTWARE REMAINS WITH YOU. IN NO EVENT SHALL MTI, 
*                ITS AFFILIATED COMPANIES OR THEIR SUPPLIERS BE LIABLE FOR ANY DIRECT, 
*                INDIRECT, CONSEQUENTIAL, INCIDENTAL, OR SPECIAL DAMAGES (INCLUDING, 
*                WITHOUT LIMITATION, DAMAGES FOR LOSS OF PROFITS, BUSINESS INTERRUPTION, 
*                OR LOSS OF INFORMATION) ARISING OUT OF YOUR USE OF OR INABILITY TO USE 
*                THE SOFTWARE, EVEN IF MTI HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH 
*                DAMAGES. Because some jurisdictions prohibit the exclusion or 
*                limitation of liability for consequential or incidental damages, the 
*                above limitation may not apply to you.
*
*                Copyright 2006 Micron Technology, Inc. All rights reserved.
*
* Rev  Author			Date		Changes
* ---  ---------------	----------	-------------------------------
* 1.0  ZS		08/07/2006	Initial release
* 
* 
*******************************************************************************/

#include "mtd/mtd_bch4bit_n8.h"

#if BCH4_DEBUG
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#endif

static unsigned char spdata[kk_max];	// Information data and received data
static void data_encode_bch(unsigned char *bit_data, unsigned char *paritybuf, int len)
/* Parallel computation of n - k parity check bits.
 * Use lookahead matrix T_G_R.
 * The incoming streams are fed into registers from the right hand
 */
{
  int iii;
  unsigned int Temp, bb_temp, bb_pack;
#if BCH4_DEBUG
  int bb_idx, idx, bb_bit[rr_max];
  unsigned char *pp2, ch;
#endif
	
  // Initialize the parity bits.
  bb_pack = 0;
	
  // Compute parity checks
  // S(t) = T_G_R [ S(t-1) + M(t) ]
  // Ref: Parallel CRC, Shieh, 2001
  for (iii = len - 1; iii >= 0; iii--)
  {	
    bb_pack = bb_pack ^ bit_data[iii];
    bb_temp = (bb_pack >> 1);
    Temp = (bb_pack & 1) * 0xF72DA17E;
    bb_pack = bb_temp ^ Temp;
  }

#if BCH4_DEBUG
  idx = 0;
  pp2 = (unsigned char *)&bb_pack;
  for (i = 0; i < rr; i += 8)
  {
    ch = bb_pack >> 24;
    bb_bit[i + 7] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 6] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 5] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 4] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 3] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 2] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 1] = ch & 1;
    ch = ch >> 1;
    bb_bit[i + 0] = ch & 1;
    bb_pack = bb_pack << 8;
  }

  printf(" Parity bb \n");
  for (i = rr-1; i >= 0; i--)
    printf("%d", bb_bit[i]);
  printf(" \n");
#endif

  paritybuf[0] = (bb_pack >> 24);
  paritybuf[1] = (bb_pack >> 16);
  paritybuf[2] = (bb_pack >>  8);
  paritybuf[3] = (bb_pack >>  0);

#if BCH4_DEBUG
  for (i = 0; i < bb_idx; i++)
    printf("0x%02x, ", paritybuf[i]);
  printf(" \n");
#endif
}

void do_bch_encode (unsigned char *inbuf, unsigned char *paritybuf, int len)
{
  int in_count, i;
  unsigned char ch;
  /* split bits from data and store in data array [].  */
  in_count = 0;
  for (i = 0; i < len; i++)
  {
    ch = inbuf[i];

    spdata[in_count + 7] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 6] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 5] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 4] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 3] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 2] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 1] = ch & 1;
    ch = ch >> 1;
    spdata[in_count + 0] = ch & 1;

    in_count += 8;
  }
  
  /* encode data bch.  */
  data_encode_bch(spdata, paritybuf, len * 8);
}

