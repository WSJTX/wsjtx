// main.c 
// Word Error Rate test example for Q-ary RA codes over GF(64)
// 
// (c) 2016 - Nico Palermo, IV3NWV
// 
// Thanks to Andrea Montefusco IW0HDV for his help on adapting the sources
// to OSs other than MS Windows
//
// ------------------------------------------------------------------------------
// This file is part of the qracodes project, a Forward Error Control
// encoding/decoding package based on Q-ary RA (Repeat and Accumulate) LDPC codes.
//
// Files in this package:
//    main.c		 - this file
//    normrnd.c/.h   - random gaussian number generator
//    npfwht.c/.h    - Fast Walsh-Hadamard Transforms
//    pdmath.c/.h    - Elementary math on probability distributions
//    qra12_63_64_irr_b.c/.h - Tables for a QRA(12,63) irregular RA code over GF(64)
//    qra13_64_64_irr_e.c/.h - Tables for a QRA(13,64) irregular RA code "     "
//    qracodes.c/.h  - QRA codes encoding/decoding functions
//
// -------------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------

// Two codes are available for simulations in this sowftware release:

// QRA12_63_64_IRR_B: K=12 N=63 Q=64 irregular QRA code (defined in qra12_63_64_irr_b.h /.c)
// QRA13_64_64_IRR_E: K=13 N=64 Q=64 irregular QRA code (defined in qra13_64_64_irr_b.h /.c)

// Codes with K=13 are designed to include a CRC as the 13th information symbol
// and improve the code UER (Undetected Error Rate).
// The CRC symbol is not sent along the channel (the codes are punctured) and the 
// resulting code is still a (12,63) code with an effective code rate of R = 12/63. 

// ------------------------------------------------------------------------------

// OS dependent defines and includes --------------------------------------------

#if _WIN32 // note the underscore: without it, it's not msdn official!
	// Windows (x64 and x86)
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

#include "qracodes.h"		   
#include "normrnd.h"		   // gaussian numbers generator
#include "pdmath.h"			   // operations on probability distributions

// defined codes
#include "qra12_63_64_irr_b.h" 
#include "qra13_64_64_irr_e.h"  

// -----------------------------------------------------------------------------------

#define NTHREADS_MAX 160	

// channel types
#define CHANNEL_AWGN     0
#define CHANNEL_RAYLEIGH 1

// amount of a-priori information provided to the decoder
#define AP_NONE 0
#define AP_28   1
#define AP_44   2
#define AP_56   3

const char ap_str[4][16] = {
	"None",
	"28 bit",
	"44 bit",
	"56 bit"
};

const char fnameout_pfx[2][64] = {
	"wer-awgn-",
	"wer-rayleigh-"
};
const char fnameout_sfx[4][64] = {
	"-ap00.txt",
	"-ap28.txt",
	"-ap44.txt",
	"-ap56.txt"
};

const int ap_masks_jt65[4][13] = { 
// Each row must be 13 entries long (to handle puntc. codes 13,64)
// The mask of 13th symbol (crc) is alway initializated to 0
	// AP0  - no a-priori knowledge
	{   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0}, 
	// AP28 - 1st field known [cq ? ?] or [dst ? ?] 
	{0x3F,0x3F,0x3F,0x3F,0x3C,   0,   0,   0,   0,   0,   0,   0}, 
	// AP44 - 1st and 3rd fields known [cq ? 0] or [dst ? 0] 
	{0x3F,0x3F,0x3F,0x3F,0x3C,   0,   0,   0,   0,0x0F,0x3F,0x3F},  
	// AP56 - 1st and 2nd fields known [dst src ?] 
    {0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x30,   0,   0}
};

void ix_mask(const qracode *pcode, float *r, const int *mask, const int *x);

void printword(char *msg, int *x, int size)
{
	int k;
	printf("\n%s ",msg);
	for (k=0;k<size;k++)
		printf("%02hx ",x[k]);
	printf("\n");
}

