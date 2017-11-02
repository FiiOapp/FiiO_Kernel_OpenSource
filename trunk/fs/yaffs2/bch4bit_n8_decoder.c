/*******************************************************************************
*
*    File Name:  bch_decoder.c
*     Revision:  2.0
*         Date:  March, 2007
*        Email:  nandsupport@micron.com
*      Company:  Micron Technology, Inc.
*
*  Description:  Micron NAND BCH Decoder
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
*                Copyright 2007 Micron Technology, Inc. All rights reserved.
*
* Rev  Author			Date		Changes
* ---  ---------------	----------	-------------------------------
* 1.0  ZS		08/07/2006	Initial release
* 2.0  PF		03/05/2007	Fixed bug that caused some codewords
* 					to not be corrected
* 
* 
*******************************************************************************/

#include "mtd/mtd_bch4bit_n8.h"

#if BCH4_DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#endif

int bb[rr_max] ;	// Syndrome polynomial
int s[rr_max];		// Syndrome values
int syn_error;		// Syndrome error indicator
int location[tt_max];	// Error location
static int ttx2;		// 2t

static unsigned char spdata[nn_max];
static void data_syndrome(unsigned char *bit_data, int len)
{
/* Parallel computation of 2t syndromes.
 * Use the same lookahead matrix T_G_R of parallel computation of parity check bits.
 * The incoming streams are fed into registers from left hand
 */
  int i, j, iii, idx;                                                               
  unsigned int Temp, bb_temp, bb_pack;                                              
  unsigned char ch;           

  // Initialize the parity bits.
  bb_pack = 0;
	
  // Compute syndrome polynomial: S(x) = C(x) mod g(x)
  // S(t) = T_G_R S(t-1) + R(t) 
  // Ref: L&C, pp. 225, Fig. 6.11
  for (iii = (len + rr) - 1; iii >= 0; iii--) 
  {
    bb_temp = (bb_pack >> 1);
    Temp = (bb_pack & 1) * 0xF72DA17E;
    bb_temp = bb_temp ^ (bit_data[iii] << 31);
    bb_pack = bb_temp ^ Temp;
  }

#if 1                                                                               
  idx = 0;                                                                          
  for (i = 0; i < rr; i += 8)                                                       
  {                                                                                 
    ch = bb_pack >> 24;                                                             
    bb[i + 7] = ch & 1;                                                             
    ch = ch >> 1;                                                                   
    bb[i + 6] = ch & 1;                                                             
    ch = ch >> 1;                                                                   
    bb[i + 5] = ch & 1;
    ch = ch >> 1;
    bb[i + 4] = ch & 1;
    ch = ch >> 1;
    bb[i + 3] = ch & 1;
    ch = ch >> 1;
    bb[i + 2] = ch & 1;
    ch = ch >> 1;
    bb[i + 1] = ch & 1;
    ch = ch >> 1;
    bb[i + 0] = ch & 1;
    bb_pack = bb_pack << 8;
  }
#endif

	
  // Computation 2t syndromes based on S(x)
  // Odd syndromes
#if 0
  syn_error = 0 ;
  for (i = 1; i <= ttx2 - 1; i = i+2) 
  {
    s[i] = 0 ;
    for (j = 0; j < rr; j++)
      if (bb[j] != 0)
	s[i] ^= alpha_to[(index_of[bb[j]] + i*j) % nn] ;
    if (s[i] != 0)
      syn_error = 1 ;	// set flag if non-zero syndrome => error
  }
#else
  syn_error = 0 ;
  s[1] = s[3] = s[5] = s[7] = 0;
  for (j = 0; j < rr; j++)
  {
    if (bb[j] != 0)
    {
      s[1] ^= alpha_to[j * 1];
      s[3] ^= alpha_to[j * 3];
      s[5] ^= alpha_to[j * 5];
      s[7] ^= alpha_to[j * 7];
    }
  }
  if (s[1] != 0 || s[3] != 0 || s[5] != 0 || s[7] != 0)
    syn_error = 1 ;     // set flag if non-zero syndrome => error
#endif

  // Even syndrome = (Odd syndrome) ** 2
  for (i = 2; i <= ttx2; i = i + 2) 
  {
    j = i / 2;
    if (s[j] == 0)
      s[i] = 0;
    else
      s[i] =  alpha_to[(2 * index_of[s[j]]) % nn];
  }
	
#if BCH4_DEBUG
  {
    printf("# The syndrome from parallel decoder is:\n");
    for (i = 1; i <= ttx2; i++)
      printf("   %4d (%4d) == 0x%04x (0x%x)\n", s[i],index_of[s[i]],s[i], index_of[s[i]]) ;
    printf("\n\n") ;
  }
#endif
}

