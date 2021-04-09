/*
qra64.c 
Encoding/decoding functions for the QRA64 mode

(c) 2016 - Nico Palermo, IV3NWV

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

QRA code used in this sowftware release:

QRA13_64_64_IRR_E: K=13 N=64 Q=64 irregular QRA code (defined in 
qra13_64_64_irr_e.h /.c)

Codes with K=13 are designed to include a CRC as the 13th information symbol
and improve the code UER (Undetected Error Rate).
The CRC symbol is not sent along the channel (the codes are punctured) and the 
resulting code is a (12,63) code 
*/
//----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "qra64.h"
#include "../qracodes/qracodes.h"
#include "../qracodes/qra13_64_64_irr_e.h"
#include "../qracodes/pdmath.h"
#include "../qracodes/normrnd.h"

// Code parameters of the QRA64 mode 
#define QRA64_CODE  qra_13_64_64_irr_e
#define QRA64_NMSG  218        // Must much value indicated in QRA64_CODE.NMSG

#define QRA64_KC (QRA64_K+1)   // Information symbols (crc included)
#define QRA64_NC (QRA64_N+1)   // Codeword length (as defined in the code)
#define QRA64_NITER 100	       // max number of iterations per decode

// static functions declarations ----------------------------------------------
static int  calc_crc6(const int *x, int sz);
static void ix_mask(float *dst, const float *src, const int *mask, 
		    const int *x);
static int qra64_decode_attempts(qra64codec *pcodec, int *xdec, const float *ix);
static int qra64_do_decode(int *x, const float *pix, const int *ap_mask, 
			    const int *ap_x);
static float qra64_fastfading_estim_noise_std(
				float *rxen, 
				const float esnometric, 
				const int submode);
static void qra64_fastfading_intrinsics(
				float *pix, 
				const float *rxamp, 
				const float *hptr, 
				const int    hlen, 
				const float cmetric, 
				const int submode);
static float qra64_fastfading_msg_esno(
			const int *ydec,
			const float *rxamp, 
			const float sigma,
			const float EsNoMetric,
			const int hlen, 
			const int submode);


// a-priori information masks for fields in JT65-like msgs --------------------
#define MASK_CQQRZ      0xFFFFFFC // CQ/QRZ calls common bits
#define MASK_CALL1      0xFFFFFFF
#define MASK_CALL2      0xFFFFFFF
#define MASK_GRIDFULL	0xFFFF
#define MASK_GRIDFULL12	0x3FFC	  // less aggressive mask (to be used with full AP decoding)
#define MASK_GRIDBIT	0x8000	  // b[15] is 1 for free text, 0 otherwise
// ----------------------------------------------------------------------------

qra64codec *qra64_init(int flags)
{

  // Eb/No value for which we optimize the decoder metric
  const float EbNodBMetric = 2.8f; 
  const float EbNoMetric   = (float)pow(10,EbNodBMetric/10);
  const float R = 1.0f*(QRA64_KC)/(QRA64_NC);	

  qra64codec *pcodec = (qra64codec*)malloc(sizeof(qra64codec));

  if (!pcodec)
    return 0;	// can't allocate memory

  pcodec->decEsNoMetric   = 1.0f*QRA64_m*R*EbNoMetric;
  pcodec->apflags			= flags;

  memset(pcodec->apmsg_set,0,APTYPE_SIZE*sizeof(int));

  if (flags==QRA_NOAP)
    return pcodec;

  // for QRA_USERAP and QRA_AUTOAP modes we always enable [CQ/QRZ ? ?] mgs look-up.
  // encode CQ/QRZ AP messages 
  // NOTE: Here we handle only CQ and QRZ msgs. 
  // 'CQ nnn', 'CQ DX' and 'DE' msgs will be handled by the decoder 
  // as messages with no a-priori knowledge
  qra64_apset(pcodec, CALL_CQ, 0, GRID_BLANK, APTYPE_CQQRZ);

  // initialize masks for decoding with a-priori information
  encodemsg_jt65(pcodec->apmask_cqqrz,     MASK_CQQRZ, 0, MASK_GRIDBIT);     
  encodemsg_jt65(pcodec->apmask_cqqrz_ooo, MASK_CQQRZ, 0, MASK_GRIDFULL);
  encodemsg_jt65(pcodec->apmask_call1,     MASK_CALL1, 0, MASK_GRIDBIT);
  encodemsg_jt65(pcodec->apmask_call1_ooo, MASK_CALL1, 0, MASK_GRIDFULL);
  encodemsg_jt65(pcodec->apmask_call2,     0, MASK_CALL2, MASK_GRIDBIT);
  encodemsg_jt65(pcodec->apmask_call2_ooo, 0, MASK_CALL2, MASK_GRIDFULL);
  encodemsg_jt65(pcodec->apmask_call1_call2,     MASK_CALL1,MASK_CALL2, MASK_GRIDBIT);
  encodemsg_jt65(pcodec->apmask_call1_call2_grid,MASK_CALL1,MASK_CALL2, MASK_GRIDFULL12);
  encodemsg_jt65(pcodec->apmask_cq_call2,     MASK_CQQRZ, MASK_CALL2, MASK_GRIDBIT);
  encodemsg_jt65(pcodec->apmask_cq_call2_ooo, MASK_CQQRZ, MASK_CALL2, MASK_GRIDFULL12);

  return pcodec;
}

