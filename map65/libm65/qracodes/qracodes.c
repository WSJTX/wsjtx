// qracodes.c
// Q-ary RA codes encoding/decoding functions
// 
// (c) 2016 - Nico Palermo, IV3NWV - Microtelecom Srl, Italy
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

#include <stdio.h>
#include <math.h>

#include "npfwht.h"	
#include "pdmath.h"

#include "qracodes.h"

int qra_encode(const qracode *pcode, int *y, const int *x)
{
	int k,j,kk,jj;
	int t, chk = 0;

	const int K = pcode->K;
	const int M = pcode->M;
	const int NC= pcode->NC;
	const int a = pcode->a;
	const int  *acc_input_idx  = pcode->acc_input_idx;
	const int *acc_input_wlog = pcode->acc_input_wlog;
	const int  *gflog		   = pcode->gflog;
	const int *gfexp          = pcode->gfexp;

	// copy the systematic symbols to destination
	memcpy(y,x,K*sizeof(int));

	y = y+K;	// point to check symbols

	// compute the code check symbols as a weighted accumulation of a permutated
	// sequence of the (repeated) systematic input symbols:
	// chk(k+1) = x(idx(k))*alfa^(logw(k)) + chk(k)
	// (all operations performed over GF(M))

	if (a==1) { // grouping factor = 1
		for (k=0;k<NC;k++) {
			t = x[acc_input_idx[k]];
			if (t) {
				// multiply input by weight[k] and xor it with previous check
				t = (gflog[t] + acc_input_wlog[k])%(M-1);
				t = gfexp[t];
				chk ^=t;
				}
			y[k] = chk;
			}

		#ifdef QRA_DEBUG
			// verify that the encoder accumulator is terminated to 0
			// (we designed the code this way so that Iex = 1 when Ia = 1)
			t = x[acc_input_idx[k]];
			if (t) {
				t = (gflog[t] + acc_input_wlog[k])%(M-1);
				t = gfexp[t];
				// accumulation
				chk ^=t;
				}
			return (chk==0);
		#else
			return 1;
		#endif // QRA_DEBUG
		}
	else { // grouping factor > 1
		for (k=0;k<NC;k++) {
			kk = a*k;
			for (j=0;j<a;j++) {
				jj = kk+j;
				// irregular grouping support
				if (acc_input_idx[jj]<0)
					continue;
				t = x[acc_input_idx[jj]];
				if (t) {
					// multiply input by weight[k] and xor it with previous check
					t = (gflog[t] + acc_input_wlog[jj])%(M-1);
					t = gfexp[t];
					chk ^=t;
					}
				}
			y[k] = chk;
			}
		#ifdef QRA_DEBUG
			// verify that the encoder accumulator is terminated to 0
			// (we designed the code this way so that Iex = 1 when Ia = 1)
			kk = a*k;
			for (j=0;j<a;j++) {
				jj = kk+j;
				if (acc_input_idx[jj]<0)
					continue;
				t = x[acc_input_idx[jj]];
				if (t) {
					// multiply input by weight[k] and xor it with previous check
					t = (gflog[t] + acc_input_wlog[jj])%(M-1);
					t = gfexp[t];
					chk ^=t;
					}
				}
			return (chk==0);
		#else
			return 1;
		#endif // QRA_DEBUG
		} 
}

static void qra_ioapprox(float *src, float C, int nitems)
{
	// In place approximation of the modified bessel function I0(x*C)
	// Computes src[k] = Io(src[k]*C) where Io() is the modified Bessel function of first kind and order 0

	float v;
	float vsq;

	while (nitems--) {
		v = src[nitems]*C;

		// rational approximation of log(Io(v))
		vsq = v*v;
		v = vsq*(v+0.039f)/(vsq*.9931f+v*2.6936f+0.5185f);

		if (v>80.f) // avoid floating point exp() overflows 
			v=80.f;

		src[nitems] = (float)exp(v);
		}
}


