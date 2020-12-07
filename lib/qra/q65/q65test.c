// q65test.c 
// Word Error Rate test example for the Q65 mode
// Multi-threaded simulator version

// (c) 2020 - Nico Palermo, IV3NWV
// 
//
// ------------------------------------------------------------------------------
// This file is part of the qracodes project, a Forward Error Control
// encoding/decoding package based on Q-ary RA (Repeat and Accumulate) LDPC codes.
//
// Dependencies:
//    q65test.c		 - this file
//    normrnd.c/.h   - random gaussian number generator
//    npfwht.c/.h    - Fast Walsh-Hadamard Transforms
//    pdmath.c/.h    - Elementary math on probability distributions
//    qra15_65_64_irr_e23.c/.h - Tables for the QRA(15,65) irregular RA code used by Q65
//    qracodes.c/.h  - QRA codes encoding/decoding functions
//    fadengauss.c	 - fading coefficients tables for gaussian shaped fast fading channels
//    fadenlorenz.c	 - fading coefficients tables for lorenzian shaped fast fading channels
//
// -------------------------------------------------------------------------------
//
//    This is free software: you can redistribute it and/or modify
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
//
// ------------------------------------------------------------------------------

// OS dependent defines and includes --------------------------------------------

#if _WIN32 // note the underscore: without it, it's not msdn official!
	// Windows (x64 and x86)
	#define _CRT_SECURE_NO_WARNINGS  // we don't need warnings for sprintf/fopen function usage
	#include <windows.h>   // required only for GetTickCount(...)
	#include <process.h>   // _beginthread
#endif

#if defined(__linux__)

// remove unwanted macros
#define __cdecl

// implements Windows API
#include <time.h>

 unsigned int GetTickCount(void) {
    struct timespec ts;
    unsigned int theTick = 0U;
    clock_gettime( CLOCK_REALTIME, &ts );
    theTick  = ts.tv_nsec / 1000000;
    theTick += ts.tv_sec * 1000;
    return theTick;
}

// Convert Windows millisecond sleep
//
// VOID WINAPI Sleep(_In_ DWORD dwMilliseconds);
//
// to Posix usleep (in microseconds)
//
// int usleep(useconds_t usec);
//
#include <unistd.h>
#define Sleep(x)  usleep(x*1000)

#endif

#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )
#include <pthread.h>
#endif

#if __APPLE__
#endif

#include <stdlib.h>
#include <stdio.h>

#include "qracodes.h"				// basic qra encoding/decoding functions
#include "normrnd.h"				// gaussian numbers generator
#include "pdmath.h"					// operations on probability distributions

#include "qra15_65_64_irr_e23.h"	// QRA code used by Q65
#include "q65.h"

#define Q65_TS		0.640f		// Q65 symbol time interval in seconds
#define Q65_REFBW	2500.0f		// reference bandwidth in Hz for SNR estimates

// -----------------------------------------------------------------------------------

#define NTHREADS_MAX 160  // if you have some big enterprise hardware

// channel types
#define CHANNEL_AWGN       0
#define CHANNEL_RAYLEIGH   1
#define CHANNEL_FASTFADING 2

// amount of a-priori information provided to the decoder
#define AP_NONE     0
#define AP_MYCALL   1
#define AP_HISCALL  2
#define AP_BOTHCALL 3
#define AP_FULL		4
#define AP_LAST AP_FULL

const char ap_str[AP_LAST+1][16] = {
	"None",
	"32 bit",
	"32 bit",
	"62 bit",
	"78 bit",
};
const char fnameout_sfx[AP_LAST+1][64] = {
	"-ap00.txt",
	"-ap32m.txt",
	"-ap32h.txt",
	"-ap62.txt",
	"-ap78.txt"
};

const char fnameout_pfx[3][64] = {
	"wer-awgn-",
	"wer-rayl-",
	"wer-ff-"
};

// AP masks are computed assuming that the source message has been packed in 13 symbols s[0]..[s12]
// in a little indian format, that's to say:

// s[0] = {src5  src4  src3 src2 src1 src0}
// s[1] = {src11 src10 src9 src8 src7 src6}
// ...
// s[12]= {src78 src77 src76 src75 src74 src73}
//
// where srcj is the j-th bit of the source message.
//
// It is also assumed that the source message is as indicated by the protocol specification of wsjt-x
// structured messages. src78 should be always set to a value known by the decoder (and masked as an AP bit)
// With this convention the field i3 of the structured message is mapped to bits src77 src76 src75,
// that's to say to the 3rd,4th and 5th bit of s[12].
// Therefore, if i3 is known in advance, since src78 is always known, 
// the AP mask for s[12] is 0x3C (4 most significant bits of s[12] are known)

const int ap_masks_q65[AP_LAST+1][13] = {
// AP0 Mask
{	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00},
// Mask first(c28 r1)  .... i3 src78   (AP32my  MyCall ?       ? StdMsg)
{	0x3F,	0x3F,	0x3F,	0x3F,	0x1F,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x3C},
// Mask second(c28 r1) .... i3 src78   (AP32his  ?      HisCall ? StdMsg)
{	0x00,	0x00,	0x00,	0x00,	0x20,	0x3F,	0x3F,	0x3F,	0x3F,	0x0F,	0x00,	0x00,	0x3C},
// Mask (c28 r1 c28 r1) ... i3 src78   (AP62    MyCall HisCall ? StdMsg)
{	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x0F,	0x00,	0x00,	0x3C},
// Mask All (c28 r1 c28 r1 R g15 StdMsg src78) (AP78)
{	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F,	0x3F},
};

int verbose = 0;

void printword(char *msg, int *x, int size)
{
	int k;
	printf("\n%s ",msg);
	for (k=0;k<size;k++)
		printf("%02hx ",x[k]);
	printf("\n");
}

typedef struct {
	int			channel_type;
	float		EbNodB;
	volatile int nt;
	volatile int nerrs;
	volatile int nerrsu;
	volatile int ncrcwrong;
	volatile int stop;
	volatile int done;
	int			ap_index;	// index to the a priori knowledge mask
	const qracode	*pcode;	// pointer to the code descriptor
#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )
	pthread_t thread;
#endif
} wer_test_ds;

typedef void( __cdecl *pwer_test_thread)(wer_test_ds*);