void qra64_close(qra64codec *pcodec)
{
	free(pcodec);
}

int qra64_apset(qra64codec *pcodec, const int mycall, const int hiscall, const int grid, const int aptype)
{
// Set decoder a-priori knowledge accordingly to the type of the message to look up for
// arguments:
//		pcodec    = pointer to a qra64codec data structure as returned by qra64_init
//		mycall    = mycall to look for
//		hiscall   = hiscall to look for
//		grid      = grid to look for
//		aptype    = define and masks the type of AP to be set accordingly to the following:
//			APTYPE_CQQRZ     set [cq/qrz ?       ?/blank]
//			APTYPE_MYCALL    set [mycall ?       ?/blank]
//			APTYPE_HISCALL   set [?      hiscall ?/blank]
//			APTYPE_BOTHCALLS set [mycall hiscall ?]
//			APTYPE_FULL		 set [mycall hiscall grid]
//			APTYPE_CQHISCALL set [cq/qrz hiscall ?/blank] and [cq/qrz hiscall grid]
// returns:
//		0   on success
//      -1  when qra64_init was called with the QRA_NOAP flag
//		-2  invalid apytpe

	if (pcodec->apflags==QRA_NOAP)
		return -1;

	switch (aptype) {
		case APTYPE_CQQRZ:
			encodemsg_jt65(pcodec->apmsg_cqqrz,  CALL_CQ, 0, GRID_BLANK);
			break;
		case APTYPE_MYCALL:
			encodemsg_jt65(pcodec->apmsg_call1,  mycall,  0, GRID_BLANK);
			break;
		case APTYPE_HISCALL:
			encodemsg_jt65(pcodec->apmsg_call2,  0, hiscall, GRID_BLANK);
			break;
		case APTYPE_BOTHCALLS:
			encodemsg_jt65(pcodec->apmsg_call1_call2,  mycall, hiscall, GRID_BLANK);
			break;
		case APTYPE_FULL:
			encodemsg_jt65(pcodec->apmsg_call1_call2_grid,  mycall, hiscall, grid);
			break;
		case APTYPE_CQHISCALL:
			encodemsg_jt65(pcodec->apmsg_cq_call2,      CALL_CQ, hiscall, GRID_BLANK);
			encodemsg_jt65(pcodec->apmsg_cq_call2_grid, CALL_CQ, hiscall, grid);
			break;
		default:
			return -2;	// invalid ap type
		}

	  pcodec->apmsg_set[aptype]=1;	// signal the decoder to look-up for the specified type


	  return 0;
}
void qra64_apdisable(qra64codec *pcodec, const int aptype)
{
	if (pcodec->apflags==QRA_NOAP)
		return;

	if (aptype<APTYPE_CQQRZ || aptype>=APTYPE_SIZE)
		return;

	pcodec->apmsg_set[aptype] = 0;	//  signal the decoder not to look-up to the specified type
}

void qra64_encode(qra64codec *pcodec, int *y, const int *x)
{
  int encx[QRA64_KC];	// encoder input buffer
  int ency[QRA64_NC];	// encoder output buffer

  int hiscall,mycall,grid;

  memcpy(encx,x,QRA64_K*sizeof(int));		// Copy input to encoder buffer
  encx[QRA64_K]=calc_crc6(encx,QRA64_K);	// Compute and add crc symbol
  qra_encode(&QRA64_CODE, ency, encx);	 // encode msg+crc using given QRA code

  // copy codeword to output puncturing the crc symbol 
  memcpy(y,ency,QRA64_K*sizeof(int));		// copy information symbols 
  memcpy(y+QRA64_K,ency+QRA64_KC,QRA64_C*sizeof(int)); // copy parity symbols 

  if (pcodec->apflags!=QRA_AUTOAP)
    return;

  // Here we handle the QRA_AUTOAP mode --------------------------------------------

  // When a [hiscall mycall ?] msg is detected we instruct the decoder
  // to look for [mycall hiscall ?] msgs
  // otherwise when a [cq mycall ?] msg is sent we reset the APTYPE_BOTHCALLS 

  // look if the msg sent is a std type message (bit15 of grid field = 0)
  if ((x[9]&0x80)==1)
    return;	// no, it's a text message, nothing to do

  // It's a [hiscall mycall grid] message

  // We assume that mycall is our call (but we don't check it)
  // hiscall the station we are calling or a general call (CQ/QRZ/etc..)
  decodemsg_jt65(&hiscall,&mycall,&grid,x);


  if ((hiscall>=CALL_CQ && hiscall<=CALL_CQ999) || hiscall==CALL_CQDX || 
      hiscall==CALL_DE) {
	// tell the decoder to look for msgs directed to us
	qra64_apset(pcodec,mycall,0,0,APTYPE_MYCALL);
    // We are making a general call and don't know who might reply 
    // Reset APTYPE_BOTHCALLS so decoder won't look for [mycall hiscall ?] msgs
    qra64_apdisable(pcodec,APTYPE_BOTHCALLS);
  } else {
    // We are replying to someone named hiscall
    // Set APTYPE_BOTHCALLS so decoder will try for [mycall hiscall ?] msgs
    qra64_apset(pcodec,mycall, hiscall, GRID_BLANK, APTYPE_BOTHCALLS);
  }

}