static int data_decode_bch(unsigned char *bit_data, unsigned int *error_pos, int len)
{
  register int i, j, elp_sum ;
  int L[ttx2+3];			// Degree of ELP 
  int u_L[ttx2+3];		// Difference between step number and the degree of ELP
  int reg[tt+3];			// Register state
  int elp[ttx2+4][ttx2+4]; 	// Error locator polynomial (ELP)
  int desc[ttx2+4];		// Discrepancy 'mu'th discrepancy
  int u;				// u = 'mu' + 1 and u ranges from -1 to 2*t (see L&C)
  int q, flag;				//
  int err_count;

  data_syndrome(bit_data, len);
	
  if (!syn_error) 
  {
    flag = 1 ;	// No errors
    *error_pos = 0;
  }
  else 
  {	
    // Having errors, begin decoding procedure
    // Simplified Berlekamp-Massey Algorithm for Binary BCH codes
    // Ref: Blahut, pp.191, Chapter 7.6 
    // Ref: L&C, pp.212, Chapter 6.4
    //
    // Following the terminology of Lin and Costello's book:   
    // desc[u] is the 'mu'th discrepancy, where  
    // u='mu'+1 and 
    // 'mu' (the Greek letter!) is the step number ranging 
    // from -1 to 2*t (see L&C)
    // l[u] is the degree of the elp at that step, and 
    // u_L[u] is the difference between the step number 
    // and the degree of the elp. 
		
#if BCH4_DEBUG
    printf("Beginning Berlekamp loop\n");
#endif

    // initialise table entries
    for (i = 1; i <= ttx2; i++) 
      s[i] = index_of[s[i]];

    desc[0] = 0;				/* index form */
    desc[1] = s[1];				/* index form */
    elp[0][0] = 1;				/* polynomial form */
    elp[1][0] = 1;				/* polynomial form */
    //elp[2][0] = 1;				/* polynomial form */
    for (i = 1; i < ttx2; i++) 
    {
      elp[0][i] = 0;			/* polynomial form */
      elp[1][i] = 0;			/* polynomial form */
      //elp[2][i] = 0;			/* polynomial form */
    }
    L[0] = 0;
    L[1] = 0;
    //L[2] = 0;
    u_L[0] = -1;
    u_L[1] = 0;
    //u_L[2] = 0;
    u = -1; 

    do {
      // even loops always produce no discrepany so they can be skipped
      u = u + 2; 
#if BCH4_DEBUG
      printf("Loop %d:\n", u);
      printf("     desc[%d] = %x\n", u, desc[u]);
#endif
      if (desc[u] == -1) 
      {
        L[u + 2] = L[u];
        for (i = 0; i <= L[u]; i++)
          elp[u + 2][i] = elp[u][i]; 
      }
      else 
      {
        // search for words with greatest u_L[q] for which desc[q]!=0 
        q = u - 2;
        if (q<0) q=0;
        // Look for first non-zero desc[q] 
        while ((desc[q] == -1) && (q > 0))
          q=q-2;

        if (q < 0) 
          q = 0;

	// Find q such that desc[u]!=0 and u_L[q] is maximum
        if (q > 0) 
        {
          j = q;
          do {
            j = j - 2;
            if (j < 0) j = 0;
            if ((desc[j] != -1) && (u_L[q] < u_L[j]))
              q = j;
          } while (j > 0);
        }
 
        // store degree of new elp polynomial
        if (L[u] > L[q] + u - q)
          L[u + 2] = L[u];
        else
          L[u + 2] = L[q] + u - q;
 
        // Form new elp(x)
        for (i = 0; i < ttx2; i++) 
          elp[u + 2][i] = 0;
        for (i = 0; i <= L[q]; i++) 
          if (elp[q][i] != 0)
            elp[u + 2][i + u - q] = alpha_to[(desc[u] + nn - desc[q] + index_of[elp[q][i]]) % nn];

        for (i = 0; i <= L[u]; i++) 
          elp[u + 2][i] ^= elp[u][i];
      }

      u_L[u + 2] = u+1 - L[u + 2];
 
      // Form (u+2)th discrepancy.  No discrepancy computed on last iteration 
      if (u < ttx2) 
      {
        if (s[u + 2] != -1)
          desc[u + 2] = alpha_to[s[u + 2]];
        else 
          desc[u + 2] = 0;

        for (i = 1; i <= L[u + 2]; i++) 
          if ((s[u + 2 - i] != -1) && (elp[u + 2][i] != 0))
            desc[u + 2] ^= alpha_to[(s[u + 2 - i] + index_of[elp[u + 2][i]]) % nn];
        // put desc[u+2] into index form 
        desc[u + 2] = index_of[desc[u + 2]];	

      }

#if BCH4_DEBUG
      {
        printf("     deg(elp) = %2d --> elp(%2d):", L[u], u);
        for (i=0; i<=L[u]; i++)
          printf("  0x%x", elp[u][i]);
        printf("\n");
        printf("     deg(elp) = %2d --> elp(%2d):", L[u+2], u+2);
        for (i=0; i<=L[u+2]; i++)
          printf("  0x%x", elp[u+2][i]);
        printf("\n");
        printf("     u_L[%2d] = %2d\n", u, u_L[u]);
        printf("     u_L[%2d] = %2d\n", u+2, u_L[u+2]);
      }
#endif
    } while ((u < (ttx2-1)) && (L[u + 2] <= tt)); 


#if BCH4_DEBUG
    printf("\n");
#endif
    u=u+2;
    L[ttx2-1] = L[u];
		
    if (L[ttx2-1] > tt) 
      flag = 0;
    else 
    {
      // Chien's search to find roots of the error location polynomial
      // Ref: L&C pp.216, Fig.6.1
#if BCH4_DEBUG
      printf("Chien Search:  L[%d]=%d=%x\n", ttx2-1,L[ttx2-1],L[ttx2-1]);
      printf("Sigma(x) = \n");

      for (i = 0; i <= L[u]; i++) 
        if (elp[u][i] != 0)
          printf("    %4d (%4d)\n", elp[u][i], index_of[elp[u][i]]);
        else
          printf("     0\n");
#endif

      for (i = 1; i <= L[ttx2-1]; i++) 
      {
        reg[i] = index_of[elp[u][i]];
#if BCH4_DEBUG
        printf("  reg[%d]=%d=%x\n", i,reg[i],reg[i]);
#endif
      }

      err_count = 0 ;
      // Begin chien search 
      for (i = 1; i <= nn; i++) 
      {
        elp_sum = 1 ;
        for (j = 1; j <= L[ttx2-1]; j++) 
          if (reg[j] != -1) 
          {
            reg[j] = (reg[j] + j) % nn ;
            elp_sum ^= alpha_to[reg[j]] ;

          }

        // store root and error location number indices
        if (!elp_sum) 
        {
          err_count++ ;
          *(error_pos + err_count) = nn - i;
        }
      }

      // Number of roots = degree of elp hence <= tt errors
      if (err_count == L[ttx2-1]) 
      {   
        flag = 1;
        *error_pos = err_count;
      }
      // Number of roots != degree of ELP => >tt errors and cannot solve
      else 
        flag = 0;
    }
  }
  return flag;
}


