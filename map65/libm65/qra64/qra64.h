// qra64.h
// Encoding/decoding functions for the QRA64 mode
// 
// (c) 2016 - Nico Palermo, IV3NWV 
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

#ifndef _qra64_h_
#define _qra64_h_

// qra64_init(...) initialization flags
#define QRA_NOAP     0	// don't use a-priori knowledge
#define QRA_AUTOAP   1	// use  auto a-priori knowledge 
#define QRA_USERAP   2	// a-priori knowledge messages provided by the user

// QRA code parameters
#define QRA64_K  12	// information symbols
#define QRA64_N  63	// codeword length
#define QRA64_C  51	// (number of parity checks C=(N-K))
#define QRA64_M  64	// code alphabet size
#define QRA64_m  6	// bits per symbol

// packed predefined callsigns and fields as defined in JT65
#define CALL_CQ		0xFA08319 
#define CALL_QRZ	0xFA0831A 
#define CALL_CQ000      0xFA0831B
#define CALL_CQ999      0xFA08702
#define CALL_CQDX	0x5624C39
#define CALL_DE  	0xFF641D1
#define GRID_BLANK	0x7E91

// Types of a-priori knowledge messages
#define APTYPE_CQQRZ     0  // [cq/qrz ?       ?/blank]
#define APTYPE_MYCALL    1  // [mycall ?       ?/blank]
#define APTYPE_HISCALL   2  // [?      hiscall ?/blank]
#define APTYPE_BOTHCALLS 3  // [mycall hiscall ?]
#define APTYPE_FULL	 4  // [mycall hiscall grid]
#define APTYPE_CQHISCALL 5  // [cq/qrz hiscall ?/blank]
#define APTYPE_SIZE	(APTYPE_CQHISCALL+1)

typedef struct {
  float decEsNoMetric;
  int apflags;
  int apmsg_set[APTYPE_SIZE];     // indicate which ap type knowledge has 
                                  // been set by the user
// ap messages buffers
  int apmsg_cqqrz[12];		  // [cq/qrz ?       ?/blank] 
  int apmsg_call1[12];		  // [mycall ?       ?/blank] 
  int apmsg_call2[12];		  // [?      hiscall ?/blank] 
  int apmsg_call1_call2[12];      // [mycall hiscall ?]
  int apmsg_call1_call2_grid[12]; // [mycall hiscall grid]
  int apmsg_cq_call2[12];         // [cq     hiscall ?/blank] 
  int apmsg_cq_call2_grid[12];    // [cq     hiscall grid]

// ap messages masks
  int apmask_cqqrz[12];		
  int apmask_cqqrz_ooo[12];	
  int apmask_call1[12];        
  int apmask_call1_ooo[12];    
  int apmask_call2[12];        
  int apmask_call2_ooo[12];    
  int apmask_call1_call2[12];  
  int apmask_call1_call2_grid[12];  
  int apmask_cq_call2[12];  
  int apmask_cq_call2_ooo[12];  
} qra64codec;