void wer_test_thread_awgnrayl(wer_test_ds *pdata)
{
	// Thread for the AWGN/Rayleigh channel types

	int nt		  = 0;		// transmitted codewords
	int nerrs	  = 0;		// total number of errors 
	int ncrcwrong = 0;		// number of decodes with wrong crc

	q65_codec_ds codec;

	int			rc, k;
	int			nK, nN, nM, nm, nSamples;
	int			*x, *y, *xdec, *ydec;
	const int	*apMask;
	float R;
	float		*rsquared, *pIntrinsics;
	float EsNodBestimated;

	// for channel simulation
	const float No = 1.0f;					// noise spectral density
	const float sigma   = sqrtf(No/2.0f);	// std dev of I/Q noise components
	const float sigmach = sqrtf(1/2.0f);	// std dev of I/Q channel gains (Rayleigh channel)
	float EbNo, EsNo, Es, A;
	float *rp, *rq, *chp, *chq;
	int channel_type = pdata->channel_type;

	rc = q65_init(&codec,pdata->pcode);

	if (rc<0) {
		printf("error in qra65_init\n");
		goto term_thread;
		}

	nK = q65_get_message_length(&codec);
	nN = q65_get_codeword_length(&codec);
	nM = q65_get_alphabet_size(&codec);
	nm = q65_get_bits_per_symbol(&codec);
	R  = q65_get_code_rate(&codec);

	nSamples = nN*nM;

	x			= (int*)malloc(nK*sizeof(int));
	xdec		= (int*)malloc(nK*sizeof(int));
	y			= (int*)malloc(nN*sizeof(int));
	ydec		= (int*)malloc(nN*sizeof(int));
	rsquared	= (float*)malloc(nSamples*sizeof(float));
	pIntrinsics	= (float*)malloc(nSamples*sizeof(float));

	// sets the AP mask to be used for this simulation
	if (pdata->ap_index==AP_NONE)
		apMask = NULL;	// we simply avoid masking if ap-index specifies no AP
	else
		apMask = ap_masks_q65[pdata->ap_index];

	// Channel simulation variables --------------------
	rp	 = (float*)malloc(nSamples*sizeof(float));
	rq	 = (float*)malloc(nSamples*sizeof(float));
	chp	 = (float*)malloc(nN*sizeof(float));
	chq	 = (float*)malloc(nN*sizeof(float));

	EbNo = (float)powf(10,pdata->EbNodB/10);
	EsNo = 1.0f*nm*R*EbNo;
	Es = EsNo*No;
	A = (float)sqrt(Es);

	// Generate a (meaningless) test message
	for (k=0;k<nK;k++)
		x[k] = k%nM;
	// printword("x", x,nK);

	// Encode
	q65_encode(&codec,y,x);
	// printword("y", y,nN);

	// Simulate the channel and decode
	// as long as we are stopped by our caller
	while (pdata->stop==0) {

		// Channel simulation --------------------------------------------
		// Generate AWGN noise
		normrnd_s(rp,nSamples,0,sigma);
		normrnd_s(rq,nSamples,0,sigma);

		if (channel_type == CHANNEL_AWGN) 
			// add symbol amplitudes
			for (k=0;k<nN;k++) 
				rp[k*nM+y[k]]+=A;
		else if (channel_type == CHANNEL_RAYLEIGH) {
			// generate Rayleigh distributed taps
			normrnd_s(chp,nN,0,sigmach);
			normrnd_s(chq,nN,0,sigmach);
			// add Rayleigh distributed symbol amplitudes
			for (k=0;k<nN;k++) {
				rp[k*nM+y[k]]+=A*chp[k];
				rq[k*nM+y[k]]+=A*chq[k];
				}
			}
		else {
			printf("Wrong channel_type %d\n",channel_type);
			goto term_thread;
			}

		// Compute the received energies
		for (k=0;k<nSamples;k++) 
			rsquared[k] = rp[k]*rp[k] + rq[k]*rq[k];

		// Channel simulation end --------------------------------------------

		// DECODING ----------------------------------------------------------

		// Compute intrinsics probabilities from the observed energies
		rc = q65_intrinsics(&codec,pIntrinsics,rsquared);
		if (rc<0) {
			printf("Error in qra65_intrinsics: rc=%d\n",rc);
			goto term_thread;
			}

		// Decode with the given AP information
		// This call can be repeated for any desierd apMask
		// until we manage to decode the message
		rc = q65_decode(&codec,ydec,xdec, pIntrinsics, apMask,x);


		switch (rc) {
			case -1:
				printf("Error in qra65_decode: rc=%d\n",rc);
				goto term_thread;
			case Q65_DECODE_FAILED:
				// decoder failed to converge
				nerrs++;
				break;
			case Q65_DECODE_CRCMISMATCH:
				// decoder converged but we found a bad crc
				nerrs++;
				ncrcwrong++;
				break;
			}

		// compute SNR from decoded codeword ydec and observed energies
		if (rc>0 && verbose==1) {
			float EbNodBestimated;
			float SNRdBestimated;
			q65_esnodb(&codec, &EsNodBestimated, ydec,rsquared);
			EbNodBestimated = EsNodBestimated -10.0f*log10f(R*nm);
			SNRdBestimated = EsNodBestimated  -10.0f*log10f(Q65_TS*Q65_REFBW);
			printf("\nEstimated Eb/No=%5.1fdB SNR2500=%5.1fdB",
					EbNodBestimated, 
					SNRdBestimated);
			}

		nt = nt+1;
		pdata->nt=nt;
		pdata->nerrs=nerrs;
		pdata->ncrcwrong = ncrcwrong;
		}

term_thread:

	free(x);
	free(xdec);
	free(y);
	free(ydec);
	free(rsquared);
	free(pIntrinsics);

	free(rp);
	free(rq);
	free(chp);
	free(chq);

	q65_free(&codec);

	// signal the calling thread we are quitting
	pdata->done=1;
	#if _WIN32
	_endthread();
	#endif
}