typedef struct {
	int channel_type;
	float EbNodB;
	volatile int nt;
	volatile int nerrs;
	volatile int nerrsu;
	volatile int stop;
	volatile int done;
	int		ap_index;		// index to the a priori knowledge mask
	const qracode	*pcode;	// pointer to the code descriptor
	int	*x;				//[qra_K];		    input message buffer
	int	*y, *ydec;		//[qra_N];			encoded/decoded codewords buffers
	float	*qra_v2cmsg;	//[qra_NMSG*qra_M]; MP decoder v->c msg buffer 
	float	*qra_c2vmsg;	//[qra_NMSG*qra_M]; MP decoder c->v msg buffer
	float	*rp;			// [qra_N*qra_M];	received samples (real component) buffer
	float	*rq;			// [qra_N*qra_M];	received samples (imag component) buffer
	float   *chp;			//[qra_N];			channel gains (real component) buffer
	float	*chq;			//[qra_N];			channel gains (imag component) buffer
	float   *r;				//[qra_N*qra_M];	received samples (amplitude)   buffer
	float   *ix;			// [qra_N*qra_M];	// intrinsic information to the MP algorithm
	float   *ex;			// [qra_N*qra_M];	// extrinsic information from the MP algorithm

} wer_test_ds;

typedef void( __cdecl *pwer_test_thread)(wer_test_ds*);

// crc-6 generator polynomial
// g(x) = x^6 + a5*x^5 + ... + a1*x + a0

// g(x) = x^6 + x + 1  
#define CRC6_GEN_POL 0x30  // MSB=a0 LSB=a5    

// g(x) = x^6 + x^2 + x + 1 (as suggested by Joe. See:  https://users.ece.cmu.edu/~koopman/crc/)
// #define CRC6_GEN_POL 0x38  // MSB=a0 LSB=a5. Simulation results are similar

