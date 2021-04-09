/*
main.c 
QRA64 mode encode/decode tests

(c) 2016 - Nico Palermo, IV3NWV

Thanks to Andrea Montefusco IW0HDV for his help on adapting the sources
to OSs other than MS Windows

------------------------------------------------------------------------------
This file is part of the qracodes project, a Forward Error Control
encoding/decoding package based on Q-ary RA (Repeat and Accumulate) LDPC codes.

Files in this package:
   main.c		 - this file
   qra64.c/.h     - qra64 mode encode/decoding functions

   ../qracodes/normrnd.{c,h}   - random gaussian number generator
   ../qracodes/npfwht.{c,h}    - Fast Walsh-Hadamard Transforms
   ../qracodes/pdmath.{c,h}    - Elementary math on probability distributions
   ../qracodes/qra12_63_64_irr_b.{c,h} - Tables for a QRA(12,63) irregular RA 
                                         code over GF(64)
   ../qracodes/qra13_64_64_irr_e.{c,h} - Tables for a QRA(13,64) irregular RA 
                                         code over GF(64)
   ../qracodes/qracodes.{c,h}  - QRA codes encoding/decoding functions

-------------------------------------------------------------------------------

   qracodes is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   qracodes is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with qracodes source distribution.  
   If not, see <http://www.gnu.org/licenses/>.

-----------------------------------------------------------------------------

The code used by the QRA64 mode is the code: QRA13_64_64_IRR_E: K=13
N=64 Q=64 irregular QRA code (defined in qra13_64_64_irr_e.{h,c}).

This code has been designed to include a CRC as the 13th information
symbol and improve the code UER (Undetected Error Rate).  The CRC
symbol is not sent along the channel (the codes are punctured) and the
resulting code is still a (12,63) code with an effective code rate of
R = 12/63.
*/

// OS dependent defines and includes ------------------------------------------

#if _WIN32 // note the underscore: without it, it's not msdn official!
// Windows (x64 and x86)
#include <windows.h>   // required only for GetTickCount(...)
#include <process.h>   // _beginthread
#endif

#if __linux__
#include <unistd.h>
#include <time.h>

unsigned GetTickCount(void) {
    struct timespec ts;
    unsigned theTick = 0U;
    clock_gettime( CLOCK_REALTIME, &ts );
    theTick  = ts.tv_nsec / 1000000;
    theTick += ts.tv_sec * 1000;
    return theTick;
}
#endif

#if __APPLE__
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "qra64.h"
#include "../qracodes/normrnd.h"		   // gaussian numbers generator

// ----------------------------------------------------------------------------

// channel types
#define CHANNEL_AWGN     0
#define CHANNEL_RAYLEIGH 1
#define CHANNEL_FASTFADE 2

#define JT65_SNR_EBNO_OFFSET 29.1f		// with the synch used in JT65
#define QRA64_SNR_EBNO_OFFSET 31.0f		// with the costas array synch 

void printwordd(char *msg, int *x, int size)
{
  int k;
  printf("\n%s ",msg);
  for (k=0;k<size;k++)
    printf("%2d ",x[k]);
  printf("\n");
}
void printwordh(char *msg, int *x, int size)
{
  int k;
  printf("\n%s ",msg);
  for (k=0;k<size;k++)
    printf("%02hx ",x[k]);
  printf("\n");
}

#define NSAMPLES (QRA64_N*QRA64_M)

static float rp[NSAMPLES];
static float rq[NSAMPLES];
static float chp[NSAMPLES];
static float chq[NSAMPLES];
static float r[NSAMPLES];