float qra_mfskbesselmetric(float *pix, const float *rsq, const int m, const int N, float EsNoMetric)
{
	// Computes the codeword symbols intrinsic probabilities
	// given the square of the received input amplitudes.

	// The input vector rqs must be a linear array of size M*N, where M=2^m,
	// containing the squared amplitudes (rp*rp+rq*rq) of the input samples

	// First symbol amplitudes should be stored in the first M positions,
	// second symbol amplitudes stored at positions [M ... 2*M-1], and so on.

	// Output vector is the intrinsic symbol metric (the probability distribution)
	// assuming that symbols are transmitted using a M-FSK modulation 
	// and incoherent demodulation.

	// As the input Es/No is generally unknown (as it cannot be exstimated accurately
	// when the codeword length is few tens symbols) but an exact metric requires it
	// we simply fix it to a predefined EsNoMetric value so that the metric is what
	// expected at that specific value.
	// The metric computed in this way is optimal only at this predefined Es/No value,
	// nevertheless it is usually better than a generic parameter-free metric which
	// makes no assumptions on the input Es/No.

	// returns the estimated noise standard deviation

	int k;
	float rsum = 0.f;
	float sigmaest, cmetric;

	const int M = 1<<m;
	const int nsamples = M*N;

	// compute total power and modulus of input signal
	for (k=0;k<nsamples;k++) {
		rsum = rsum+rsq[k];
		pix[k] = (float)sqrt(rsq[k]);
		}

	rsum = rsum/nsamples;	// average S+N	

	// IMPORTANT NOTE: in computing the noise stdev it is assumed that 
	// in the input amplitudes there's no strong interference!
	// A more robust estimation can be done evaluating the histogram of the input amplitudes

	sigmaest = (float)sqrt(rsum/(1.0f+EsNoMetric/M)/2); // estimated noise stdev
	cmetric = (float)sqrt(2*EsNoMetric)/sigmaest;

	for (k=0;k<N;k++) {
		// compute bessel metric for each symbol in the codeword
		qra_ioapprox(PD_ROWADDR(pix,M,k),cmetric,M);
		// normalize to a probability distribution
		pd_norm(PD_ROWADDR(pix,M,k),m);
		}

	return sigmaest;
}


#ifdef QRA_DEBUG
void pd_print(int imsg,float *ppd,int size)
{
	int k;
	printf("imsg=%d\n",imsg);
	for (k=0;k<size;k++)
		printf("%7.1e ",ppd[k]);
	printf("\n");
}
#endif


#define ADDRMSG(fp, msgidx)    PD_ROWADDR(fp,qra_M,msgidx)
#define C2VMSG(msgidx)         PD_ROWADDR(qra_c2vmsg,qra_M,msgidx)
#define V2CMSG(msgidx)         PD_ROWADDR(qra_v2cmsg,qra_M,msgidx)
#define MSGPERM(logw)          PD_ROWADDR(qra_pmat,qra_M,logw)

#define QRACODE_MAX_M	256	// Maximum alphabet size handled by qra_extrinsic