#define EBNO_MIN -10.0f		// minimum Eb/No value returned by the decoder (in dB)
int qra64_decode(qra64codec *pcodec, float *ebno, int *x, const float *rxen)
{
  int k;
  float *srctmp, *dsttmp;
  float ix[QRA64_NC*QRA64_M];		// (depunctured) intrisic information
  int   xdec[QRA64_KC];				// decoded message (with crc)
  int   ydec[QRA64_NC];				// re-encoded message (for snr calculations)
  float noisestd;					// estimated noise variance
  float msge;						// estimated message energy
  float ebnoval;					// estimated Eb/No
  int rc;
  
  if (QRA64_NMSG!=QRA64_CODE.NMSG)      // sanity check 
    return -16;				// QRA64_NMSG define is wrong

  // compute symbols intrinsic probabilities from received energy observations
  noisestd = qra_mfskbesselmetric(ix, rxen, QRA64_m, QRA64_N,pcodec->decEsNoMetric);

  // de-puncture observations adding a uniform distribution for the crc symbol

  // move check symbols distributions one symbol towards the end
  dsttmp = PD_ROWADDR(ix,QRA64_M, QRA64_NC-1);	//Point to last symbol prob dist
  srctmp = dsttmp-QRA64_M;              // source is the previous pd
  for (k=0;k<QRA64_C;k++) {
    pd_init(dsttmp,srctmp,QRA64_M);
    dsttmp -=QRA64_M;
    srctmp -=QRA64_M;
  }
  // Initialize crc prob to a uniform distribution
  pd_init(dsttmp,pd_uniform(QRA64_m),QRA64_M);

  // Try to decode using all AP cases if required
  rc = qra64_decode_attempts(pcodec, xdec, ix);

  if (rc<0)
	  return rc;	// no success

  // successfull decode --------------------------------
  
  // copy decoded message (without crc) to output buffer
  memcpy(x,xdec,QRA64_K*sizeof(int));

  if (ebno==0)	// null pointer indicates we are not interested in the Eb/No estimate
	  return rc;

  // reencode message and estimate Eb/No
  qra_encode(&QRA64_CODE, ydec, xdec);	 
  // puncture crc
  memmove(ydec+QRA64_K,ydec+QRA64_KC,QRA64_C*sizeof(int)); 
  // compute total power of decoded message
  msge = 0;
  for (k=0;k<QRA64_N;k++) {
	  msge +=rxen[ydec[k]];	// add energy of current symbol
	  rxen+=QRA64_M;			// ptr to next symbol
	  }

  // NOTE:
  // To make a more accurate Eb/No estimation we should compute the noise variance
  // on all the rxen values but the transmitted symbols.
  // Noisestd is compute by qra_mfskbesselmetric assuming that
  // the signal power is much less than the total noise power in the QRA64_M tones
  // but this is true only if the Eb/No is low.
  // Here, in order to improve accuracy, we linearize the estimated Eb/No value empirically
  // (it gets compressed when it is very high as in this case the noise variance 
  // is overestimated)

  // this would be the exact value if the noisestd were not overestimated at high Eb/No
  ebnoval = (0.5f/(QRA64_K*QRA64_m))*msge/(noisestd*noisestd)-1.0f; 

  // Empirical linearization (to remove the noise variance overestimation)
  // the resulting SNR is accurate up to +20 dB (51 dB Eb/No)
  if (ebnoval>57.004f)
	  ebnoval=57.004f;
  ebnoval = ebnoval*57.03f/(57.03f-ebnoval);

  // compute value in dB
  if (ebnoval<=0)
	  ebnoval = EBNO_MIN; // assume a minimum, positive value
  else
	  ebnoval = 10.0f*(float)log10(ebnoval);
	  if (ebnoval<EBNO_MIN)
		  ebnoval = EBNO_MIN;
  
  *ebno = ebnoval;

  return rc;	
}

// Tables of fading amplitudes coefficients for QRA64 (Ts=6912/12000)
// As the fading is assumed to be symmetric around the nominal frequency
// only the leftmost and the central coefficient are stored in the tables.
// (files have been generated with the Matlab code efgengauss.m and efgenlorentz.m)
#include "fadampgauss.c"
#include "fadamplorentz.c"

int qra64_decode_fastfading(
				qra64codec *pcodec,		// ptr to the codec structure
				float *ebno,			// ptr to where the estimated Eb/No value will be saved
				int *x,					// ptr to decoded message 
				float *rxen,		    // ptr to received symbol energies array
				const int submode,		// submode idx (0=QRA64A ... 4=QRA64E)
				const float B90,	    // spread bandwidth (90% fractional energy)
				const int fadingModel)  // 0=Gaussian 1=Lorentzian fade model

// Decode a QRA64 msg using a fast-fading metric
//
// rxen: The array of the received bin energies
//       Bins must be spaced by integer multiples of the symbol rate (1/Ts Hz)
//       The array must be an array of total length U = L x N where:
//			L: is the number of frequency bins per message symbol (see after)
//          N: is the number of symbols in a QRA64 msg (63)