void wer_test_thread_ff(wer_test_ds *pdata)
{
	// We don't do a realistic simulation of the fading-channel here
	// If required give a look to the simulator used in the QRA64 mode.
	// For the purpose of testing the formal correctness of the Q65 decoder
	// fast-fadind routines here we simulate the channel as a Rayleigh channel
	// with no frequency spread but use the q65....-fastfading routines
	// to check that they produce correct results also in this case.

	const int submode = 2;		// Assume that we are using the Q65C tone spacing
	const float B90 = 4.0f;		// Configure the Q65 fast-fading decoder for a the given freq. spread
	const int fadingModel = 1;	// Assume a lorenzian frequency spread

	int nt		  = 0;		// transmitted codewords
	int nerrs	  = 0;		// total number of errors 
	int ncrcwrong = 0;		// number of decodes with wrong crc

	q65_codec_ds codec;

	int			rc, k;
	int			nK, nN, nM, nm, nSamples;
	int			*x, *y, *xdec, *ydec;
	const int	*apMask;
	float R;
	float		*rsquared, *pIntrinsics;
	float EsNodBestimated;

	int nBinsPerTone, nBinsPerSymbol;


	// for channel simulation
	const float No = 1.0f;					// noise spectral density
	const float sigma   = sqrtf(No/2.0f);	// std dev of I/Q noise components
	const float sigmach = sqrtf(1/2.0f);	// std dev of I/Q channel gains (Rayleigh channel)
	float EbNo, EsNo, Es, A;
	float *rp, *rq, *chp, *chq;
	int channel_type = pdata->channel_type;

	rc = q65_init(&codec,pdata->pcode);

	if (rc<0) {
		printf("error in q65_init\n");
		goto term_thread;
		}

	nK = q65_get_message_length(&codec);
	nN = q65_get_codeword_length(&codec);
	nM = q65_get_alphabet_size(&codec);
	nm = q65_get_bits_per_symbol(&codec);
	R  = q65_get_code_rate(&codec);


	nBinsPerTone   = 1<<submode;
	nBinsPerSymbol = nM*(2+nBinsPerTone); 
	nSamples = nN*nBinsPerSymbol;

	// sets the AP mask to be used for this simulation
	if (pdata->ap_index==AP_NONE)
		apMask = NULL;	// we simply avoid masking if ap-index specifies no AP
	else
		apMask = ap_masks_q65[pdata->ap_index];


	x			= (int*)malloc(nK*sizeof(int));
	xdec		= (int*)malloc(nK*sizeof(int));
	y			= (int*)malloc(nN*sizeof(int));
	ydec		= (int*)malloc(nN*sizeof(int));
	rsquared	= (float*)malloc(nSamples*sizeof(float));
	pIntrinsics	= (float*)malloc(nN*nM*sizeof(float));

	// Channel simulation variables --------------------
	rp	 = (float*)malloc(nSamples*sizeof(float));
	rq	 = (float*)malloc(nSamples*sizeof(float));
	chp	 = (float*)malloc(nN*sizeof(float));
	chq	 = (float*)malloc(nN*sizeof(float));

	EbNo = (float)powf(10,pdata->EbNodB/10);
	EsNo = 1.0f*nm*R*EbNo;
	Es = EsNo*No;
	A = (float)sqrt(Es);
	// -------------------------------------------------

	// generate a test message
	for (k=0;k<nK;k++)
		x[k] = k%nM;

	// printword("x", x,nK);

	// encode
	q65_encode(&codec,y,x);
	// printword("y", y,nN);

	while (pdata->stop==0) {

		// Channel simulation --------------------------------------------
		// generate AWGN noise
		normrnd_s(rp,nSamples,0,sigma);
		normrnd_s(rq,nSamples,0,sigma);


		// Generate Rayleigh distributed symbol amplitudes
		normrnd_s(chp,nN,0,sigmach);
		normrnd_s(chq,nN,0,sigmach);
		// Don't simulate a really frequency spreaded signal.
		// Just place the tones in the appropriate central bins
		// ot the received signal
		for (k=0;k<nN;k++) {
			rp[k*nBinsPerSymbol+y[k]*nBinsPerTone+nM]+=A*chp[k];
			rq[k*nBinsPerSymbol+y[k]*nBinsPerTone+nM]+=A*chq[k];
			}

		// compute the received energies
		for (k=0;k<nSamples;k++) 
			rsquared[k] = rp[k]*rp[k] + rq[k]*rq[k];

		// Channel simulation end --------------------------------------------

		// compute intrinsics probabilities from the observed energies
		// using the fast-fading version
		rc = q65_intrinsics_fastfading(&codec,pIntrinsics,rsquared,submode,B90,fadingModel);
		if (rc<0) {
			printf("Error in q65_intrinsics: rc=%d\n",rc);
			goto term_thread;
			}

		// decode with the given AP information (eventually with different apMasks and apSymbols)
		rc = q65_decode(&codec,ydec,xdec, pIntrinsics, apMask,x);

		switch (rc) {
			case -1:
				printf("Error in q65_decode: rc=%d\n",rc);
				goto term_thread;
			case Q65_DECODE_FAILED:
				// decoder failed to converge
				nerrs++;
				break;
			case Q65_DECODE_CRCMISMATCH:
				// decoder converged but we found a bad crc
				nerrs++;
				ncrcwrong++;
				break;
			}

		// compute SNR from decoded codeword ydec and observed energies rsquared
		if (rc>0 && verbose==1) {
			float EbNodBestimated;
			float SNRdBestimated;
			// use the fastfading version
			q65_esnodb_fastfading(&codec, &EsNodBestimated, ydec,rsquared);
			EbNodBestimated = EsNodBestimated -10.0f*log10f(R*nm);
			SNRdBestimated = EsNodBestimated  -10.0f*log10f(Q65_TS*Q65_REFBW);
			printf("\nEstimated Eb/No=%5.1fdB SNR2500=%5.1fdB",
					EbNodBestimated, 
					SNRdBestimated);
		}

		nt = nt+1;
		pdata->nt=nt;
		pdata->nerrs=nerrs;
		pdata->ncrcwrong = ncrcwrong;
		}

term_thread:

	free(x);
	free(xdec);
	free(y);
	free(ydec);
	free(rsquared);
	free(pIntrinsics);

	free(rp);
	free(rq);
	free(chp);
	free(chq);

	q65_free(&codec);

	// signal the calling thread we are quitting
	pdata->done=1;
	#if _WIN32
	_endthread();
	#endif
}