float *mfskchannel(int *x, int channel_type, float EbNodB)
{
/*
Simulate an MFSK channel, either AWGN or Rayleigh.

x is a pointer to the transmitted codeword, an array of QRA64_N
integers in the range 0..63.

Returns the received symbol energies (squared amplitudes) as an array of 
(QRA64_M*QRA64_N) floats.  The first QRA64_M entries of this array are 
the energies of the first symbol in the codeword.  The second QRA64_M 
entries are those of the second symbol, and so on up to the last codeword 
symbol.
*/
  const float No = 1.0f;		        // noise spectral density
  const float sigma   = (float)sqrt(No/2.0f);	// std dev of noise I/Q components
  const float sigmach = (float)sqrt(1/2.0f);	// std dev of channel I/Q gains
  const float R = 1.0f*QRA64_K/QRA64_N;	

  float EbNo = (float)pow(10,EbNodB/10);
  float EsNo = 1.0f*QRA64_m*R*EbNo;
  float Es = EsNo*No;
  float A = (float)sqrt(Es);
  int k;

  normrnd_s(rp,NSAMPLES,0,sigma);
  normrnd_s(rq,NSAMPLES,0,sigma);

  if(EbNodB>-15) 
	if (channel_type == CHANNEL_AWGN) 
	    for (k=0;k<QRA64_N;k++) 
		  rp[k*QRA64_M+x[k]]+=A;
	else 
		if (channel_type == CHANNEL_RAYLEIGH) {
			normrnd_s(chp,QRA64_N,0,sigmach);
			normrnd_s(chq,QRA64_N,0,sigmach);
			for (k=0;k<QRA64_N;k++) {
				rp[k*QRA64_M+x[k]]+=A*chp[k];
				rq[k*QRA64_M+x[k]]+=A*chq[k];
				}
			}
		else {
		return 0;	// unknown channel type
		}

  // compute the squares of the amplitudes of the received samples
  for (k=0;k<NSAMPLES;k++) 
    r[k] = rp[k]*rp[k] + rq[k]*rq[k];

  return r;
}

// These defines are some packed fields as computed by JT65 
#define CALL_IV3NWV		0x7F85AE7	
#define CALL_K1JT		0xF70DDD7
#define GRID_JN66		0x3AE4		// JN66
#define GRID_73 		0x7ED0		// 73

char decode_type[12][32] = {
  "[?    ?    ?] AP0",
  "[CQ   ?    ?] AP27",
  "[CQ   ?     ] AP42",
  "[CALL ?    ?] AP29",
  "[CALL ?     ] AP44",
  "[CALL CALL ?] AP57",
  "[?    CALL ?] AP29",
  "[?    CALL  ] AP44",
  "[CALL CALL G] AP72",
  "[CQ   CALL ?] AP55",
  "[CQ   CALL  ] AP70",
  "[CQ   CALL G] AP70"
};
char apmode_type[3][32] = {
  "NO AP",
  "AUTO AP",
  "USER AP"
};