int do_bch_decode (unsigned char *inbuf, unsigned char *paritybuf, unsigned int *error_pos, int len)
{
  int in_count, i, j, idx;
  unsigned char ch;

  ttx2 = 2 * tt;
  /* split bits from paritybuf and store in data array[].  */
  idx = 0;
  for (i = 0; i <= rr - 8; i += 8)
  {
    ch = paritybuf[idx++];
    spdata[i + 7] = ch & 1;
    ch = ch >> 1;
    spdata[i + 6] = ch & 1;
    ch = ch >> 1;
    spdata[i + 5] = ch & 1;
    ch = ch >> 1;
    spdata[i + 4] = ch & 1;
    ch = ch >> 1;
    spdata[i + 3] = ch & 1;
    ch = ch >> 1;
    spdata[i + 2] = ch & 1;
    ch = ch >> 1;
    spdata[i + 1] = ch & 1;
    ch = ch >> 1;
    spdata[i + 0] = ch & 1;
  }

  /* split bits from data and store in data array [].  */
  in_count = rr;
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
  
  /* decode data bch.  */
  return data_decode_bch(spdata, error_pos, len * 8);

}

void do_bch_correct_data (unsigned char *inbuf, unsigned int *errpos)
{
  int i;
  for (i = errpos[0]; i > 0 ; i--)
  {
    if (errpos[i] >= rr)
    {
       int pos;
       pos = errpos[i] - rr;
       inbuf[pos >> 3] = inbuf[pos >> 3] ^ (1 << ((8 - pos - 1) & 7));
    }
  }
}


extern int printk(char *, ...);

void do_bch_decode_and_correct (unsigned char *inbuf, unsigned char *paritybuf, int len)
{
  int flag, errpos[64];
    
  flag = do_bch_decode (inbuf, paritybuf, errpos, len);
  if (flag == 1)
  {
    if (errpos[0] != 0)
    {
      //printk("{Software bch_decode: correct error}\n");
      do_bch_correct_data (inbuf, errpos);
    }
  }
  else
    printk("{Software bch_decode: uncorrect error}\n");
}