//       The number of bins/symbol L depends on the selected submode accordingly to 
//		 the following rule:
//			L = (64+64*2^submode+64) = 64*(2+2^submode)
//		 Tone 0 is always supposed to be at offset 64 in the array.
//		 The m-th tone nominal frequency is located at offset 64 + m*2^submode (m=0..63)
//
//		 Submode A: (2^submode = 1)
//          L = 64*3 = 196 bins/symbol
//          Total length of the energies array: U = 192*63 = 12096 floats
//
//		 Submode B: (2^submode = 2)
//          L = 64*4 = 256 bins/symbol
//          Total length of the energies array: U = 256*63 = 16128 floats
//
//		 Submode C: (2^submode = 4)
//          L = 64*6 = 384 bins/symbol
//          Total length of the energies array: U = 384*63 = 24192 floats
//
//		 Submode D: (2^submode = 8)
//          L = 64*10 = 640 bins/symbol
//          Total length of the energies array: U = 640*63 = 40320 floats
//
//		 Submode E: (2^submode = 16)
//          L = 64*18 = 1152 bins/symbol
//          Total length of the energies array: U = 1152*63 = 72576 floats
//
//		Note: The rxen array is modified and reused for internal calculations.
//
//
//	B90: spread fading bandwidth in Hz (90% fractional average energy)
//
//			B90 should be in the range 1 Hz ... 238 Hz
//			The value passed to the call is rounded to the closest value among the 
//			64 available values:
//				B = 1.09^k Hz, with k=0,1,...,63
//
//			I.e. B90=27 Hz will be approximated in this way:
//				k = rnd(log(27)/log(1.09)) = 38
//              B90 = 1.09^k = 1.09^38 = 26.4 Hz
//
//          For any input value the maximum rounding error is not larger than +/- 5%
//          

{

  int k;
  float *srctmp, *dsttmp;
  float ix[QRA64_NC*QRA64_M];		// (depunctured) intrisic information
  int   xdec[QRA64_KC];				// decoded message (with crc)
  int   ydec[QRA64_NC];				// re-encoded message (for snr calculations)
  float noisestd;					// estimated noise std
  float esno,ebnoval;				// estimated Eb/No
  float tempf;
  float EsNoMetric, cmetric;
  int rc;
  int hidx, hlen;
  const float *hptr;
  
	if (QRA64_NMSG!=QRA64_CODE.NMSG)
		return -16;					// QRA64_NMSG define is wrong

	if (submode<0 || submode>4)
		return -17;				// invalid submode

	if (B90<1.0f || B90>238.0f)	
		return -18;				// B90 out of range

	// compute index to most appropriate amplitude weighting function coefficients
    hidx = (int)(log((float)B90)/log(1.09f) - 0.499f);

	if (hidx<0 || hidx > 64) 
		return -19;				// index of weighting function out of range

	if (fadingModel==0) {	 // gaussian fading model
		// point to gaussian weighting taps
		hlen = hlen_tab_gauss[hidx];	 // hlen = (L+1)/2 (where L=(odd) number of taps of w fun)
		hptr = hptr_tab_gauss[hidx];     // pointer to the first (L+1)/2 coefficients of w fun
		}
	else if (fadingModel==1) {
		// point to lorentzian weighting taps
		hlen = hlen_tab_lorentz[hidx];	 // hlen = (L+1)/2 (where L=(odd) number of taps of w fun)
		hptr = hptr_tab_lorentz[hidx];     // pointer to the first (L+1)/2 coefficients of w fun
		}
	else 
		return -20;			// invalid fading model index


	// compute (euristically) the optimal decoder metric accordingly the given spread amount
	// We assume that the decoder threshold is:
	//		Es/No(dB) = Es/No(AWGN)(dB) + 8*log(B90)/log(240)(dB)
	// that's to say, at the maximum Doppler spread bandwidth (240 Hz) there's a ~8 dB Es/No degradation
	// over the AWGN case
	tempf = 8.0f*(float)log((float)B90)/(float)log(240.0f);
	EsNoMetric = pcodec->decEsNoMetric*(float)pow(10.0f,tempf/10.0f);

	// Step 1 ----------------------------------------------------------------------------------- 
	// Evaluate the noise stdev from the received energies at nominal tone frequencies
    // and transform energies to amplitudes
	tempf = hptr[hlen-1];				// amplitude weigth at nominal freq;
	tempf = tempf*tempf;				// fractional energy at nominal freq. bin
	
	noisestd = qra64_fastfading_estim_noise_std(rxen, EsNoMetric, submode);
	cmetric = (float)sqrt(M_PI_2*EsNoMetric)/noisestd;

	// Step 2 -----------------------------------------------------------------------------------
	// Compute message symbols probability distributions
	qra64_fastfading_intrinsics(ix, rxen, hptr, hlen, cmetric, submode);

	// Step 3 ---------------------------------------------------------------------------
	// De-puncture observations adding a uniform distribution for the crc symbol
	// Move check symbols distributions one symbol towards the end
	dsttmp = PD_ROWADDR(ix,QRA64_M, QRA64_NC-1);	//Point to last symbol prob dist
	srctmp = dsttmp-QRA64_M;              // source is the previous pd
	for (k=0;k<QRA64_C;k++) {
		pd_init(dsttmp,srctmp,QRA64_M);
		dsttmp -=QRA64_M;
		srctmp -=QRA64_M;
		}
	// Initialize crc prob to a uniform distribution
	pd_init(dsttmp,pd_uniform(QRA64_m),QRA64_M);

	// Step 4 ---------------------------------------------------------------------------
	// Attempt to decode
	rc = qra64_decode_attempts(pcodec, xdec, ix);
	if (rc<0)
	  return rc;	// no success

	// copy decoded message (without crc) to output buffer
	memcpy(x,xdec,QRA64_K*sizeof(int));

	// Step 5 ----------------------------------------------------------------------------
	// Estimate the message Eb/No

	if (ebno==0)	// null pointer indicates we are not interested in the Eb/No estimate
		return rc;

	// reencode message to estimate Eb/No
	qra_encode(&QRA64_CODE, ydec, xdec);	 
	// puncture crc
	memmove(ydec+QRA64_K,ydec+QRA64_KC,QRA64_C*sizeof(int)); 

	// compute Es/N0 of decoded message
	esno = qra64_fastfading_msg_esno(ydec,rxen,noisestd, EsNoMetric, hlen,submode);

	// as the weigthing function include about 90% of the energy
	// we could compute the unbiased esno with:
	// esno = esno/0.9;
	
	// this would be the exact value if the noisestd were not overestimated at high Eb/No
	ebnoval = 1.0f/(1.0f*QRA64_K/QRA64_N*QRA64_m)*esno; 

	// compute value in dB
	if (ebnoval<=0)
	  ebnoval = EBNO_MIN; // assume a minimum, positive value
	else
	  ebnoval = 10.0f*(float)log10(ebnoval);
	  if (ebnoval<EBNO_MIN)
		  ebnoval = EBNO_MIN;
  
	*ebno = ebnoval;

  return rc;	

}