int test_proc_1(int channel_type, float EbNodB, int mode)
{
/*
Here we simulate the following (dummy) QSO:

1) CQ IV3NWV
2)                 IV3NWV K1JT
3) K1JT IV3NWV 73
4)                 IV3NWV K1JT 73

No message repetition is attempted

The QSO is counted as successfull if IV3NWV received the last message
When mode=QRA_AUTOAP each decoder attempts to decode the message sent
by the other station using the a-priori information derived by what
has been already decoded in a previous phase of the QSO if decoding
with no a-priori information has not been successful.

Step 1) K1JT's decoder first attempts to decode msgs of type [? ? ?]
and if this attempt fails, it attempts to decode [CQ/QRZ ? ?]  or
[CQ/QRZ ?] msgs

Step 2) if IV3NWV's decoder is unable to decode K1JT's without AP it
attempts to decode messages of the type [IV3NWV ? ?] and [IV3NWV ?].

Step 3) K1JT's decoder attempts to decode [? ? ?] and [K1JT IV3NWV ?]
(this last decode type has been enabled by K1JT's encoder at step 2)

Step 4) IV3NWV's decoder attempts to decode [? ? ?] and [IV3NWV K1JT
?] (this last decode type has been enabled by IV3NWV's encoder at step
3)

At each step the simulation reports if a decode was successful.  In
this case it also reports the type of decode (see table decode_type
above)

When mode=QRA_NOAP, only [? ? ?] decodes are attempted and no a-priori
information is used by the decoder

The function returns 0 if all of the four messages have been decoded
by their recipients (with no retries) and -1 if any of them could not
be decoded
*/

  int x[QRA64_K], xdec[QRA64_K];
  int y[QRA64_N];
  float *rx;
  int rc;

// Each simulated station must use its own codec since it might work with
// different a-priori information.
  qra64codec *codec_iv3nwv = qra64_init(mode);  // codec for IV3NWV
  qra64codec *codec_k1jt   = qra64_init(mode);    // codec for K1JT

// Step 1a: IV3NWV makes a CQ call (with no grid)
  printf("IV3NWV tx: CQ IV3NWV\n");
  encodemsg_jt65(x,CALL_CQ,CALL_IV3NWV,GRID_BLANK);
  qra64_encode(codec_iv3nwv, y, x);
  rx = mfskchannel(y,channel_type,EbNodB);

// Step 1b: K1JT attempts to decode [? ? ?], [CQ/QRZ ? ?] or [CQ/QRZ ?]
  rc = qra64_decode(codec_k1jt, 0, xdec,rx);
  if (rc>=0) { // decoded
    printf("K1JT   rx: received with apcode=%d %s\n",rc, decode_type[rc]);

// Step 2a: K1JT replies to IV3NWV (with no grid)
    printf("K1JT   tx: IV3NWV K1JT\n");
    encodemsg_jt65(x,CALL_IV3NWV,CALL_K1JT, GRID_BLANK);
    qra64_encode(codec_k1jt, y, x);
    rx = mfskchannel(y,channel_type,EbNodB);

// Step 2b: IV3NWV attempts to decode [? ? ?], [IV3NWV ? ?] or [IV3NWV ?]
    rc = qra64_decode(codec_iv3nwv, 0, xdec,rx);
    if (rc>=0) { // decoded
      printf("IV3NWV rx: received with apcode=%d %s\n",rc, decode_type[rc]);

// Step 3a: IV3NWV replies to K1JT with a 73
      printf("IV3NWV tx: K1JT   IV3NWV 73\n");
      encodemsg_jt65(x,CALL_K1JT,CALL_IV3NWV, GRID_73);
      qra64_encode(codec_iv3nwv, y, x);
      rx = mfskchannel(y,channel_type,EbNodB);

// Step 3b: K1JT attempts to decode [? ? ?] or [K1JT IV3NWV ?]
      rc = qra64_decode(codec_k1jt, 0, xdec,rx);
      if (rc>=0) { // decoded
	printf("K1JT   rx: received with apcode=%d %s\n",rc, decode_type[rc]);

// Step 4a: K1JT replies to IV3NWV with a 73
	printf("K1JT   tx: IV3NWV K1JT   73\n");
	encodemsg_jt65(x,CALL_IV3NWV,CALL_K1JT, GRID_73);
	qra64_encode(codec_k1jt, y, x);
	rx = mfskchannel(y,channel_type,EbNodB);

// Step 4b: IV3NWV attempts to decode [? ? ?], [IV3NWV ? ?], or [IV3NWV ?]
	rc = qra64_decode(codec_iv3nwv, 0, xdec,rx);
	if (rc>=0) { // decoded
	  printf("IV3NWV rx: received with apcode=%d %s\n",rc, decode_type[rc]);
	  return 0;
	}
      }
    }
  }
  printf("no decode\n");
  return -1;
}