#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )

void *wer_test_pthread_awgnrayl(void *p)
{
	wer_test_thread_awgnrayl((wer_test_ds *)p);
	return 0;
}

void *wer_test_pthread_ff(void *p)
{
	wer_test_thread_ff((wer_test_ds *)p);
	return 0;
}

#endif


int wer_test_proc(const qracode *pcode, int nthreads, int chtype, int ap_index, float *EbNodB, int *nerrstgt, int nitems)
{
	int k,j,nt,nerrs,nerrsu,ncrcwrong,nd;
	int cini,cend; 
	char fnameout[128];
	FILE *fout;
	wer_test_ds wt[NTHREADS_MAX];
	float pe,peu,avgt;

	if (nthreads>NTHREADS_MAX) {
		printf("Error: nthreads should be <=%d\n",NTHREADS_MAX);
		return -1;
		}

	sprintf(fnameout,"%s%s%s",
				fnameout_pfx[chtype],
				pcode->name, 
				fnameout_sfx[ap_index]);

	fout = fopen(fnameout,"w");
	fprintf(fout,"#Code Name: %s\n",pcode->name);
	fprintf(fout,"#ChannelType (0=AWGN,1=Rayleigh,2=Fast-Fading)\n#Eb/No (dB)\n#Transmitted Codewords\n#Errors\n#CRC Errors\n#Undetected\n#Avg dec. time (ms)\n#WER\n#UER\n");

	printf("\nTesting the code %s\nSimulation data will be saved to %s\n",
			pcode->name, 
			fnameout);
	fflush (stdout);

	// init fixed thread parameters and preallocate buffers
	for (j=0;j<nthreads;j++)  {
		wt[j].channel_type=chtype;
		wt[j].ap_index = ap_index;
		wt[j].pcode    = pcode;
		}

	for (k=0;k<nitems;k++) {

		printf("\nTesting at Eb/No=%4.2f dB...",EbNodB[k]);
		fflush (stdout);

		for (j=0;j<nthreads;j++)  {
			wt[j].EbNodB=EbNodB[k];
			wt[j].nt=0;
			wt[j].nerrs=0;
			wt[j].nerrsu=0;
			wt[j].ncrcwrong=0;
			wt[j].done = 0;
			wt[j].stop = 0;
			#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )
				if (chtype==CHANNEL_FASTFADING) {
					if (pthread_create (&wt[j].thread, 0, wer_test_pthread_ff, &wt[j])) {
						perror ("Creating thread: ");
						exit (255);
						}
					}
				else {
					if (pthread_create (&wt[j].thread, 0, wer_test_pthread_awgnrayl, &wt[j])) {
						perror ("Creating thread: ");
						exit (255);
						}
					}
			#else
				if (chtype==CHANNEL_FASTFADING)
					_beginthread((void*)(void*)wer_test_thread_ff,0,&wt[j]);
				else
					_beginthread((void*)(void*)wer_test_thread_awgnrayl,0,&wt[j]);
			#endif
			}

		nd = 0;
		cini = GetTickCount();

		while (1) {
			// count errors
			nerrs = 0;
			for (j=0;j<nthreads;j++) 
				nerrs += wt[j].nerrs;
			// stop the working threads
			// if the number of errors at this Eb/No value 
			// reached the target value
			if (nerrs>=nerrstgt[k]) {
				for (j=0;j<nthreads;j++) 
					wt[j].stop = 1;
				break;
				}
			else { // continue with the simulation
				Sleep(2);
				nd = (nd+1)%100;
				if (nd==0) {
					if (verbose==0) {
							printf(".");
							fflush (stdout);
							}
					}
				}

			}

		cend = GetTickCount();

		// wait for the working threads to exit
		for (j=0;j<nthreads;j++) 
		#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )
		{
			void *rc;
			if (pthread_join (wt[j].thread, &rc)) {
				perror ("Waiting working threads to exit");
				exit (255);
			}
		}
		#else
			while(wt[j].done==0)
				Sleep(1);

		#endif
		printf("\n");
		fflush (stdout);

		// compute the total number of transmitted codewords
		// the total number of errors and the total number of undetected errors
		nt = 0;
		nerrs =0;
		nerrsu = 0;
		ncrcwrong = 0;
		for (j=0;j<nthreads;j++) {
			nt += wt[j].nt;
			nerrs += wt[j].nerrs;
			nerrsu += wt[j].nerrsu;
			ncrcwrong += wt[j].ncrcwrong;
			}

		pe = 1.0f*nerrs/nt;			// word error rate 
		avgt = 1.0f*(cend-cini)/nt; // average time per decode (ms)
		peu = 1.0f*ncrcwrong/4095/nt;

		printf("Elapsed Time=%6.1fs (%5.2fms/word)\nTransmitted=%8d  Errors=%6d CRCErrors=%3d  Undet=%3d - WER=%8.2e UER=%8.2e \n",
				0.001f*(cend-cini),
				avgt, nt, nerrs, ncrcwrong, nerrsu, pe, peu);
		fflush (stdout);

		// save simulation data to output file
		fprintf(fout,"%01d %6.2f %6d %6d %6d %6d %6.2f %8.2e %8.2e\n",
					chtype, 
					EbNodB[k],
					nt,
					nerrs,
					ncrcwrong,
					nerrsu, 
					avgt, 
					pe,
					peu);
		fflush(fout);

		}

	fclose(fout);

	return 0;
}