int qra64_fastfading_channel(float **rxen, const int *xmsg, const int submode, const float EbN0dB, const float B90, const int fadingModel)
{
	// Simulate transmission over a fading channel and non coherent detection

	// Set rxen to point to an array of bin energies formatted as required 
	// by the (fast-fading) decoding routine

	// returns 0 on success or negative values on error conditions

	static float *channel_out = NULL;
	static int    channel_submode = -1;

	int bpt = (1<<submode);			// bins per tone 
	int bps = QRA64_M*(2+bpt);		// total number of bins per symbols
	int bpm = bps*QRA64_N;			// total number of bins in a message
	int n,j,hidx, hlen;
	const float *hptr;
	float *cursym,*curtone;

	float iq[2];
	float *curi, *curq;

//	float tote=0;	// debug

	float N0, EsN0, Es, A, sigmanoise, sigmasig;

	if (rxen==NULL)
		return -1;		// rxen must be a non-null ptr 

	// allocate output buffer if not yet done or if submode changed
	if (channel_out==NULL || submode!=channel_submode) {

		// unallocate previous buffer
		if (channel_out)
			free(channel_out);

		// allocate new buffer
		// we allocate twice the mem so that we can store/compute complex amplitudes
		channel_out = (float*)malloc(bpm*sizeof(float)*2);	
		if (channel_out==NULL)
			return -2;	// error allocating memory

		channel_submode = submode;
		}

	if (B90<1.0f || B90>238.0f)	
		return -18;				// B90 out of range

	// compute index to most appropriate amplitude weighting function coefficients
    hidx = (int)(log((float)B90)/log(1.09f) - 0.499f);

	if (hidx<0 || hidx > 64) 
		return -19;				// index of weighting function out of range

	if (fadingModel==0) {	 // gaussian fading model
		// point to gaussian weighting taps
		hlen = hlen_tab_gauss[hidx];	 // hlen = (L+1)/2 (where L=(odd) number of taps of w fun)
		hptr = hptr_tab_gauss[hidx];     // pointer to the first (L+1)/2 coefficients of w fun
		}
	else if (fadingModel==1) {
		// point to lorentzian weighting taps
		hlen = hlen_tab_lorentz[hidx];	 // hlen = (L+1)/2 (where L=(odd) number of taps of w fun)
		hptr = hptr_tab_lorentz[hidx];     // pointer to the first (L+1)/2 coefficients of w fun
		}
	else 
		return -20;			// invalid fading model index


	// Compute the unfaded tone amplitudes from the Eb/No value passed to the call
	N0 = 1.0f;	// assume unitary noise PSD
	sigmanoise = (float)sqrt(N0/2);
	EsN0 = (float)pow(10.0f,EbN0dB/10.0f)*QRA64_m*QRA64_K/QRA64_N; // Es/No = m*R*Eb/No
	Es   = EsN0*N0;
	A    = (float)sqrt(Es/2.0f);	// unfaded tone amplitude (i^2+q^2 = Es/2+Es/2 = Es)


	// Generate gaussian noise iq components
	normrnd_s(channel_out, bpm*2, 0 , sigmanoise);

	// Add message symbols energies
	for (n=0;n<QRA64_N;n++) {					

		cursym  = channel_out+n*bps + QRA64_M; // point to n-th symbol
		curtone = cursym+xmsg[n]*bpt;	 // point to encoded tone 
		curi    = curtone-hlen+1;		 // point to real part of first bin
		curq    = curtone-hlen+1+bpm;	 // point to imag part of first bin
		
		// generate Rayleigh faded bins with given average energy and add to noise
		for (j=0;j<hlen;j++) {	
			sigmasig = A*hptr[j];
			normrnd_s(iq, 2, 0 , sigmasig);
//			iq[0]=sigmasig*sqrt(2); iq[1]=0;	debug: used to verify Eb/No 
			*curi++ += iq[0];
			*curq++ += iq[1];
//			tote +=iq[0]*iq[0]+iq[1]*iq[1];		// debug
			}
		for (j=hlen-2;j>=0;j--) {	
			sigmasig = A*hptr[j];
			normrnd_s(iq, 2, 0 , sigmasig);
//			iq[0]=sigmasig*sqrt(2); iq[1]=0;	debug: used to verify Eb/No
			*curi++ += iq[0];
			*curq++ += iq[1];
//			tote +=iq[0]*iq[0]+iq[1]*iq[1];		// debug
			}

		}

//	tote = tote/QRA64_N;	// debug

	// compute total bin energies (S+N) and store in first half of buffer
	curi = channel_out;
	curq = channel_out+bpm;
	for (n=0;n<bpm;n++) 					
		channel_out[n] = curi[n]*curi[n] + curq[n]*curq[n];

	// set rxen to point to the channel output energies
	*rxen = channel_out;

	return 0;	
}