int qra_extrinsic(const qracode *pcode, 
				  float *pex, 
				  const float *pix, 
				  int maxiter,
				  float *qra_v2cmsg,
				  float *qra_c2vmsg)
{
	const int qra_M		= pcode->M;
	const int qra_m		= pcode->m;
	const int qra_V		= pcode->V;
	const int qra_MAXVDEG  = pcode->MAXVDEG;
	const int  *qra_vdeg    = pcode->vdeg;
	const int qra_C		= pcode->C;
	const int qra_MAXCDEG  = pcode->MAXCDEG;
	const int *qra_cdeg    = pcode->cdeg;
	const int  *qra_v2cmidx = pcode->v2cmidx;
	const int  *qra_c2vmidx = pcode->c2vmidx;
	const int  *qra_pmat    = pcode->gfpmat;
	const int *qra_msgw    = pcode->msgw;

//	float msgout[qra_M];		 // buffer to store temporary results
	float msgout[QRACODE_MAX_M]; // we use a fixed size in order to avoid mallocs

	float totex;	// total extrinsic information
	int nit;		// current iteration
	int nv;		// current variable
	int nc;		// current check
	int k,kk;		// loop indexes

	int ndeg;		// current node degree
	int msgbase;	// current offset in the table of msg indexes
	int imsg;		// current message index
	int wmsg;		// current message weight

	int rc     = -1; // rc>=0  extrinsic converged to 1 at iteration rc (rc=0..maxiter-1)
					 // rc=-1  no convergence in the given number of iterations
					 // rc=-2  error in the code tables (code checks degrees must be >1)
					 // rc=-3  M is larger than QRACODE_MAX_M



	if (qra_M>QRACODE_MAX_M)
		return -3;

	// message initialization -------------------------------------------------------

	// init c->v variable intrinsic msgs
	pd_init(C2VMSG(0),pix,qra_M*qra_V);

	// init the v->c messages directed to code factors (k=1..ndeg) with the intrinsic info
	for (nv=0;nv<qra_V;nv++) {

		ndeg = qra_vdeg[nv];		// degree of current node
		msgbase = nv*qra_MAXVDEG;	// base to msg index row for the current node

		// copy intrinsics on v->c
		for (k=1;k<ndeg;k++) {		
			imsg = qra_v2cmidx[msgbase+k];
			pd_init(V2CMSG(imsg),ADDRMSG(pix,nv),qra_M);
			}
		}

	// message passing algorithm iterations ------------------------------

	for (nit=0;nit<maxiter;nit++) {

		// c->v step -----------------------------------------------------
		// Computes messages from code checks to code variables.
		// As the first qra_V checks are associated with intrinsic information
		// (the code tables have been constructed in this way)
		// we need to do this step only for code checks in the range [qra_V..qra_C)

		// The convolutions of probability distributions over the alphabet of a finite field GF(qra_M)
		// are performed with a fast convolution algorithm over the given field.
		// 
		// I.e. given the code check x1+x2+x3 = 0 (with x1,x2,x3 in GF(2^m))
		// and given Prob(x2) and Prob(x3), we have that:
		// Prob(x1=X1) = Prob((x2+x3)=X1) = sum((Prob(x2=X2)*Prob(x3=(X1+X2))) for all the X2s in the field
		// This translates to Prob(x1) = IWHT(WHT(Prob(x2))*WHT(Prob(x3)))
		// where WHT and IWHT are the direct and inverse Walsh-Hadamard transforms of the argument.
		// Note that the WHT and the IWHF differs only by a multiplicative coefficent and since in this step 
		// we don't need that the output distribution is normalized we use the relationship
		// Prob(x1) =(proportional to) WH(WH(Prob(x2))*WH(Prob(x3)))

		// In general given the check code x1+x2+x3+..+xm = 0
		// the output distribution of a variable given the distributions of the other m-1 variables
		// is the inverse WHT of the product of the WHTs of the distribution of the other m-1 variables
		// The complexity of this algorithm scales with M*log2(M) instead of the M^2 complexity of
		// the brute force approach (M=size of the alphabet)

		for (nc=qra_V;nc<qra_C;nc++) {

			ndeg = qra_cdeg[nc];		// degree of current node

			if (ndeg==1) 				// this should never happen (code factors must have deg>1)
				return -2;				// bad code tables

			msgbase = nc*qra_MAXCDEG;	// base to msg index row for the current node

			// transforms inputs in the Walsh-Hadamard "frequency" domain
			// v->c  -> fwht(v->c)
			for (k=0;k<ndeg;k++) {		
				imsg = qra_c2vmidx[msgbase+k];		// msg index
				np_fwht(qra_m,V2CMSG(imsg),V2CMSG(imsg)); // compute fwht 
				}

			// compute products and transform them back in the WH "time" domain
			for (k=0;k<ndeg;k++) {

				// init output message to uniform distribution
				pd_init(msgout,pd_uniform(qra_m),qra_M);

				// c->v = prod(fwht(v->c))
				// TODO: we assume that checks degrees are not larger than three but
				// if they are larger the products can be computed more efficiently
				for (kk=0;kk<ndeg;kk++) 
					if (kk!=k) {
						imsg = qra_c2vmidx[msgbase+kk];
						pd_imul(msgout,V2CMSG(imsg),qra_m);
						}

				// transform product back in the WH "time" domain
				
				// Very important trick: 
				// we bias WHT[0] so that the sum of output pd components is always strictly positive
				// this helps avoiding the effects of underflows in the v->c steps when multipling 
				// small fp numbers
				msgout[0]+=1E-7f;	// TODO: define the bias accordingly to the field size

				np_fwht(qra_m,msgout,msgout); 
			
				// inverse weight and output 
				imsg = qra_c2vmidx[msgbase+k]; // current output msg index
				wmsg = qra_msgw[imsg];		   // current msg weight

				if (wmsg==0)
					pd_init(C2VMSG(imsg),msgout,qra_M);
				else
					// output p(alfa^(-w)*x)
					pd_bwdperm(C2VMSG(imsg),msgout, MSGPERM(wmsg), qra_M);

				} // for (k=0;k<ndeg;k++)

			} // for (nc=qra_V;nc<qra_C;nc++)

		// v->c step -----------------------------------------------------
		for (nv=0;nv<qra_V;nv++) {

			ndeg = qra_vdeg[nv];		// degree of current node
			msgbase = nv*qra_MAXVDEG;	// base to msg index row for the current node

			for (k=0;k<ndeg;k++) {
				// init output message to uniform distribution
				pd_init(msgout,pd_uniform(qra_m),qra_M);

				// v->c msg = prod(c->v)
				// TODO: factor factors to reduce the number of computations for high degree nodes
				for (kk=0;kk<ndeg;kk++) 
					if (kk!=k) {
						imsg = qra_v2cmidx[msgbase+kk];
						pd_imul(msgout,C2VMSG(imsg),qra_m);
						}

#ifdef QRA_DEBUG
// normalize and check if product of messages v->c are null
				// normalize output to a probability distribution
				if (pd_norm(msgout,qra_m)<=0) {
					// dump msgin;
					printf("warning: v->c pd with invalid norm. nit=%d nv=%d k=%d\n",nit,nv,k);
					for (kk=0;kk<ndeg;kk++) {
						imsg = qra_v2cmidx[msgbase+kk];
						pd_print(imsg,C2VMSG(imsg),qra_M);
						}
					printf("-----------------\n");
					}
#else
				// normalize the result to a probability distribution
				pd_norm(msgout,qra_m);
#endif
				// weight and output 
				imsg = qra_v2cmidx[msgbase+k]; // current output msg index
				wmsg = qra_msgw[imsg];		   // current msg weight

				if (wmsg==0) {
					pd_init(V2CMSG(imsg),msgout,qra_M);
					}
				else {
					// output p(alfa^w*x)
					pd_fwdperm(V2CMSG(imsg),msgout, MSGPERM(wmsg), qra_M);
					}

				} // for (k=0;k<ndeg;k++)
			} // for (nv=0;nv<qra_V;nv++)

		// check extrinsic information ------------------------------
		// We assume that decoding is successful if each of the extrinsic
		// symbol probability is close to ej, where ej = [0 0 0 1(j-th position) 0 0 0 ] 
		// Therefore, for each symbol k in the codeword we compute max(prob(Xk)) 
		// and we stop the iterations if sum(max(prob(xk)) is close to the codeword length
		// Note: this is a more restrictive criterium than that of computing the a 
		// posteriori probability of each symbol, making a hard decision and then check 
		// if the codeword syndrome is null.
		// WARNING: this is tricky and probably works only for the particular class of RA codes 
		// we are coping with (we designed the code weights so that for any input symbol the
		// sum of its weigths is always 0, thus terminating the accumulator trellis to zero
		// for every combination of the systematic symbols).
		// More generally we should instead compute the max a posteriori probabilities
		// (as a product of the intrinsic and extrinsic information), make a symbol by symbol hard
		// decision and then check that the syndrome of the result is indeed null.

		totex = 0;
		for (nv=0;nv<qra_V;nv++) 
			totex += pd_max(V2CMSG(nv),qra_M);

		if (totex>(1.*(qra_V)-0.01)) {
			// the total maximum extrinsic information of each symbol in the codeword
			// is very close to one. This means that we have reached the (1,1) point in the
			// code EXIT chart(s) and we have successfully decoded the input.
			rc = nit;
			break;	// remove the break to evaluate the decoder speed performance as a function of the max iterations number)
			}

		} // for (nit=0;nit<maxiter;nit++)

	// copy extrinsic information to output to do the actual max a posteriori prob decoding
	pd_init(pex,V2CMSG(0),(qra_M*qra_V));
	return rc;
}

void qra_mapdecode(const qracode *pcode, int *xdec, float *pex, const float *pix)
{
// Maximum a posteriori probability decoding.
// Given the intrinsic information (pix) and extrinsic information (pex) (computed with qra_extrinsic(...))
// compute pmap = pex*pix and decode each (information) symbol of the received codeword
// as the symbol which maximizes pmap

// Returns:
//	xdec[k] = decoded (information) symbols k=[0..qra_K-1]

//  Note: pex is destroyed and overwritten with mapp

	const int qra_M		= pcode->M;
	const int qra_m		= pcode->m;
	const int qra_K		= pcode->K;

	int k;

	for (k=0;k<qra_K;k++) {
		// compute a posteriori prob
		pd_imul(PD_ROWADDR(pex,qra_M,k),PD_ROWADDR(pix,qra_M,k),qra_m);
		xdec[k]=pd_argmax(NULL, PD_ROWADDR(pex,qra_M,k), qra_M);
		}
}
