// q65.h
// Q65 modes encoding/decoding functions
// 
// (c) 2020 - Nico Palermo, IV3NWV - Microtelecom Srl, Italy
// ------------------------------------------------------------------------------
// This file is part of the qracodes project, a Forward Error Control
// encoding/decoding package based on Q-ary RA (Repeat and Accumulate) LDPC codes.
//
//    qracodes is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//    qracodes is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License
//    along with qracodes source distribution.  
//    If not, see <http://www.gnu.org/licenses/>.

#ifndef _q65_h
#define _q65_h

#include "qracodes.h"

// Error codes returned by q65_decode(...) 
#define Q65_DECODE_INVPARAMS	 -1
#define Q65_DECODE_FAILED		 -2
#define Q65_DECODE_CRCMISMATCH   -3
#define Q65_DECODE_LLHLOW		 -4
#define Q65_DECODE_UNDETERR		 -5

// Verify loglikelihood after successful decoding
#define Q65_CHECKLLH
// Max codeword list size in q65_decode_fullaplist
#define Q65_FULLAPLIST_SIZE	256

// maximum number of weights for the fast-fading metric evaluation
#define Q65_FASTFADING_MAXWEIGTHS 65

extern float q65_llh;

typedef struct {
	const qracode *pQraCode; // qra code to be used by the codec
	float decoderEsNoMetric; // value for which we optimize the decoder metric
	int		*x;				 // codec input 
	int		*y;				 // codec output
	float	*qra_v2cmsg;	 // decoder v->c messages
	float	*qra_c2vmsg;	 // decoder c->v messages 
	float	*ix;			 // decoder intrinsic information
	float	*ex;			 // decoder extrinsic information 
	// variables used to compute the intrinsics in the fast-fading case
	int     nBinsPerTone;
	int		nBinsPerSymbol;
	float   ffNoiseVar;	
	float   ffEsNoMetric;
	int	    nWeights;
	float   ffWeight[Q65_FASTFADING_MAXWEIGTHS]; 
} q65_codec_ds;

int		q65_init(q65_codec_ds *pCodec, const qracode *pQraCode);
void	q65_free(q65_codec_ds *pCodec);

int		q65_encode(const q65_codec_ds *pCodec, int *pOutputCodeword, const int *pInputMsg);

int		q65_intrinsics(q65_codec_ds *pCodec, float *pIntrinsics, const float *pInputEnergies);

int		q65_intrinsics_fastfading(q65_codec_ds *pCodec, 
					float *pIntrinsics,				// intrinsic symbol probabilities output
					const float *pInputEnergies,	// received energies input
					const int submode,				// submode idx (0=A ... 4=E)
					const float B90Ts,				// normalized spread bandwidth (90% fractional energy)
					const int fadingModel);			// 0=Gaussian 1=Lorentzian fade model


int		q65_decode(q65_codec_ds *pCodec, 
			   int* pDecodedCodeword, 
			   int *pDecodedMsg, 
			   const float *pIntrinsics, 
			   const int *pAPMask, 
			   const int *pAPSymbols,
			   const int maxiters);

int		q65_decode_fullaplist(q65_codec_ds *codec,
						   int *ydec,
						   int *xdec, 
						   const float *pIntrinsics, 
						   const int *pCodewords, 
						   const int nCodewords);

int		q65_esnodb(const q65_codec_ds *pCodec,
					float		*pEsNodB,
					const int *ydec, 
					const float *pInputEnergies);

int		q65_esnodb_fastfading(
					const q65_codec_ds *pCodec,
					float		*pEsNodB,
					const int   *ydec,
					const float *pInputEnergies);


// helper functions
#define q65_get_message_length(pCodec)  _q65_get_message_length((pCodec)->pQraCode)
#define q65_get_codeword_length(pCodec) _q65_get_codeword_length((pCodec)->pQraCode)
#define q65_get_code_rate(pCodec)		_q65_get_code_rate((pCodec)->pQraCode)
#define q65_get_alphabet_size(pCodec)	_q65_get_alphabet_size((pCodec)->pQraCode)
#define q65_get_bits_per_symbol(pCodec) _q65_get_bits_per_symbol((pCodec)->pQraCode)


// internally used but made public for the above defines
int		_q65_get_message_length(const qracode *pCode);
int		_q65_get_codeword_length(const qracode *pCode);
float	_q65_get_code_rate(const qracode *pCode);
static void	_q65_mask(const qracode *pcode, float *ix, const int *mask, const int *x);
int		_q65_get_alphabet_size(const qracode *pCode);
int		_q65_get_bits_per_symbol(const qracode *pCode);

// internally used but made public for threshold optimization
int q65_check_llh(float *llh, const int* ydec, const int nN, const int nM, const float *pIntrin);

#endif // _qra65_h