// Static functions definitions ----------------------------------------------

// fast-fading static functions --------------------------------------------------------------

static float qra64_fastfading_estim_noise_std(float *rxen, const float esnometric, const int submode)
{
	// estimate the noise standard deviation from nominal frequency symbol bins
	// transform energies to amplitudes

	// rxen = message symbols energies (overwritten with symbols amplitudes)
	// esnometric = Es/No at nominal frequency bin for which we compute the decoder metric
	// submode = submode used (0=A...4=E)

	int bpt = (1<<submode);			// bins per tone 
	int bps = QRA64_M*(2+bpt);		// total number of bins per symbols
	int bpm = bps*QRA64_N;			// total number of bins in a message
	int k;
	float sigmaest;

	// estimate noise std
	sigmaest = 0;
	for (k=0;k<bpm;k++) {
		sigmaest += rxen[k];
		// convert energies to amplitudes for later use
		rxen[k] = (float)sqrt(rxen[k]);	// we do it in place to avoid memory allocations
		}
	sigmaest = sigmaest/bpm;
	sigmaest = (float)sqrt(sigmaest/(1.0f+esnometric/bps)/2.0f); 

	// Note: sigma is overestimated by the (unknown) factor sqrt((1+esno(true)/bps)/(1+esnometric/bps))

	return sigmaest;
}

static void qra64_fastfading_intrinsics(
				float *pix, 
				const float *rxamp, 
				const float *hptr, 
				const int    hlen, 
				const float cmetric, 
				const int submode)
{

	// For each symbol in a message:
	// a) Compute tones loglikelihoods as a sum of products between of the expected 
	// amplitude fading coefficient and received amplitudes.
	// Each product is computed as log(I0(hk*xk*cmetric)) where hk is the average fading amplitude,
	// xk is the received amplitude at bin offset k, and cmetric is a constant dependend on the
	// Eb/N0 value for which the metric is optimized
	// The function y = log(I0(x)) is approximated as y = x^2/(x+e)
	// b) Compute intrinsic symbols probability distributions from symbols loglikelihoods

	int n,k,j, bps, bpt;
	const float *cursym, *curbin;
	float *curix;
	float u, maxloglh, loglh, sumix;

	bpt = 1<<submode;				// bins per tone
	bps = QRA64_M*(2+bpt);			// bins per symbol

	for (n=0;n<QRA64_N;n++) {			// for each symbol in the message
		cursym = rxamp+n*bps + QRA64_M;	// point to current symbol nominal bin
		maxloglh = 0;
		curix  = pix+n*QRA64_M;		
		for (k=0;k<QRA64_M;k++) {   // for each tone in the current symbol
			curbin = cursym + k*bpt -hlen+1;	// ptr to lowest bin of the current tone
			// compute tone loglikelihood as a weighted sum of bins loglikelihoods
			loglh = 0.f;
			for (j=0;j<hlen;j++) {	
				u = *curbin++ * hptr[j]*cmetric;
				u = u*u/(u+(float)M_E);	// log(I0(u)) approx.
				loglh = loglh + u;	
				}
			for (j=hlen-2;j>=0;j--) {	
				u = *curbin++ * hptr[j]*cmetric;
				u = u*u/(u+(float)M_E);	// log(I0(u)) approx.
				loglh = loglh + u;
				}
			if (loglh>maxloglh)		// keep track of the max loglikelihood
				maxloglh = loglh;
			curix[k]=loglh;
			}
		// scale to likelihoods
		sumix = 0.f;
		for (k=0;k<QRA64_M;k++) {   
			u = (float)exp(curix[k]-maxloglh);
			curix[k]=u;
			sumix +=u;
			}
		// scale to probabilities
		sumix = 1.0f/sumix;
		for (k=0;k<QRA64_M;k++) 
			curix[k] = curix[k]*sumix;
		}
}