int calc_crc6(int *x, int sz)
{
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

void wer_test_thread(wer_test_ds *pdata)
{
	const qracode *pcode=pdata->pcode;
	const int qra_K = pcode->K;
	const int qra_N = pcode->N;
	const int qra_M = pcode->M;
	const int qra_m = pcode->m;
	const int NSAMPLES = pcode->N*pcode->M;

	const float No = 1.0f;		// noise spectral density
	const float sigma   = (float)sqrt(No/2.0f);	// std dev of noise I/Q components
	const float sigmach = (float)sqrt(1/2.0f);	// std dev of channel I/Q gains

	// Eb/No value for which we optimize the bessel metric
	const float EbNodBMetric = 2.8f; 
	const float EbNoMetric   = (float)pow(10,EbNodBMetric/10);

	int k,t,j,diff;
	float R;
	float EsNoMetric;
	float EbNo, EsNo, Es, A;
	int channel_type, code_type;
	int nt=0;				// transmitted codewords
	int nerrs  = 0;			// total number of errors 
	int nerrsu = 0;			// number of undetected errors
	int rc;


	// inizialize pointer to required buffers
	int *x=pdata->x;			// message buffer
	int *y=pdata->y, *ydec=pdata->ydec;	// encoded/decoded codeword buffers
	float *qra_v2cmsg=pdata->qra_v2cmsg; // table of the v->c messages
	float *qra_c2vmsg=pdata->qra_c2vmsg; // table of the c->v messages
	float *rp=pdata->rp;		// received samples (real component)
	float *rq=pdata->rq;		// received samples (imag component)
	float *chp=pdata->chp;		// channel gains (real component)
	float *chq=pdata->chq;		// channel gains (imag component)
	float *r=pdata->r;			// received samples amplitudes
	float *ix=pdata->ix;		// intrinsic information to the MP algorithm
	float *ex=pdata->ex;		// extrinsic information from the MP algorithm

	channel_type = pdata->channel_type;
	code_type    = pcode->type;

	// define the (true) code rate accordingly to the code type
	switch(code_type) {
		case QRATYPE_CRC:
			R = 1.0f*(qra_K-1)/qra_N;	
			break;
		case QRATYPE_CRCPUNCTURED:
			R = 1.0f*(qra_K-1)/(qra_N-1);	
			break;
		case QRATYPE_NORMAL:
		default:
			R = 1.0f*(qra_K)/(qra_N);	
		}

	EsNoMetric   = 1.0f*qra_m*R*EbNoMetric;

	EbNo = (float)pow(10,pdata->EbNodB/10);
	EsNo = 1.0f*qra_m*R*EbNo;
	Es = EsNo*No;
	A = (float)sqrt(Es);


	// encode the input
	if (code_type==QRATYPE_CRC || code_type==QRATYPE_CRCPUNCTURED) {
		// compute the information message symbol check as the (negated) xor of all the 
		// information message symbols 
		for (k=0;k<(qra_K-1);k++) 
			x[k]=k%qra_M;
		x[k]=calc_crc6(x,qra_K-1);
		}
	else 
		for (k=0;k<qra_K;k++)
			x[k]=k%qra_M;

	qra_encode(pcode,y,x);

	while (pdata->stop==0) {

		// simulate the channel
		// NOTE: in the case that the code is punctured, for simplicity
		// we compute the channel outputs and the metric also for the crc symbol
		// then we ignore its observation.
		normrnd_s(rp,NSAMPLES,0,sigma);
		normrnd_s(rq,NSAMPLES,0,sigma);

		if (channel_type == CHANNEL_AWGN) {
			for (k=0;k<qra_N;k++) 
				rp[k*qra_M+y[k]]+=A;
			}
		else if (channel_type == CHANNEL_RAYLEIGH) {
			normrnd_s(chp,qra_N,0,sigmach);
			normrnd_s(chq,qra_N,0,sigmach);
			for (k=0;k<qra_N;k++) {
				rp[k*qra_M+y[k]]+=A*chp[k];
				rq[k*qra_M+y[k]]+=A*chq[k];
				}
			}
		else {
			pdata->done = 1;
			return;	// unknown channel type
			}

		// compute the squares of the amplitudes of the received samples
		for (k=0;k<NSAMPLES;k++) 
			r[k] = rp[k]*rp[k] + rq[k]*rq[k];

		// compute the intrinsic symbols probabilities 
		qra_mfskbesselmetric(ix,r,pcode->m,pcode->N,EsNoMetric);

		if (code_type==QRATYPE_CRCPUNCTURED) {
			// ignore observations of the CRC symbol as it is not actually sent
			// over the channel
			pd_init(PD_ROWADDR(ix,qra_M,qra_K),pd_uniform(qra_m),qra_M);
			}


		if (pdata->ap_index!=0)
			// mask channel observations with a priori knowledge
			ix_mask(pcode,ix,ap_masks_jt65[pdata->ap_index],x);


		// compute the extrinsic symbols probabilities with the message-passing algorithm
		// stop if extrinsic information does not converges to 1 within the given number of iterations
		rc = qra_extrinsic(pcode,ex,ix,100,qra_v2cmsg,qra_c2vmsg);

		if (rc>=0) { // the MP algorithm converged to Iex~1 in rc iterations

			// decode the codeword
			qra_mapdecode(pcode,ydec,ex,ix);

			// look for undetected errors
			if (code_type==QRATYPE_CRC || code_type==QRATYPE_CRCPUNCTURED) {

				j = 0; diff = 0;
				for (k=0;k<(qra_K-1);k++) 
					diff |= (ydec[k]!=x[k]);
				t = calc_crc6(ydec,qra_K-1);
				if (t!=ydec[k]) // error detected - crc doesn't matches
					nerrs  += 1;
				else
					if (diff) {	// decoded message is not equal to the transmitted one but 
						        // the crc test passed
						// add as undetected error
						nerrsu += 1;
						nerrs  += 1;
						// uncomment to see what the undetected error pattern looks like
						//printword("U", ydec);
						}
				}
			else 			
				for (k=0;k<qra_K;k++) 
					if (ydec[k]!=x[k]) {	// decoded msg differs from the transmitted one
						nerrsu += 1;		// it's a false decode
						nerrs  += 1;
						// uncomment to see what the undetected error pattern looks like
						// printword("U", ydec);
						break;
						}

			}	
		else // failed to converge to a solution within the given number of iterations
			nerrs++;

		nt = nt+1;

		pdata->nt=nt;
		pdata->nerrs=nerrs;
		pdata->nerrsu=nerrsu;

		}

	pdata->done=1;

	#if _WIN32
	_endthread();
	#endif
}

#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )

void *wer_test_pthread(void *p)
{
	wer_test_thread ((wer_test_ds *)p);
	return 0;
}

#endif

void ix_mask(const qracode *pcode, float *r, const int *mask, const int *x)
{
	// mask intrinsic information (channel observations) with a priori knowledge
	
	int k,kk, smask;
	const int qra_K=pcode->K;
	const int qra_M=pcode->M;
	const int qra_m=pcode->m;

	for (k=0;k<qra_K;k++) {
		smask = mask[k];
		if (smask) {
			for (kk=0;kk<qra_M;kk++) 
				if (((kk^x[k])&smask)!=0)
					*(PD_ROWADDR(r,qra_M,k)+kk) = 0.f;

			pd_norm(PD_ROWADDR(r,qra_M,k),qra_m);
			}
		}
}


int wer_test_proc(const qracode *pcode, int nthreads, int chtype, int ap_index, float *EbNodB, int *nerrstgt, int nitems)
{
	int k,nn,j,nt,nerrs,nerrsu,nd;
	int cini,cend; 
	char fnameout[128];
	FILE *fout;
	wer_test_ds wt[NTHREADS_MAX];
	float pe,avgt;

	nn = sizeof(EbNodB)/sizeof(float);	// size of the EbNo array to test

	if (nthreads>NTHREADS_MAX) {
		printf("Error: nthreads should be <=%d\n",NTHREADS_MAX);
		return -1;
		}

	sprintf(fnameout,"%s%s%s",
				fnameout_pfx[chtype],
				pcode->name, 
				fnameout_sfx[ap_index]);

	fout = fopen(fnameout,"w");
	fprintf(fout,"# Channel (0=AWGN,1=Rayleigh), Eb/No (dB), Transmitted codewords, Errors, Undetected Errors, Avg dec. time (ms), WER\n");

	printf("\nTesting the code %s over the %s channel\nSimulation data will be saved to %s\n",
			pcode->name, 
			chtype==CHANNEL_AWGN?"AWGN":"Rayleigh",
			fnameout);
	fflush (stdout);

	// init fixed thread parameters and preallocate buffers
	for (j=0;j<nthreads;j++)  {
			wt[j].channel_type=chtype;
			wt[j].ap_index = ap_index;
			wt[j].pcode    = pcode;
			wt[j].x        = (int*)malloc(pcode->K*sizeof(int));
			wt[j].y        = (int*)malloc(pcode->N*sizeof(int));
			wt[j].ydec     = (int*)malloc(pcode->N*sizeof(int));
			wt[j].qra_v2cmsg = (float*)malloc(pcode->NMSG*pcode->M*sizeof(float));
			wt[j].qra_c2vmsg = (float*)malloc(pcode->NMSG*pcode->M*sizeof(float));
			wt[j].rp       = (float*)malloc(pcode->N*pcode->M*sizeof(float));
			wt[j].rq       = (float*)malloc(pcode->N*pcode->M*sizeof(float));
			wt[j].chp      = (float*)malloc(pcode->N*sizeof(float));
			wt[j].chq      = (float*)malloc(pcode->N*sizeof(float));
			wt[j].r        = (float*)malloc(pcode->N*pcode->M*sizeof(float));
			wt[j].ix       = (float*)malloc(pcode->N*pcode->M*sizeof(float));
			wt[j].ex       = (float*)malloc(pcode->N*pcode->M*sizeof(float));
		}


	for (k=0;k<nitems;k++) {

		printf("\nTesting at Eb/No=%4.1f dB...",EbNodB[k]);
		fflush (stdout);

		for (j=0;j<nthreads;j++)  {
			wt[j].EbNodB=EbNodB[k];
			wt[j].nt=0;
			wt[j].nerrs=0;
			wt[j].nerrsu=0;
			wt[j].done = 0;
			wt[j].stop = 0;
			#if defined(__linux__) || ( defined(__MINGW32__) || defined (__MIGW64__) )
			if (pthread_create (&wt[j].thread, 0, wer_test_pthread, &wt[j])) {
				perror ("Creating thread: ");
				exit (255);
			}
			#else
			_beginthread((void*)(void*)wer_test_thread,0,&wt[j]);
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
			// if number of errors reached at this Eb/No value
			if (nerrs>=nerrstgt[k]) {
				for (j=0;j<nthreads;j++) 
					wt[j].stop = 1;
				break;
				}
			else { // continue with the simulation
				Sleep(2);
				nd = (nd+1)%100;
				if (nd==0) {
					printf(".");
					fflush (stdout);
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
		for (j=0;j<nthreads;j++) {
			nt += wt[j].nt;
			nerrs += wt[j].nerrs;
			nerrsu += wt[j].nerrsu;
			}

		pe = 1.0f*nerrs/nt;			// word error rate 
		avgt = 1.0f*(cend-cini)/nt; // average time per decode (ms)

		printf("Elapsed Time=%6.1fs (%5.2fms/word)\nTransmitted=%8d - Errors=%6d - Undetected=%3d - WER=%.2e\n",
				0.001f*(cend-cini),
				avgt, nt, nerrs, nerrsu, pe);
		fflush (stdout);

		// save simulation data to output file
		fprintf(fout,"%01d %.2f %d %d %d %.2f %.2e\n",
					chtype, 
					EbNodB[k],
					nt,
					nerrs,
					nerrsu, 
					avgt, 
					pe);

		}

	fclose(fout);

	return 0;
}


const qracode *codetotest[] = {
	&qra_12_63_64_irr_b,
	&qra_13_64_64_irr_e
};

void syntax(void)
{
	printf("\nQ-ary Repeat-Accumulate Code Word Error Rate Simulator\n");
	printf("2016, Nico Palermo - IV3NWV\n\n");
	printf("Syntax: qracodes [-q<code_index>] [-t<threads>] [-c<ch_type>] [-a<ap_index>] [-f<fnamein>[-h]\n");
	printf("Options: \n");
	printf("       -q<code_index>: code to simulate. 0=qra_12_63_64_irr_b\n");
	printf("                                         1=qra_13_64_64_irr_e (default)\n");
	printf("       -t<threads>   : number of threads to be used for the simulation [1..24]\n");
	printf("                       (default=8)\n");
	printf("       -c<ch_type>   : channel_type. 0=AWGN 1=Rayleigh \n");
	printf("                       (default=AWGN)\n");
	printf("       -a<ap_index>  : amount of a-priori information provided to decoder. \n");
	printf("                       0= No a-priori (default)\n");
	printf("                       1= 28 bit \n");
	printf("                       2= 44 bit \n");
	printf("                       3= 56 bit \n");
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
	int code_idx = 1;
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
			if (code_idx>1) {
				printf("Invalid code index\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-t",2)==0) {
			nthreads = (int)atoi((*argv)+2);
			printf("nthreads = %d\n",nthreads);
			if (nthreads>NTHREADS_MAX) {
				printf("Invalid number of threads\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-c",2)==0) {
			ch_type = (int)atoi((*argv)+2);
			if (ch_type>CHANNEL_RAYLEIGH) {
				printf("Invalid channel type\n");
				syntax();
				return -1;
				}
			}
		else
		if (strncmp(*argv,"-a",2)==0) {
			ap_index = (int)atoi((*argv)+2);
			if (ap_index>AP_56) {
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

	printf("\nQ-ary Repeat-Accumulate Code Word Error Rate Simulator\n");
	printf("2016, Nico Palermo - IV3NWV\n\n");

	printf("Nthreads   = %d\n",nthreads);
	printf("Channel    = %s\n",ch_type==CHANNEL_AWGN?"AWGN":"Rayleigh");
	printf("Codename   = %s\n",codetotest[code_idx]->name);
	printf("A-priori   = %s\n",ap_str[ap_index]);
	printf("Eb/No input file = %s\n\n",fnamein);

	wer_test_proc(codetotest[code_idx], nthreads, ch_type, ap_index, EbNodB, nerrstgt, nitems);

	return 0;
}