int test_proc_2(int channel_type, float EbNodB, int mode)
{
/*
Here we simulate the decoder of K1JT after K1JT has sent a msg [IV3NWV K1JT]
and IV3NWV sends him the msg [K1JT IV3NWV JN66].

If mode=QRA_NOAP, K1JT decoder attempts to decode only msgs of type [? ? ?].

If mode=QRA_AUTOP, K1JT decoder will attempt to decode also the msgs 
[K1JT IV3NWV] and [K1JT IV3NWV ?].

In the case a decode is successful the return code of the qra64_decode function
indicates the amount of a-priori information required to decode the received 
message according to this table:

 rc=0    [?    ?    ?]		AP0
 rc=1    [CQ   ?    ?]		AP27
 rc=2    [CQ   ?     ]		AP42
 rc=3    [CALL ?    ?]		AP29
 rc=4    [CALL ?     ]		AP44
 rc=5    [CALL CALL ?]		AP57
 rc=6    [?    CALL ?]		AP29
 rc=7    [?    CALL  ]		AP44
 rc=8    [CALL CALL GRID]	AP72
 rc=9    [CQ   CALL ?]		AP55
 rc=10   [CQ   CALL  ]		AP70
 rc=11   [CQ   CALL GRID]	AP70

The return code is <0 when decoding is unsuccessful

This test simulates the situation ntx times and reports how many times
a particular type decode among the above 6 cases succeded.
*/

  int x[QRA64_K], xdec[QRA64_K];
  int y[QRA64_N];
  float *rx;
  float ebnodbest, ebnodbavg=0;
  int rc,k;

  int ndecok[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  int nundet = 0;
  int ntx = 200,ndec=0;

  qra64codec *codec_iv3nwv = qra64_init(mode);   // codec for IV3NWV
  qra64codec *codec_k1jt   = qra64_init(mode);     // codec for K1JT

  printf("\nQRA64 Test #2 - Decoding with AP knowledge (SNR-Eb/No offset = %.1f dB)\n\n",
		   QRA64_SNR_EBNO_OFFSET);

// This will enable K1JT's decoder to look for calls directed to him [K1JT ? ?/b]
//  printf("K1JT decoder enabled for [K1JT  ?     ?/blank]\n");
//  qra64_apset(codec_k1jt, CALL_K1JT,0,0,APTYPE_MYCALL);

// This will enable K1JT's decoder to look for IV3NWV calls directed to him [K1JT IV3NWV ?/b]
//  printf("K1JT decoder enabled for [K1JT IV3NWV ?]\n");
//  qra64_apset(codec_k1jt, CALL_CQ,CALL_IV3NWV,0,APTYPE_BOTHCALLS);

// This will enable K1JT's decoder to look for msges sent by IV3NWV [? IV3NWV ?]
//  printf("K1JT decoder enabled for [?    IV3NWV ?/blank]\n");
//  qra64_apset(codec_k1jt, 0,CALL_IV3NWV,GRID_BLANK,APTYPE_HISCALL);

// This will enable K1JT's decoder to look for full-knowledge [K1JT IV3NWV JN66] msgs
  printf("K1JT decoder enabled for [K1JT IV3NWV JN66]\n");
  qra64_apset(codec_k1jt, CALL_K1JT,CALL_IV3NWV,GRID_JN66,APTYPE_FULL);

// This will enable K1JT's decoder to look for calls from IV3NWV [CQ IV3NWV ?/b] msgs
  printf("K1JT decoder enabled for [CQ   IV3NWV ?/b/JN66]\n");
  qra64_apset(codec_k1jt, 0,CALL_IV3NWV,GRID_JN66,APTYPE_CQHISCALL);


  // Dx station IV3NWV calls
  printf("\nIV3NWV encoder sends msg: [K1JT IV3NWV JN66]\n\n");
  encodemsg_jt65(x,CALL_CQ,CALL_IV3NWV,GRID_JN66);

//  printf("\nIV3NWV encoder sends msg: [CQ IV3NWV JN66]\n\n");
//  encodemsg_jt65(x,CALL_CQ,CALL_IV3NWV,GRID_JN66);

//  printf("\nIV3NWV encoder sends msg: [CQ IV3NWV]\n\n");
//  encodemsg_jt65(x,CALL_CQ,CALL_IV3NWV,GRID_BLANK);
  qra64_encode(codec_iv3nwv, y, x);

  printf("Simulating K1JT decoder up to AP72\n");

  for (k=0;k<ntx;k++) {
    printf(".");
    rx = mfskchannel(y,channel_type,EbNodB);
    rc = qra64_decode(codec_k1jt, &ebnodbest, xdec,rx);
	if (rc>=0) {
	  ebnodbavg +=ebnodbest;
	  if (memcmp(xdec,x,12*sizeof(int))==0)
		ndecok[rc]++;
	  else
	    nundet++;
	}
  }
  printf("\n\n");


  printf("Transimtted msgs:%d\nDecoded msgs:\n\n",ntx);
  for (k=0;k<12;k++) {
    printf("%3d with %s\n",ndecok[k],decode_type[k]);
    ndec += ndecok[k];
  }
  printf("\nTotal: %d/%d (%d undetected errors)\n\n",ndec,ntx,nundet);
  printf("");

  ebnodbavg/=(ndec+nundet);
  printf("Estimated SNR (average in dB) = %.2f dB\n\n",ebnodbavg-QRA64_SNR_EBNO_OFFSET);

  return 0;
}

int test_fastfading(float EbNodB, float B90, int fadingModel, int submode, int apmode, int olddec, int channel_type, int ntx)
{
  int x[QRA64_K], xdec[QRA64_K];
  int y[QRA64_N];
  float *rx;
  float ebnodbest, ebnodbavg=0;
  int rc,k;
  float rxolddec[QRA64_N*QRA64_M];	// holds the energies at nominal tone freqs

  int ndecok[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  int nundet = 0;
  int ndec=0;

  qra64codec *codec_iv3nwv; 
  qra64codec *codec_k1jt; 

  codec_iv3nwv=qra64_init(QRA_NOAP);  
  codec_k1jt  =qra64_init(apmode);  

  if (channel_type==2) {	// fast-fading case
	printf("Simulating the fast-fading channel\n");
	printf("B90=%.2f Hz - Fading Model=%s - Submode=QRA64%c\n",B90,fadingModel?"Lorentz":"Gauss",submode+'A');
	printf("Decoder metric = %s\n",olddec?"AWGN":"Matched to fast-fading signal");
	}
  else {
    printf("Simulating the %s channel\n",channel_type?"Rayleigh block fading":"AWGN");
	printf("Decoder metric = AWGN\n");
	}


  printf("\nEncoding msg [K1JT IV3NWV JN66]\n");
  encodemsg_jt65(x,CALL_K1JT,CALL_IV3NWV,GRID_JN66);
// printf("[");
//  for (k=0;k<11;k++) printf("%02hX ",x[k]); printf("%02hX]\n",x[11]);

  qra64_encode(codec_iv3nwv, y, x);
  printf("%d transmissions will be simulated\n\n",ntx);

  if (apmode==QRA_USERAP) {
	// This will enable K1JT's decoder to look for cq/qrz calls [CQ/QRZ ? ?/b]
	printf("K1JT decoder enabled for [CQ    ?     ?/blank]\n");
	qra64_apset(codec_k1jt, CALL_K1JT,0,0,APTYPE_CQQRZ);

	// This will enable K1JT's decoder to look for calls directed to him [K1JT ? ?/b]
	printf("K1JT decoder enabled for [K1JT  ?     ?/blank]\n");
	qra64_apset(codec_k1jt, CALL_K1JT,0,0,APTYPE_MYCALL);

	// This will enable K1JT's decoder to look for msges sent by IV3NWV [? IV3NWV ?]
	printf("K1JT decoder enabled for [?    IV3NWV ?/blank]\n");
	qra64_apset(codec_k1jt, 0,CALL_IV3NWV,GRID_BLANK,APTYPE_HISCALL);

	// This will enable K1JT's decoder to look for IV3NWV calls directed to him [K1JT IV3NWV ?/b]
	printf("K1JT decoder enabled for [K1JT IV3NWV ?]\n");
	qra64_apset(codec_k1jt, CALL_K1JT,CALL_IV3NWV,0,APTYPE_BOTHCALLS);

	// This will enable K1JT's decoder to look for full-knowledge [K1JT IV3NWV JN66] msgs
	printf("K1JT decoder enabled for [K1JT IV3NWV JN66]\n");
	qra64_apset(codec_k1jt, CALL_K1JT,CALL_IV3NWV,GRID_JN66,APTYPE_FULL);

	// This will enable K1JT's decoder to look for calls from IV3NWV [CQ IV3NWV ?/b] msgs
	printf("K1JT decoder enabled for [CQ   IV3NWV ?/b/JN66]\n");
	qra64_apset(codec_k1jt, 0,CALL_IV3NWV,GRID_JN66,APTYPE_CQHISCALL);

  }

  printf("\nNow decoding with K1JT's decoder...\n");
/*
  if (channel_type==2) 	// simulate a fast-faded signal
	  printf("Simulating a fast-fading channel with given B90 and spread type\n");
  else
	  printf("Simulating a %s channel\n",channel_type?"Rayleigh block fading":"AWGN");
*/
  for (k=0;k<ntx;k++) {

	  if ((k%10)==0)
	    printf("  %5.1f %%\r",100.0*k/ntx);
//		printf(".");	// work in progress

	if (channel_type==2) {	
		// generate a fast-faded signal
		rc = qra64_fastfading_channel(&rx,y,submode,EbNodB,B90,fadingModel);
		if (rc<0) {
			printf("\nqra64_fastfading_channel error. rc=%d\n",rc);
			return -1;
			}
		}
	else // generate a awgn or Rayleigh block fading signal
		rx = mfskchannel(y, channel_type, EbNodB);


	if (channel_type==2)	// fast-fading case
		if (olddec==1) {
			int k, j;
			int jj = 1<<submode;
			int bps = QRA64_M*(2+jj);
			float *rxbase;
			float *out = rxolddec;
			// calc energies at nominal freqs
			for (k=0;k<QRA64_N;k++) {
				rxbase = rx + QRA64_M + k*bps;
				for (j=0;j<QRA64_M;j++) {
					*out++=*rxbase;
					rxbase+=jj;
					}
				}
			// decode with awgn decoder
			rc = qra64_decode(codec_k1jt,&ebnodbest,xdec,rxolddec);
			}
		else // use fast-fading decoder
			rc = qra64_decode_fastfading(codec_k1jt,&ebnodbest,xdec,rx,submode,B90,fadingModel);
	else // awgn or rayleigh channel. use the old decoder whatever the olddec option is
		rc = qra64_decode(codec_k1jt,&ebnodbest,xdec,rx);



	if (rc>=0) {
	  ebnodbavg +=ebnodbest;
	  if (memcmp(xdec,x,12*sizeof(int))==0)
		ndecok[rc]++;
	  else {
		fprintf(stderr,"\nUndetected error with rc=%d\n",rc);
		nundet++;
		}
	  }

  }
  printf("  %5.1f %%\r",100.0*k/ntx);

  printf("\n\n");

  printf("Msgs transmitted:%d\nMsg decoded:\n\n",ntx);
  for (k=0;k<12;k++) {
    printf("rc=%2d   %3d with %s\n",k,ndecok[k],decode_type[k]);
    ndec += ndecok[k];
  }
  printf("\nTotal: %d/%d (%d undetected errors)\n\n",ndec,ntx,nundet);
  printf("");

  if (ndec>0) {
	ebnodbavg/=(ndec+nundet);
	printf("Estimated SNR (average in dB) = %.2f dB\n\n",ebnodbavg-QRA64_SNR_EBNO_OFFSET);
	}

  return 0;
}



void syntax(void)
{

  printf("\nQRA64 Mode Tests\n");
  printf("2016, Nico Palermo - IV3NWV\n\n");
  printf("---------------------------\n\n");
  printf("Syntax: qra64 [-s<snrdb>] [-c<channel>] [-a<ap-type>] [-t<testtype>] [-h]\n");
  printf("Options: \n");
  printf("       -s<snrdb>   : set simulation SNR in 2500 Hz BW (default:-27.5 dB)\n");
  printf("       -c<channel> : set channel type 0=AWGN (default) 1=Rayleigh 2=Fast-fading\n");
  printf("       -a<ap-type> : set decode type 0=NOAP 1=AUTOAP (default) 2=USERAP\n");
  printf("       -t<testtype>: 0=simulate seq of msgs between IV3NWV and K1JT (default)\n");
  printf("                     1=simulate K1JT receiving K1JT IV3NWV JN66\n");
  printf("                     2=simulate fast-fading/awgn/rayliegh decoders performance\n");
  printf("       -n<ntx>     : simulate the transmission of ntx codewords (default=100)\n");

  printf("Options used only for fast-fading simulations (-c2):\n");
  printf("       -b          : 90%% fading bandwidth in Hz [1..230 Hz] (default = 2.5 Hz)\n");
  printf("       -m          : fading model. 0=Gauss, 1=Lorentz (default = Lorentz)\n");
  printf("       -q          : qra64 submode. 0=QRA64A,... 4=QRA64E (default = QRA64A)\n");
  printf("       -d          : use the old awgn decoder\n");
  printf("       -h: this help\n");
  printf("Example:\n");
  printf("        qra64 -t2 -c2 -a2 -b50 -m1 -q2 -n10000 -s-26\n");
  printf("        runs the error performance test (-t2)\n");
  printf("        with USER_AP (-a2)\n");
  printf("        simulating a fast fading channel (-c2)\n");
  printf("        with B90 = 50 Hz (-b50), Lorentz Doppler (-m1), mode QRA64C (-q2)\n");
  printf("        ntx = 10000 codewords (-n10000) and SNR = -26 dB (-s-26)\n");

}

int main(int argc, char* argv[])
{
  int k, rc, nok=0;
  float SNRdB = -27.5f;
  unsigned int channel = CHANNEL_AWGN;
  unsigned int mode    = QRA_AUTOAP;
  unsigned int testtype=0;
  int   nqso = 100;
  float EbNodB;
  float B90 = 2.5;
  int fadingModel = 1;
  int submode = 0;
  int olddec = 0;
  int ntx = 100;

// Parse the command line
  while(--argc) {
    argv++;

    if (strncmp(*argv,"-h",2)==0) {
      syntax();
      return 0;
	  } 
	else 
	if (strncmp(*argv,"-n",2)==0) {
		ntx = ( int)atoi((*argv)+2);
		if (ntx<100 || ntx>1000000) {
			printf("Invalid -n option. ntx must be in the range [100..1000000]\n");
			syntax();
			return -1;
			}
		} 
	else
	if (strncmp(*argv,"-a",2)==0) {
		mode = ( int)atoi((*argv)+2);
		if (mode>2) {
			printf("Invalid decoding mode\n");
			syntax();
			return -1;
			}
		} 
	else
	if (strncmp(*argv,"-s",2)==0) {
		SNRdB = (float)atof((*argv)+2);
		if (SNRdB>20 || SNRdB<-50) {
			printf("SNR should be in the range [-50..20]\n");
			syntax();
			return -1;
			}
		} 
	else 
	if (strncmp(*argv,"-t",2)==0) {
	    testtype = ( int)atoi((*argv)+2);
	    if (testtype>2) {
			printf("Invalid test type\n");
			syntax();
			return -1;
			}
		}
	else 
	if (strncmp(*argv,"-c",2)==0) {
		channel = ( int)atoi((*argv)+2);
	    if (channel>CHANNEL_FASTFADE) {
			printf("Invalid channel type\n");
			syntax();
			return -1;
			}
	    } 
	else
	if (strncmp(*argv,"-b",2)==0) {
	    B90 = (float)atof((*argv)+2);
	    if (B90<1 || B90>230) {
			printf("Invalid B90\n");
			syntax();
			return -1;
			}
		}
	else
	if (strncmp(*argv,"-m",2)==0) {
	    fadingModel = (int)atoi((*argv)+2);
	    if (fadingModel<0 || fadingModel>1) {
			printf("Invalid fading model\n");
			syntax();
			return -1;
			}
		}
	else
	if (strncmp(*argv,"-q",2)==0) {
	    submode = (int)atoi((*argv)+2);
	    if (submode<0 || submode>4) {
			printf("Invalid submode\n");
			syntax();
			return -1;
			}
		}
	else
	if (strncmp(*argv,"-d",2)==0) {
		olddec = 1;
		}
	else {
	    printf("Invalid option\n");
	    syntax();
	    return -1;
	    }
	}

  if (testtype<2) // old tests
	  if (channel==CHANNEL_FASTFADE) {
		printf("Invalid Option. Test type 0 and 1 supports only AWGN or Rayleigh Channel model\n");
		return -1;
		}
  
  EbNodB = SNRdB+QRA64_SNR_EBNO_OFFSET;	
  
#if defined(__linux__) || defined(__unix__)
  srand48(GetTickCount());
#endif

  if (testtype==0) {
	for (k=0;k<nqso;k++) {
		printf("\n\n------------------------\n");
		rc = test_proc_1(channel, EbNodB, mode);
		if (rc==0)
			nok++;
		}
	printf("\n\n%d/%d QSOs to end without repetitions\n",nok,nqso);
	printf("Input SNR = %.1fdB channel=%s ap-mode=%s\n\n",
		SNRdB,
		channel==CHANNEL_AWGN?"AWGN":"RAYLEIGH",
		apmode_type[mode]
		);
	} 
  else if (testtype==1) {
	test_proc_2(channel, EbNodB, mode);
	printf("Input SNR = %.1fdB channel=%s ap-mode=%s\n\n",
		SNRdB,
		channel==CHANNEL_AWGN?"AWGN":"RAYLEIGH",
		apmode_type[mode]
		);
	}
  else {
	printf("Input SNR = %.1fdB ap-mode=%s\n\n",
	 SNRdB,
	 apmode_type[mode]
	 );
	test_fastfading(EbNodB,B90,fadingModel,submode,mode,olddec, channel, ntx);
  }
  return 0;
}