static float qra64_fastfading_msg_esno(
			const int *ydec,
			const float *rxamp, 
			const float sigma,
			const float EsNoMetric,
			const int hlen, 
			const int submode)
{
	// Estimate msg Es/N0

	int n,j, bps, bpt;
	const float *cursym, *curtone, *curbin;
	float u, msgsn,esno;
	int tothlen = 2*hlen-1;

	bpt = 1<<submode;				// bins per tone
	bps = QRA64_M*(2+bpt);			// bins per symbol

	msgsn = 0;
	for (n=0;n<QRA64_N;n++) {					
		cursym  = rxamp+n*bps + QRA64_M; // point to n-th symbol amplitudes
		curtone = cursym+ydec[n]*bpt;	 // point to decoded tone amplitudes
		curbin  = curtone-hlen+1;		 // point to first bin amplitude
		
		// sum bin energies
		for (j=0;j<hlen;j++) {	
			u = *curbin++; 
			msgsn += u*u;	
			}
		for (j=hlen-2;j>=0;j--) {	
			u = *curbin++; 
			msgsn += u*u;	
			}

		}

	msgsn =  msgsn/(QRA64_N*tothlen);	// avg msg energy per bin (noise included)

	// as sigma is overestimated (sigmatrue = sigma*sqrt((1+EsNoMetric/bps)/(1+EsNo/bps))
	// we have: msgsn = (1+x/hlen)/(1+x/bps)*2*sigma^2*(1+EsnoMetric/bps), where x = Es/N0(true)
	//
	// we can then write:
	// u = msgsn/2.0f/(sigma*sigma)/(1.0f+EsNoMetric/bps);
	// (1+x/hlen)/(1+x/bps) = u

	u = msgsn/(2.0f*sigma*sigma)/(1.0f+EsNoMetric/bps);

	// check u>1 
	if (u<1)
		return 0.f;

	// check u<bps/tot hlen
	if (u>(bps/tothlen))
		return 10000.f;

	// solve for Es/No
	esno = (u-1.0f)/(1.0f/tothlen-u/bps);

	return esno;
	

}


// Attempt to decode given intrisic information
static int qra64_decode_attempts(qra64codec *pcodec, int *xdec, const float *ix)
{
  int rc;

  // Attempt to decode without a-priori info --------------------------------
  rc = qra64_do_decode(xdec, ix, NULL, NULL);
  if (rc>=0) 
	  return 0; // successfull decode with AP0
  else
	  if (pcodec->apflags==QRA_NOAP) 
		  // nothing more to do
		  return rc; // rc<0 = unsuccessful decode

  // Here we handle decoding with AP knowledge

  // Attempt to decode CQ calls
  rc = qra64_do_decode(xdec,ix,pcodec->apmask_cqqrz, pcodec->apmsg_cqqrz); 
  if (rc>=0) return 1;    // decoded [cq/qrz ? ?]

  rc = qra64_do_decode(xdec, ix, pcodec->apmask_cqqrz_ooo, 
		       pcodec->apmsg_cqqrz);	                        
  if (rc>=0) return 2;    // decoded [cq ? ooo]

  // attempt to decode calls directed to us 
  if (pcodec->apmsg_set[APTYPE_MYCALL]) {
	rc = qra64_do_decode(xdec, ix, pcodec->apmask_call1, 
		       pcodec->apmsg_call1);		                
	if (rc>=0) return 3;    // decoded [mycall ? ?]
	rc = qra64_do_decode(xdec, ix, pcodec->apmask_call1_ooo, 
		       pcodec->apmsg_call1);	                    
	if (rc>=0) return 4;    // decoded [mycall ? ooo]
	}

  // attempt to decode [mycall srccall ?] msgs
  if (pcodec->apmsg_set[APTYPE_BOTHCALLS]) {
	rc = qra64_do_decode(xdec, ix, pcodec->apmask_call1_call2, 
		       pcodec->apmsg_call1_call2);	                
	if (rc>=0) return 5;    // decoded [mycall srccall ?]	
	}

  // attempt to decode [? hiscall ?/b] msgs
  if (pcodec->apmsg_set[APTYPE_HISCALL]) {
	rc = qra64_do_decode(xdec, ix, pcodec->apmask_call2, 
		       pcodec->apmsg_call2);		                
	if (rc>=0) return 6;    // decoded [? hiscall ?]
	rc = qra64_do_decode(xdec, ix, pcodec->apmask_call2_ooo, 
		       pcodec->apmsg_call2);	                    
	if (rc>=0) return 7;    // decoded [? hiscall ooo]
	}

  // attempt to decode [cq/qrz hiscall ?/b/grid] msgs
  if (pcodec->apmsg_set[APTYPE_CQHISCALL]) {

	rc = qra64_do_decode(xdec, ix, pcodec->apmask_cq_call2, 
				pcodec->apmsg_cq_call2);		                
	if (rc>=0) return 9;	// decoded [cq/qrz hiscall ?]

	rc = qra64_do_decode(xdec, ix, pcodec->apmask_cq_call2_ooo, 
		       pcodec->apmsg_cq_call2_grid);	
	if (rc>=0) {
		// Full AP mask need special handling
		// To minimize false decodes we check the decoded message
		// with what passed in the ap_set call
		if (memcmp(pcodec->apmsg_cq_call2_grid,xdec, QRA64_K*sizeof(int))!=0) 
			return -1;
		else
			return 11;		// decoded [cq/qrz hiscall grid]
		};    

	rc = qra64_do_decode(xdec, ix, pcodec->apmask_cq_call2_ooo, 
		       pcodec->apmsg_cq_call2);	                    
	if (rc>=0) { 
		// Full AP mask need special handling
		// To minimize false decodes we check the decoded message
		// with what passed in the ap_set call
		if (memcmp(pcodec->apmsg_cq_call2,xdec, QRA64_K*sizeof(int))!=0) 
			return -1;
		else
			return 10;    // decoded [cq/qrz hiscall ]
		}
	}

  // attempt to decode [mycall hiscall grid]
  if (pcodec->apmsg_set[APTYPE_FULL]) {
	rc = qra64_do_decode(xdec, ix, pcodec->apmask_call1_call2_grid, 
		       pcodec->apmsg_call1_call2_grid); 
	if (rc>=0) { 
		// Full AP mask need special handling
		// All the three msg fields were given.
		// To minimize false decodes we check the decoded message
		// with what passed in the ap_set call
		if (memcmp(pcodec->apmsg_call1_call2_grid,xdec, QRA64_K*sizeof(int))!=0) 
			return -1;
		else
			return 8;	   // decoded [mycall hiscall grid]
		}
	}

  // all decoding attempts failed
  return rc;
}