const qracode *codetotest[] = {
	&qra15_65_64_irr_e23,
};

void syntax(void)
{
	printf("\nQ65 Word Error Rate Simulator\n");
	printf("2020, Nico Palermo - IV3NWV\n\n");
	printf("Syntax: q65test [-q<code_index>] [-t<threads>] [-c<ch_type>] [-a<ap_index>] [-f<fnamein>[-h]\n");
	printf("Options: \n");
	printf("       -q<code_index>: code to simulate. 0=qra_15_65_64_irr_e23 (default)\n");
	printf("       -t<threads>   : number of threads to be used for the simulation [1..24]\n");
	printf("                       (default=8)\n");
	printf("       -c<ch_type>   : channel_type. 0=AWGN 1=Rayleigh 2=Fast-Fading\n");
	printf("                       (default=AWGN)\n");
	printf("       -a<ap_index>  : amount of a-priori information provided to decoder. \n");
	printf("                       0= No a-priori (default)\n");
	printf("                       1= 32 bit (Mycall)\n");
	printf("                       2= 32 bit (Hiscall)\n");
	printf("                       3= 62 bit (Bothcalls\n");
	printf("                       4= 78 bit (full AP)\n");
	printf("       -v            : verbose (output SNRs of decoded messages\n");

	printf("       -f<fnamein>   : name of the file containing the Eb/No values to be simulated\n");
	printf("                       (default=ebnovalues.txt)\n");
	printf("                       This file should contain lines in this format:\n");
	printf("                       # Eb/No(dB) Target Errors\n");
	printf("                       0.1 5000\n");
	printf("                       0.6 5000\n");
	printf("                       1.1 1000\n");
	printf("                       1.6 1000\n");
	printf("                       ...\n");
	printf("                       (lines beginning with a # are treated as comments\n\n");
}