#ifdef __cplusplus
extern "C" {
#endif

qra64codec *qra64_init(int flags);
// QRA64 mode initialization function
// arguments:
//    flags: set the decoder mode
//	     QRA_NOAP    use no a-priori information
//	     QRA_AUTOAP  use any relevant previous decodes
//	     QRA_USERAP  use a-priori information provided via qra64_apset(...)
// returns:
//    Pointer to initialized qra64codec data structure 
//		this pointer should be passed to the encoding/decoding functions
//
//    0   if unsuccessful (can't allocate memory)
// ----------------------------------------------------------------------------

void qra64_encode(qra64codec *pcodec, int *y, const int *x);
// QRA64 encoder
// arguments:
//    pcodec = pointer to a qra64codec data structure as returned by qra64_init
//    x      = pointer to the message to be encoded, int x[12]
//	       x must point to an array of integers (i.e. defined as int x[12])
//    y      = pointer to encoded message, int y[63]=
// ----------------------------------------------------------------------------

int  qra64_decode(qra64codec *pcodec, float *ebno, int *x, const float *r);
// QRA64 mode decoder
// arguments:
//    pcodec = pointer to a qra64codec data structure as returned by qra64_init
//    ebno   = pointer to a float where the avg Eb/No (in dB) will be stored
//             in case of successfull decoding 
//             (pass a null pointer if not interested)
//    x      = pointer to decoded message, int x[12]
//    r      = pointer to received symbol energies (squared amplitudes)
//             r must point to an array of length QRA64_M*QRA64_N (=64*63=4032) 
//	       The first QRA_M entries should be the energies of the first 
//             symbol in the codeword; the last QRA_M entries should be the 
//             energies of the last symbol in the codeword
//
// return code:
//
//  The return code is <0 when decoding is unsuccessful
//  -16 indicates that the definition of QRA64_NMSG does not match what required by the code
//  If the decoding process is successfull the return code is accordingly to the following table
//		rc=0    [?    ?    ?]    AP0	(decoding with no a-priori)
//		rc=1    [CQ   ?    ?]    AP27
//		rc=2    [CQ   ?     ]    AP44
//		rc=3    [CALL ?    ?]    AP29
//		rc=4    [CALL ?     ]    AP45
//		rc=5    [CALL CALL ?]    AP57
//		rc=6    [?    CALL ?]    AP29
//		rc=7    [?    CALL  ]    AP45
//		rc=8    [CALL CALL GRID] AP72 (actually a AP68 mask to reduce false decodes)
//		rc=9    [CQ   CALL ?]    AP55
//		rc=10   [CQ   CALL  ]    AP70 (actaully a AP68 mask to reduce false decodes)

//  return codes in the range 1-10 indicate the amount and the type of a-priori 
//  information was required to decode the received message.


// Decode a QRA64 msg using a fast-fading metric
int qra64_decode_fastfading(
				qra64codec *pcodec,		// ptr to the codec structure
				float *ebno,			// ptr to where the estimated Eb/No value will be saved
				int *x,					// ptr to decoded message 
				const float *rxen,		// ptr to received symbol energies array
				const int submode,		// submode idx (0=QRA64A ... 4=QRA64E)
				const float B90,	    // spread bandwidth (90% fractional energy)
				const int fadingModel); // 0=Gaussian 1=Lorentzian fade model
//
// rxen: The array of the received bin energies
//       Bins must be spaced by integer multiples of the symbol rate (1/Ts Hz)
//       The array must be an array of total length U = L x N where:
//			L: is the number of frequency bins per message symbol (see after)
//          N: is the number of symbols in a QRA64 msg (63)
//
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
// return codes: same return codes of qra64_decode (+some additional error codes)


// Simulate the fast-fading channel (to be used with qra64_decode_fastfading)
int qra64_fastfading_channel(
				float **rxen, 
				const int *xmsg, 
				const int submode,
				const float EbN0dB, 
				const float B90, 
				const int fadingModel);
// Simulate transmission over a fading channel with given B90, fading model and submode
// and non coherent detection.
// Sets rxen to point to an array of bin energies formatted as required 
// by the (fast-fading) decoding routine.
// returns 0 on success or negative values on error conditions


int qra64_apset(qra64codec *pcodec, const int mycall, const int hiscall, const int grid, const int aptype);
// Set decoder a-priori knowledge accordingly to the type of the message to 
// look up for
// arguments:
//    pcodec    = pointer to a qra64codec data structure as returned by qra64_init
//    mycall    = mycall to look for
//    hiscall   = hiscall to look for
//    grid      = grid to look for
//    aptype    = define the type of AP to be set: 
//		APTYPE_CQQRZ     set [cq/qrz ?       ?/blank]
//		APTYPE_MYCALL    set [mycall ?       ?/blank]
//		APTYPE_HISCALL   set [?      hiscall ?/blank]
//		APTYPE_BOTHCALLS set [mycall hiscall ?]
//		APTYPE_FULL		 set [mycall hiscall grid]
//		APTYPE_CQHISCALL set [cq/qrz hiscall ?/blank]

// returns:
//	 0  on success
//  -1  when qra64_init was called with the QRA_NOAP flag
//	-2  invalid apytpe (valid range [APTYPE_CQQRZ..APTYPE_CQHISCALL]
//	    (APTYPE_CQQRZ [cq/qrz ? ?] is set by default )

void qra64_apdisable(qra64codec *pcodec, const int aptype);
// disable specific AP type
// arguments:
//    pcodec    = pointer to a qra64codec data structure as returned by qra64_init
//    aptype    = define the type of AP to be disabled
//		APTYPE_CQQRZ     disable [cq/qrz   ?      ?/blank]
//		APTYPE_MYCALL    disable [mycall   ?      ?/blank]
//		APTYPE_HISCALL   disable [   ?   hiscall  ?/blank]
//		APTYPE_BOTHCALLS disable [mycall hiscall     ?   ]
//		APTYPE_FULL	 disable [mycall hiscall     grid]
//		APTYPE_CQHISCALL set [cq/qrz hiscall ?/blank]

void qra64_close(qra64codec *pcodec);
// Free memory allocated by qra64_init
// arguments:
//    pcodec = pointer to a qra64codec data structure as returned by qra64_init

// ----------------------------------------------------------------------------

// encode/decode std msgs in 12 symbols as done in jt65
void encodemsg_jt65(int *y, const int call1, const int call2, const int grid);
void decodemsg_jt65(int *call1, int *call2, int *grid, const int *x);

#ifdef __cplusplus
}
#endif

#endif // _qra64_h_