// Decode with given a-priori information 
static int qra64_do_decode(int *xdec, const float *pix, const int *ap_mask, 
			   const int *ap_x)
{
  int rc;
  const float *ixsrc;
  float ix_masked[QRA64_NC*QRA64_M];  // Masked intrinsic information
  float ex[QRA64_NC*QRA64_M];	      // Extrinsic information from the decoder

  float v2cmsg[QRA64_NMSG*QRA64_M];   // buffers for the decoder messages
  float c2vmsg[QRA64_NMSG*QRA64_M];

  if (ap_mask==NULL) {   // no a-priori information
    ixsrc = pix;	 // intrinsic source is what passed as argument
  } else {	
    // a-priori information provided
    // mask channel observations with a-priori 
    ix_mask(ix_masked,pix,ap_mask,ap_x);
    ixsrc = ix_masked;	// intrinsic source is the masked version
  }

  // run the decoding algorithm
  rc = qra_extrinsic(&QRA64_CODE,ex,ixsrc,QRA64_NITER,v2cmsg,c2vmsg);
  if (rc<0)
    return -1;	// no convergence in given iterations

  // decode 
  qra_mapdecode(&QRA64_CODE,xdec,ex,ixsrc);

  // verify crc
  if (calc_crc6(xdec,QRA64_K)!=xdec[QRA64_K]) // crc doesn't match (detected error)
    return -2;	// decoding was succesfull but crc doesn't match

  return 0;
}


// crc functions --------------------------------------------------------------
// crc-6 generator polynomial
// g(x) = x^6 + a5*x^5 + ... + a1*x + a0

// g(x) = x^6 + x + 1  
#define CRC6_GEN_POL 0x30  // MSB=a0 LSB=a5    

// g(x) = x^6 + x^2 + x + 1 (See:  https://users.ece.cmu.edu/~koopman/crc/)
// #define CRC6_GEN_POL 0x38  // MSB=a0 LSB=a5. Simulation results are similar

static int calc_crc6(const int *x, int sz)
{
  // todo: compute it faster using a look up table
  int k,j,t,sr = 0;
  for (k=0;k<sz;k++) {
    t = x[k];
    for (j=0;j<6;j++) {
      if ((t^sr)&0x01)
	sr = (sr>>1) ^ CRC6_GEN_POL;
      else
	sr = (sr>>1);
      t>>=1;
    }
  }
  return sr;
}

static void ix_mask(float *dst, const float *src, const int *mask, 
		    const int *x)
{
  // mask intrinsic information (channel observations) with a priori knowledge
	
  int k,kk, smask;
  float *row;

  memcpy(dst,src,(QRA64_NC*QRA64_M)*sizeof(float));

  for (k=0;k<QRA64_K;k++) {	// we can mask only information symbols distrib
    smask = mask[k];
    row = PD_ROWADDR(dst,QRA64_M,k);
    if (smask) {
      for (kk=0;kk<QRA64_M;kk++) 
	if (((kk^x[k])&smask)!=0)
	  *(row+kk) = 0.f;

      pd_norm(row,QRA64_m);
    }
  }
}

// encode/decode msgs as done in JT65
void encodemsg_jt65(int *y, const int call1, const int call2, const int grid)
{
  y[0]= (call1>>22)&0x3F;
  y[1]= (call1>>16)&0x3F;
  y[2]= (call1>>10)&0x3F;
  y[3]= (call1>>4)&0x3F;
  y[4]= (call1<<2)&0x3F;

  y[4] |= (call2>>26)&0x3F;
  y[5]= (call2>>20)&0x3F;
  y[6]= (call2>>14)&0x3F;
  y[7]= (call2>>8)&0x3F;
  y[8]= (call2>>2)&0x3F;
  y[9]= (call2<<4)&0x3F;

  y[9] |= (grid>>12)&0x3F;
  y[10]= (grid>>6)&0x3F;
  y[11]= (grid)&0x3F;

}
void decodemsg_jt65(int *call1, int *call2, int *grid, const int *x)
{
  int nc1, nc2, ng;

  nc1 = x[4]>>2;
  nc1 |= x[3]<<4;
  nc1 |= x[2]<<10;
  nc1 |= x[1]<<16;
  nc1 |= x[0]<<22;

  nc2 = x[9]>>4;
  nc2 |= x[8]<<2;
  nc2 |= x[7]<<8;
  nc2 |= x[6]<<14;
  nc2 |= x[5]<<20;
  nc2 |= (x[4]&0x03)<<26;

  ng   = x[11];
  ng  |= x[10]<<6;
  ng  |= (x[9]&0x0F)<<12;

  *call1 = nc1;
  *call2 = nc2;
  *grid  = ng;
}