#define SIM_POINTS_MAX 20

int main(int argc, char* argv[])
{

	float EbNodB[SIM_POINTS_MAX];
	int  nerrstgt[SIM_POINTS_MAX];
	FILE *fin;

	char fnamein[128]= "ebnovalues.txt";
	char buf[128];

	int nitems   = 0;
	int code_idx = 0;
	int nthreads = 8;
	int ch_type  = CHANNEL_AWGN;
	int ap_index = AP_NONE;

	// parse command line
	while(--argc) {
		argv++;
		if (strncmp(*argv,"-h",2)==0) {
			syntax();
			return 0;
			}
		else
		if (strncmp(*argv,"-q",2)==0) {
			code_idx = (int)atoi((*argv)+2);
			if (code_idx>7) {
				printf("Invalid code index\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-t",2)==0) {
			nthreads = (int)atoi((*argv)+2);
			
//			printf("nthreads = %d\n",nthreads);
			
			if (nthreads>NTHREADS_MAX) {
				printf("Invalid number of threads\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-c",2)==0) {
			ch_type = (int)atoi((*argv)+2);
			if (ch_type>CHANNEL_FASTFADING) {
				printf("Invalid channel type\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-a",2)==0) {
			ap_index = (int)atoi((*argv)+2);
			if (ap_index>AP_LAST) {
				printf("Invalid a-priori information index\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-f",2)==0) {
			strncpy(fnamein,(*argv)+2,127);
			}
		else
		if (strncmp(*argv,"-h",2)==0) {
			syntax();
			return -1;
			}
		else
		if (strncmp(*argv,"-v",2)==0) 
			verbose = TRUE;
		else {
			printf("Invalid option\n");
			syntax();
			return -1;
			}
		}

	// parse points to be simulated from the input file
	fin = fopen(fnamein,"r");
	if (!fin) {
		printf("Can't open file: %s\n",fnamein);
		syntax();
		return -1;
		}

	while (fgets(buf,128,fin)!=0) 
		if (*buf=='#' || *buf=='\n' )
			continue;
		else
			if (nitems==SIM_POINTS_MAX)
				break;
			else
				if (sscanf(buf,"%f %u",&EbNodB[nitems],&nerrstgt[nitems])!=2) {
					printf("Invalid input file format\n");
					syntax();
					return -1;
					}
				else
					nitems++;

	fclose(fin);

	if (nitems==0) {
		printf("No Eb/No point specified in file %s\n",fnamein);
		syntax();
		return -1;
		}

	printf("\nQ65 Word Error Rate Simulator\n");
	printf("(c) 2016-2020, Nico Palermo - IV3NWV\n\n");

	printf("Nthreads   = %d\n",nthreads);
	switch(ch_type) {
		case CHANNEL_AWGN:
			printf("Channel    = AWGN\n");
			break;
		case CHANNEL_RAYLEIGH:
			printf("Channel    = Rayleigh\n");
			break;
		case CHANNEL_FASTFADING:
			printf("Channel    = Fast Fading\n");
			break;
		}
	printf("Codename   = %s\n",codetotest[code_idx]->name);
	printf("A-priori   = %s\n",ap_str[ap_index]);
	printf("Eb/No input file = %s\n\n",fnamein);

	wer_test_proc(codetotest[code_idx], nthreads, ch_type, ap_index, EbNodB, nerrstgt, nitems);

	printf("\n\n\n");
	return 0;
}

