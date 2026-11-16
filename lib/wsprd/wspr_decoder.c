/*
 This file is part of program wsprd, a detector/demodulator/decoder
 for the Weak Signal Propagation Reporter (WSPR) mode.

 File name: wspr_decoder.c

 Copyright 2001-2018, Joe Taylor, K1JT

 Much of the present code is based on work by Steven Franke, K9AN,
 which in turn was based on earlier work by K1JT.

 Copyright 2014-2018, Steven Franke, K9AN

 License: GNU GPL v3

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fftw3.h>
#include "wspr_decoder.h"
#include "wsprd_utils.h"
#include "wsprsim_utils.h"
#include "fano.h"
#include "jelinek.h"

#define max(x,y) ((x) > (y) ? (x) : (y))
#define PATIENCE FFTW_ESTIMATE
#define IQ_CAPACITY 65536
#define IQ_SAMPLES 46080
#define SUB_SAMPLES 45000
extern void osdwspr_(float [], unsigned char [], int *, unsigned char [], int *, float *);

struct wspr_decoder {
    char hashtab[WSPRD_HASH_COUNT * WSPRD_CALLSIGN_SIZE];
    char loctab[WSPRD_HASH_COUNT * WSPRD_GRID4_SIZE];
    float original_i[IQ_CAPACITY], original_q[IQ_CAPACITY];
    float residual_i[IQ_CAPACITY], residual_q[IQ_CAPACITY];
    float subtraction[6 * SUB_SAMPLES];
    float spectrum[512 * 359];
    fftwf_complex *fftin, *fftout, *iqin, *iqout;
    fftwf_plan spectrum_plan, inverse_plan;
    wspr_plan_guard plan_guard;
    int npoints, waveform;
    struct snode *stack;
    struct fano_node fano_nodes[82];
    unsigned int stack_buckets[1000];
};

static const unsigned char pr3[162]=
{1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
    0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
    0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
    0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
    0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
    0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
    0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0};

static void sync_and_demodulate(float *id, float *qd, long np,
                         unsigned char *symbols, float *f1, int ifmin, int ifmax, float fstep,
                         int *shift1, int lagmin, int lagmax, int lagstep,
                         float *drift1, int symfac, float *sync, int mode)
{
    /***********************************************************************
     * mode = 0: no frequency or drift search. find best time lag.          *
     *        1: no time lag or drift search. find best frequency.          *
     *        2: no frequency or time lag search. calculate soft-decision   *
     *           symbols using passed frequency and shift.                  *
     ************************************************************************/

    float fplast=-10000.0;
    static float dt=1.0/375.0, df=375.0/256.0;
    static float pi=3.14159265358979323846;
    float twopidt, df15=df*1.5, df05=df*0.5;

    int i, j, k, lag;
    float i0[162],q0[162],i1[162],q1[162],i2[162],q2[162],i3[162],q3[162];
    float p0,p1,p2,p3,cmet,totp,syncmax,fac;
    float c0[256],s0[256],c1[256],s1[256],c2[256],s2[256],c3[256],s3[256];
    float dphi0, cdphi0, sdphi0, dphi1, cdphi1, sdphi1, dphi2, cdphi2, sdphi2,
    dphi3, cdphi3, sdphi3;
    float f0=0.0, fp, ss, fbest=0.0, fsum=0.0, f2sum=0.0, fsymb[162];
    int best_shift = 0, ifreq;

    syncmax=-1e30;
    if( mode == 0 ) {ifmin=0; ifmax=0; fstep=0.0; f0=*f1;}
    if( mode == 1 ) {lagmin=*shift1;lagmax=*shift1;f0=*f1;}
    if( mode == 2 ) {lagmin=*shift1;lagmax=*shift1;ifmin=0;ifmax=0;f0=*f1;}

    twopidt=2*pi*dt;
    for(ifreq=ifmin; ifreq<=ifmax; ifreq++) {
        f0=*f1+ifreq*fstep;
        for(lag=lagmin; lag<=lagmax; lag=lag+lagstep) {
            ss=0.0;
            totp=0.0;
            for (i=0; i<162; i++) {
                fp = f0 + (*drift1/2.0)*((float)i-81.0)/81.0;
                if( i==0 || (fp != fplast) ) {  // only calculate sin/cos if necessary
                    dphi0=twopidt*(fp-df15);
                    cdphi0=cos(dphi0);
                    sdphi0=sin(dphi0);

                    dphi1=twopidt*(fp-df05);
                    cdphi1=cos(dphi1);
                    sdphi1=sin(dphi1);

                    dphi2=twopidt*(fp+df05);
                    cdphi2=cos(dphi2);
                    sdphi2=sin(dphi2);

                    dphi3=twopidt*(fp+df15);
                    cdphi3=cos(dphi3);
                    sdphi3=sin(dphi3);

                    c0[0]=1; s0[0]=0;
                    c1[0]=1; s1[0]=0;
                    c2[0]=1; s2[0]=0;
                    c3[0]=1; s3[0]=0;

                    for (j=1; j<256; j++) {
                        c0[j]=c0[j-1]*cdphi0 - s0[j-1]*sdphi0;
                        s0[j]=c0[j-1]*sdphi0 + s0[j-1]*cdphi0;
                        c1[j]=c1[j-1]*cdphi1 - s1[j-1]*sdphi1;
                        s1[j]=c1[j-1]*sdphi1 + s1[j-1]*cdphi1;
                        c2[j]=c2[j-1]*cdphi2 - s2[j-1]*sdphi2;
                        s2[j]=c2[j-1]*sdphi2 + s2[j-1]*cdphi2;
                        c3[j]=c3[j-1]*cdphi3 - s3[j-1]*sdphi3;
                        s3[j]=c3[j-1]*sdphi3 + s3[j-1]*cdphi3;
                    }
                    fplast = fp;
                }

                i0[i]=0.0; q0[i]=0.0;
                i1[i]=0.0; q1[i]=0.0;
                i2[i]=0.0; q2[i]=0.0;
                i3[i]=0.0; q3[i]=0.0;

                for (j=0; j<256; j++) {
                    k=lag+i*256+j;
                    if( (k>0) && (k<np) ) {
                        i0[i]=i0[i] + id[k]*c0[j] + qd[k]*s0[j];
                        q0[i]=q0[i] - id[k]*s0[j] + qd[k]*c0[j];
                        i1[i]=i1[i] + id[k]*c1[j] + qd[k]*s1[j];
                        q1[i]=q1[i] - id[k]*s1[j] + qd[k]*c1[j];
                        i2[i]=i2[i] + id[k]*c2[j] + qd[k]*s2[j];
                        q2[i]=q2[i] - id[k]*s2[j] + qd[k]*c2[j];
                        i3[i]=i3[i] + id[k]*c3[j] + qd[k]*s3[j];
                        q3[i]=q3[i] - id[k]*s3[j] + qd[k]*c3[j];
                    }
                }
                p0=i0[i]*i0[i] + q0[i]*q0[i];
                p1=i1[i]*i1[i] + q1[i]*q1[i];
                p2=i2[i]*i2[i] + q2[i]*q2[i];
                p3=i3[i]*i3[i] + q3[i]*q3[i];

                p0=sqrt(p0);
                p1=sqrt(p1);
                p2=sqrt(p2);
                p3=sqrt(p3);

                totp=totp+p0+p1+p2+p3;
                cmet=(p1+p3)-(p0+p2);
                ss = (pr3[i] == 1) ? ss+cmet : ss-cmet;
                if( mode == 2) {                 //Compute soft symbols
                    if(pr3[i]==1) {
                        fsymb[i]=p3-p1;
                    } else {
                        fsymb[i]=p2-p0;
                    }
                }
            }
            ss=ss/totp;
            if( ss > syncmax ) {          //Save best parameters
                syncmax=ss;
                best_shift=lag;
                fbest=f0;
            }
        } // lag loop
    } //freq loop

    if( mode <=1 ) {                       //Send best params back to caller
        *sync=syncmax;
        *shift1=best_shift;
        *f1=fbest;
        return;
    }

    if( mode == 2 ) {
        *sync=syncmax;
        for (i=0; i<162; i++) {              //Normalize the soft symbols
            fsum=fsum+fsymb[i]/162.0;
            f2sum=f2sum+fsymb[i]*fsymb[i]/162.0;
        }
        fac=sqrt(f2sum-fsum*fsum);
        for (i=0; i<162; i++) {
            fsymb[i]=symfac*fsymb[i]/fac;
            if( fsymb[i] > 127) fsymb[i]=127.0;
            if( fsymb[i] < -128 ) fsymb[i]=-128.0;
            symbols[i]=fsymb[i] + 128;
        }
        return;
    }
    return;
}

static void noncoherent_sequence_detection(float *id, float *qd, long np,
                                    unsigned char *symbols, float *f1,  int *shift1,
                                    float *drift1, int symfac, int *nblocksize, int *bitmetric)
{
    /************************************************************************
     *  Noncoherent sequence detection for wspr.                            *
     *  Allowed block lengths are nblock=1,2,3,6, or 9 symbols.             *
     *  Longer block lengths require longer channel coherence time.         *
     *  The whole block is estimated at once.                               *
     *  nblock=1 corresponds to noncoherent detection of individual symbols *
     *     like the original wsprd symbol demodulator.                      *
     ************************************************************************/
    float fplast=-10000.0;
    static float dt=1.0/375.0, df=375.0/256.0;
    static float pi=3.14159265358979323846;
    float twopidt, df15=df*1.5, df05=df*0.5;

    int i, j, k, lag, itone, ib, b, nblock, nseq, imask;
    float xi[512],xq[512];
    float is[4][162],qs[4][162],cf[4][162],sf[4][162],cm,sm,cmp,smp;
    float p[512],fac,xm1,xm0;
    float c0[257],s0[257],c1[257],s1[257],c2[257],s2[257],c3[257],s3[257];
    float dphi0, cdphi0, sdphi0, dphi1, cdphi1, sdphi1, dphi2, cdphi2, sdphi2,
    dphi3, cdphi3, sdphi3;
    float f0, fp, fsum=0.0, f2sum=0.0, fsymb[162];

    twopidt=2*pi*dt;
    f0=*f1;
    lag=*shift1;
    nblock=*nblocksize;
    nseq=1<<nblock;
    int bitbybit=*bitmetric;

    for (i=0; i<162; i++) {
        fp = f0 + (*drift1/2.0)*((float)i-81.0)/81.0;
        if( i==0 || (fp != fplast) ) {  // only calculate sin/cos if necessary
            dphi0=twopidt*(fp-df15);
            cdphi0=cos(dphi0);
            sdphi0=sin(dphi0);

            dphi1=twopidt*(fp-df05);
            cdphi1=cos(dphi1);
            sdphi1=sin(dphi1);

            dphi2=twopidt*(fp+df05);
            cdphi2=cos(dphi2);
            sdphi2=sin(dphi2);

            dphi3=twopidt*(fp+df15);
            cdphi3=cos(dphi3);
            sdphi3=sin(dphi3);

            c0[0]=1; s0[0]=0;
            c1[0]=1; s1[0]=0;
            c2[0]=1; s2[0]=0;
            c3[0]=1; s3[0]=0;

            for (j=1; j<257; j++) {
                c0[j]=c0[j-1]*cdphi0 - s0[j-1]*sdphi0;
                s0[j]=c0[j-1]*sdphi0 + s0[j-1]*cdphi0;
                c1[j]=c1[j-1]*cdphi1 - s1[j-1]*sdphi1;
                s1[j]=c1[j-1]*sdphi1 + s1[j-1]*cdphi1;
                c2[j]=c2[j-1]*cdphi2 - s2[j-1]*sdphi2;
                s2[j]=c2[j-1]*sdphi2 + s2[j-1]*cdphi2;
                c3[j]=c3[j-1]*cdphi3 - s3[j-1]*sdphi3;
                s3[j]=c3[j-1]*sdphi3 + s3[j-1]*cdphi3;
            }

            fplast = fp;
        }

        cf[0][i]=c0[256]; sf[0][i]=s0[256];
        cf[1][i]=c1[256]; sf[1][i]=s1[256];
        cf[2][i]=c2[256]; sf[2][i]=s2[256];
        cf[3][i]=c3[256]; sf[3][i]=s3[256];

        is[0][i]=0.0; qs[0][i]=0.0;
        is[1][i]=0.0; qs[1][i]=0.0;
        is[2][i]=0.0; qs[2][i]=0.0;
        is[3][i]=0.0; qs[3][i]=0.0;

        for (j=0; j<256; j++) {
            k=lag+i*256+j;
            if( (k>0) && (k<np) ) {
                is[0][i]=is[0][i] + id[k]*c0[j] + qd[k]*s0[j];
                qs[0][i]=qs[0][i] - id[k]*s0[j] + qd[k]*c0[j];
                is[1][i]=is[1][i] + id[k]*c1[j] + qd[k]*s1[j];
                qs[1][i]=qs[1][i] - id[k]*s1[j] + qd[k]*c1[j];
                is[2][i]=is[2][i] + id[k]*c2[j] + qd[k]*s2[j];
                qs[2][i]=qs[2][i] - id[k]*s2[j] + qd[k]*c2[j];
                is[3][i]=is[3][i] + id[k]*c3[j] + qd[k]*s3[j];
                qs[3][i]=qs[3][i] - id[k]*s3[j] + qd[k]*c3[j];
            }
        }
    }

    for (i=0; i<162; i=i+nblock) {
        for (j=0;j<nseq;j++) {
            xi[j]=0.0; xq[j]=0.0;
            cm=1; sm=0;
            for (ib=0; ib<nblock; ib++) {
                b=(j&(1<<(nblock-1-ib)))>>(nblock-1-ib);
                itone=pr3[i+ib]+2*b;
                xi[j]=xi[j]+is[itone][i+ib]*cm + qs[itone][i+ib]*sm;
                xq[j]=xq[j]+qs[itone][i+ib]*cm - is[itone][i+ib]*sm;
                cmp=cf[itone][i+ib]*cm - sf[itone][i+ib]*sm;
                smp=sf[itone][i+ib]*cm + cf[itone][i+ib]*sm;
                cm=cmp; sm=smp;
            }
            p[j]=xi[j]*xi[j]+xq[j]*xq[j];
            p[j]=sqrt(p[j]);
        }
        for (ib=0; ib<nblock; ib++) {
            imask=1<<(nblock-1-ib);
            xm1=0.0; xm0=0.0;
            for (j=0; j<nseq; j++) {
                if((j & imask)!=0) {
                    if(p[j] > xm1) xm1=p[j];
                }
                if((j & imask)==0) {
                    if(p[j]>xm0) xm0=p[j];
                }
            }
            fsymb[i+ib]=xm1-xm0;
            if( bitbybit == 1 ) {
                fsymb[i+ib]=fsymb[i+ib]/(xm1 > xm0 ? xm1 : xm0);
            }
        }
    }
    for (i=0; i<162; i++) {              //Normalize the soft symbols
        fsum=fsum+fsymb[i]/162.0;
        f2sum=f2sum+fsymb[i]*fsymb[i]/162.0;
    }
    fac=sqrt(f2sum-fsum*fsum);
    for (i=0; i<162; i++) {
        fsymb[i]=symfac*fsymb[i]/fac;
        if( fsymb[i] > 127) fsymb[i]=127.0;
        if( fsymb[i] < -128 ) fsymb[i]=-128.0;
        symbols[i]=fsymb[i] + 128;
    }
    return;
}

/******************************************************************************
  Subtract the coherent component of a signal
 *******************************************************************************/
static void subtract_signal2(float *id, float *qd, long np,
                      float f0, int shift0, float drift0, unsigned char* channel_symbols, float *scratch)
{
    float dt=1.0/375.0, df=375.0/256.0;
    float pi=4.*atan(1.0), twopidt, phi=0, dphi, cs;
    int i, j, k, ii, nsym=162, nspersym=256,  nfilt=360; //nfilt must be even number.
    int nsig=nsym*nspersym;
    int nc2=45000;

    float *refi, *refq, *ci, *cq, *cfi, *cfq;

    memset(scratch, 0, 6 * nc2 * sizeof(float));
    refi=scratch;
    refq=refi+nc2;
    ci=refq+nc2;
    cq=ci+nc2;
    cfi=cq+nc2;
    cfq=cfi+nc2;

    twopidt=2.0*pi*dt;

    /******************************************************************************
     Measured signal:                    s(t)=a(t)*exp( j*theta(t) )
     Reference is:                       r(t) = exp( j*phi(t) )
     Complex amplitude is estimated as:  c(t)=LPF[s(t)*conjugate(r(t))]
     so c(t) has phase angle theta-phi
     Multiply r(t) by c(t) and subtract from s(t), i.e. s'(t)=s(t)-c(t)r(t)
     *******************************************************************************/

    // create reference wspr signal vector, centered on f0.
    //
    for (i=0; i<nsym; i++) {

        cs=(float)channel_symbols[i];

        dphi=twopidt*
        (
         f0 + (drift0/2.0)*((float)i-(float)nsym/2.0)/((float)nsym/2.0)
         + (cs-1.5)*df
         );

        for ( j=0; j<nspersym; j++ ) {
            ii=nspersym*i+j;
            refi[ii]=cos(phi); //cannot precompute sin/cos because dphi is changing
            refq[ii]=sin(phi);
            phi=phi+dphi;
        }
    }

    float w[nfilt], norm=0, partialsum[nfilt];
    //lowpass filter and remove startup transient
    for (i=0; i<nfilt; i++) partialsum[i]=0.0;
    for (i=0; i<nfilt; i++) {
        w[i]=sin(pi*(float)i/(float)(nfilt-1));
        norm=norm+w[i];
    }
    for (i=0; i<nfilt; i++) {
        w[i]=w[i]/norm;
    }
    for (i=1; i<nfilt; i++) {
        partialsum[i]=partialsum[i-1]+w[i];
    }

    // s(t) * conjugate(r(t))
    // beginning of first symbol in reference signal is at i=0
    // beginning of first symbol in received data is at shift0.
    // filter transient lasts nfilt samples
    // leave nfilt zeros as a pad at the beginning of the unfiltered reference signal
    for (i=0; i<nsym*nspersym; i++) {
        k=shift0+i;
        if( (k>0) && (k<np) ) {
            ci[i+nfilt] = id[k]*refi[i] + qd[k]*refq[i];
            cq[i+nfilt] = qd[k]*refi[i] - id[k]*refq[i];
        }
    }

    // LPF
    for (i=nfilt/2; i<45000-nfilt/2; i++) {
        cfi[i]=0.0; cfq[i]=0.0;
        for (j=0; j<nfilt; j++) {
            cfi[i]=cfi[i]+w[j]*ci[i-nfilt/2+j];
            cfq[i]=cfq[i]+w[j]*cq[i-nfilt/2+j];
        }
    }

    // subtract c(t)*r(t) here
    // (ci+j*cq)(refi+j*refq)=(ci*refi-cq*refq)+j(ci*refq+cq*refi)
    // beginning of first symbol in reference signal is at i=nfilt
    // beginning of first symbol in received data is at shift0.
    for (i=0; i<nsig; i++) {
        if( i<nfilt/2 ) {        // take care of the end effect (LPF step response) here
            norm=partialsum[nfilt/2+i];
        } else if( i>(nsig-1-nfilt/2) ) {
            norm=partialsum[nfilt/2+nsig-1-i];
        } else {
            norm=1.0;
        }
        k=shift0+i;
        j=i+nfilt;
        if( (k>0) && (k<np) ) {
            id[k]=id[k] - (cfi[j]*refi[i]-cfq[j]*refq[i])/norm;
            qd[k]=qd[k] - (cfi[j]*refq[i]+cfq[j]*refi[i])/norm;
        }
    }

    return;
}

static unsigned int count_hard_errors( unsigned char *symbols, unsigned char *channel_symbols)
{
    int i,is;
    unsigned char cw[162];
    unsigned int nerrors;
    for (i=0; i<162; i++) {
        cw[i] = channel_symbols[i] >=2 ? 1:0;
    }
    deinterleave(cw);
    nerrors=0;
    for (i=0; i<162; i++) {
        is = symbols[i] > 127 ? 1:0;
        nerrors = nerrors + (is == cw[i] ? 0:1);
    }
    return nerrors;
}

/* FFTW's planner is not thread-safe. A host whose other threads plan
   transforms gives a guard that serializes them (wspr_decoder_create_guarded);
   without one, plans are made and destroyed directly. */
struct plan_call {
    int kind;                 /* 0 complex, 1 real to complex, 2 destroy */
    int n, sign;
    float *real_in;
    fftwf_complex *in, *out;
    fftwf_plan plan;
};

static void plan_call_run(void *arg)
{
    struct plan_call *c = arg;
    if (c->kind == 0) c->plan = fftwf_plan_dft_1d(c->n, c->in, c->out, c->sign, PATIENCE);
    else if (c->kind == 1) c->plan = fftwf_plan_dft_r2c_1d(c->n, c->real_in, c->out, PATIENCE);
    else {
        fftwf_destroy_plan(c->plan);
        c->plan = NULL;
    }
}

static fftwf_plan plan_call(const wspr_decoder *ctx, struct plan_call *c)
{
    if (!ctx->plan_guard) plan_call_run(c);
    else if (ctx->plan_guard(plan_call_run, c)) return c->kind == 2 ? c->plan : NULL;
    return c->plan;
}

static fftwf_plan plan_dft_1d(const wspr_decoder *ctx, int n, fftwf_complex *in,
                              fftwf_complex *out, int sign)
{
    struct plan_call c = {0, n, sign, NULL, in, out, NULL};
    return plan_call(ctx, &c);
}

static fftwf_plan plan_r2c_1d(const wspr_decoder *ctx, int n, float *in, fftwf_complex *out)
{
    struct plan_call c = {1, n, 0, in, NULL, out, NULL};
    return plan_call(ctx, &c);
}

static void destroy_plan(const wspr_decoder *ctx, fftwf_plan plan)
{
    struct plan_call c = {2, 0, 0, NULL, NULL, NULL, plan};
    plan_call(ctx, &c);
}

int32_t wspr_decoder_create(wspr_decoder **out)
{
    return wspr_decoder_create_guarded(out, NULL);
}

int32_t wspr_decoder_create_guarded(wspr_decoder **out, wspr_plan_guard guard)
{
    if (!out) return DECODER_INVALID;
    *out = NULL;
    wspr_decoder *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return DECODER_INTERNAL_ERROR;
    ctx->plan_guard = guard;
    ctx->fftin = fftwf_malloc(512 * sizeof *ctx->fftin);
    ctx->fftout = fftwf_malloc(512 * sizeof *ctx->fftout);
    ctx->iqin = fftwf_malloc(IQ_SAMPLES * sizeof *ctx->iqin);
    ctx->iqout = fftwf_malloc(IQ_SAMPLES * sizeof *ctx->iqout);
    if (!ctx->fftin || !ctx->fftout || !ctx->iqin || !ctx->iqout) {
        wspr_decoder_destroy(ctx);
        return DECODER_INTERNAL_ERROR;
    }
    ctx->spectrum_plan = plan_dft_1d(ctx, 512, ctx->fftin, ctx->fftout, FFTW_FORWARD);
    ctx->inverse_plan = plan_dft_1d(ctx, IQ_SAMPLES, ctx->iqin, ctx->iqout, FFTW_BACKWARD);
    if (!ctx->spectrum_plan || !ctx->inverse_plan) {
        wspr_decoder_destroy(ctx);
        return DECODER_INTERNAL_ERROR;
    }
    *out = ctx;
    return DECODER_OK;
}

void wspr_decoder_destroy(wspr_decoder *ctx)
{
    if (!ctx) return;
    if (ctx->inverse_plan) destroy_plan(ctx, ctx->inverse_plan);
    if (ctx->spectrum_plan) destroy_plan(ctx, ctx->spectrum_plan);
    fftwf_free(ctx->iqin);
    fftwf_free(ctx->iqout);
    fftwf_free(ctx->fftin);
    fftwf_free(ctx->fftout);
    free(ctx->stack);
    free(ctx);
}

void wspr_decoder_release(wspr_decoder *ctx)
{
    if (ctx) ctx->npoints = 0;
}

void wspr_decoder_reset(wspr_decoder *ctx)
{
    if (!ctx) return;
    wspr_decoder_release(ctx);
    memset(ctx->hashtab, 0, sizeof ctx->hashtab);
    memset(ctx->loctab, 0, sizeof ctx->loctab);
}

int32_t wspr_decoder_prepare_iq(wspr_decoder *ctx, const float *i, const float *q,
                               int32_t count, int32_t waveform)
{
    if (ctx) ctx->npoints = 0;  /* a failed prepare leaves no reception */
    if (!ctx || !i || !q || (waveform != 2 && waveform != 15) ||
        (count != 45000 && count != IQ_SAMPLES)) return DECODER_INVALID;
    memset(ctx->original_i, 0, sizeof ctx->original_i);
    memset(ctx->original_q, 0, sizeof ctx->original_q);
    memcpy(ctx->original_i, i, count * sizeof *i);
    memcpy(ctx->original_q, q, count * sizeof *q);
    ctx->npoints = count;
    ctx->waveform = waveform;
    return DECODER_OK;
}

int32_t wspr_decoder_prepare_pcm(wspr_decoder *ctx, const int16_t *pcm, int32_t count,
                                int32_t rate, int32_t waveform)
{
    if (ctx) ctx->npoints = 0;  /* a failed prepare leaves no reception */
    if (!ctx || !pcm || rate != 12000 || (waveform != 2 && waveform != 15) ||
        count < 1 || count > 114 * 12000 * (waveform == 15 ? 8 : 1)) return DECODER_INVALID;
    const int scale = waveform == 15 ? 8 : 1;
    const int nfft = IQ_SAMPLES * 32 * scale;
    /* The 12 kHz transform's buffers and plan last this call only, as
       readwavfile's did: WSPR-2's come to about 12 MB, WSPR-15's 94 MB. */
    float *in = fftwf_malloc(nfft * sizeof *in);
    fftwf_complex *out = fftwf_malloc((nfft / 2 + 1) * sizeof *out);
    fftwf_plan plan = in && out ? plan_r2c_1d(ctx, nfft, in, out) : NULL;
    if (!plan) {
        fftwf_free(in);
        fftwf_free(out);
        return DECODER_INTERNAL_ERROR;
    }
    for (int i = 0; i < count; ++i) in[i] = pcm[i] / 32768.0;
    memset(in + count, 0, (nfft - count) * sizeof *in);
    fftwf_execute(plan);
    double df = 12000.0 / nfft;
    int i0 = (waveform == 15 ? 1612.5 : 1500.0) / df + 0.5;
    for (int i = 0; i < IQ_SAMPLES; ++i) {
        int j = i0 + i;
        if (i > IQ_SAMPLES / 2) j -= IQ_SAMPLES;
        ctx->iqin[i][0] = out[j][0];
        ctx->iqin[i][1] = out[j][1];
    }
    destroy_plan(ctx, plan);
    fftwf_free(in);
    fftwf_free(out);
    fftwf_execute(ctx->inverse_plan);
    memset(ctx->original_i, 0, sizeof ctx->original_i);
    memset(ctx->original_q, 0, sizeof ctx->original_q);
    for (int i = 0; i < IQ_SAMPLES; ++i) {
        ctx->original_i[i] = ctx->iqout[i][0] / 1000.0;
        ctx->original_q[i] = ctx->iqout[i][1] / 1000.0;
    }
    ctx->npoints = IQ_SAMPLES;
    ctx->waveform = waveform;
    return DECODER_OK;
}

void wspr_decoder_default_tuning(struct wspr_tuning *t)
{
    *t = (struct wspr_tuning){10000, 0, 0, 0, 1, 3, -1, 0, 0.45f, 0.0};
}

int32_t wspr_decoder_import_hash_line(wspr_decoder *ctx, const char *line)
{
    return ctx && line && wsprd_load_hash_line(line, ctx->hashtab, ctx->loctab)
        ? DECODER_OK : DECODER_INVALID;
}

int wspr_decoder_hash_entry(wspr_decoder *ctx, int slot, const char **call, const char **grid)
{
    if (!ctx || slot < 0 || slot >= WSPRD_HASH_COUNT || !call || !grid) return 0;
    *call = ctx->hashtab + slot * WSPRD_CALLSIGN_SIZE;
    *grid = ctx->loctab + slot * WSPRD_GRID4_SIZE;
    return **call != 0;
}

static void unpack_fixed(char *out, const char *in, size_t n)
{
    memcpy(out, in, n);
    while (n && (out[n-1] == ' ' || out[n-1] == 0)) --n;
    out[n] = 0;
}

int32_t wspr_decoder_set_calls(wspr_decoder *ctx, const decoder_wspr_call *calls, int32_t count)
{
    if (!ctx || count < 0 || count > WSPRD_HASH_COUNT || (count && !calls)) return DECODER_INVALID;
    for (int i = 0; i < count; ++i) {
        char call[13], grid[5];
        unpack_fixed(call, calls[i].call, 12);
        unpack_fixed(grid, calls[i].grid, 4);
        size_t n = strlen(call), g = strlen(grid);
        if (!n || n > 10 || (g != 0 && g != 4)) return DECODER_INVALID;
        /* Only padding after the call or grid: no embedded NUL or space. */
        for (size_t j = 0; j < 12; ++j) {
            char c = calls[i].call[j];
            if (j < n ? !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '/'
                      : c != ' ' && c != 0) return DECODER_INVALID;
        }
        for (size_t j = 0; j < 4; ++j) {
            char c = calls[i].grid[j];
            if (j < g ? (j < 2 ? !(c >= 'A' && c <= 'R') : !(c >= '0' && c <= '9'))
                      : c != ' ' && c != 0) return DECODER_INVALID;
        }
    }
    memset(ctx->hashtab, 0, sizeof ctx->hashtab);
    memset(ctx->loctab, 0, sizeof ctx->loctab);
    for (int i = 0; i < count; ++i) {
        char call[13], grid[5];
        unpack_fixed(call, calls[i].call, 12);
        unpack_fixed(grid, calls[i].grid, 4);
        int slot = nhash(call, strlen(call), 146);
        strcpy(ctx->hashtab + slot * WSPRD_CALLSIGN_SIZE, call);
        strcpy(ctx->loctab + slot * WSPRD_GRID4_SIZE, grid);
    }
    return DECODER_OK;
}

int32_t wspr_decoder_get_calls(wspr_decoder *ctx, decoder_wspr_call *calls, int32_t cap, int32_t *count)
{
    if (!ctx || !count || cap < 0 || (cap && !calls)) return DECODER_INVALID;
    *count = 0;
    for (int i = 0; i < WSPRD_HASH_COUNT; ++i) {
        const char *call = ctx->hashtab + i * WSPRD_CALLSIGN_SIZE;
        const char *grid = ctx->loctab + i * WSPRD_GRID4_SIZE;
        if (!*call) continue;
        if (*count < cap) {
            memset(&calls[*count], ' ', sizeof *calls);
            memcpy(calls[*count].call, call, strlen(call));
            memcpy(calls[*count].grid, grid, strlen(grid));
        }
        ++*count;
    }
    return cap && cap < *count ? DECODER_CAPACITY : DECODER_OK;
}

int32_t wspr_decoder_run(wspr_decoder *ctx, const decoder_wspr_options *options,
                         const struct wspr_tuning *tuning, struct wspr_diagnostic *decodes,
                         int32_t *count, struct wspr_timing *timing,
                         wspr_residual_callback residual_callback, void *residual_user)
{
    if (count) *count = 0;
    if (!ctx || !options || !tuning || !decodes || !count || !ctx->npoints ||
        options->waveform != ctx->waveform || !isfinite(options->search_low_hz) ||
        !isfinite(options->search_high_hz) || !isfinite(options->calibration_error_hz) ||
        options->search_low_hz >= options->search_high_hz) return DECODER_INVALID;
    if (tuning->stackdecoder && !ctx->stack) {
        ctx->stack = calloc(200000, sizeof *ctx->stack);
        if (!ctx->stack) return DECODER_INTERNAL_ERROR;
    }
    memset(decodes, 0, WSPR_DECODER_MAX_RESULTS * sizeof *decodes);
    memcpy(ctx->residual_i, ctx->original_i, sizeof ctx->original_i);
    memcpy(ctx->residual_q, ctx->original_q, sizeof ctx->original_q);
    float *idat=ctx->residual_i, *qdat=ctx->residual_q;
    char *hashtab=ctx->hashtab, *loctab=ctx->loctab;
    unsigned char symbols[162]={0},decdata[11]={0},channel_symbols[162]={0},apmask[162]={0},cw[162]={0};
    signed char message[11]={0};
    char callsign[13]={0},grid[5]={0},call_loc_pow[23]={0};
    float allfreqs[100]={0};
    char allcalls[100][13]={{0}};
    int i,j,k,ipass,nblocksize,nhardmin=0,ihash,maxdrift;
    int shift1,lagmin,lagmax,lagstep,ifmin,ifmax,not_decoded;
    unsigned int nbits=81,stacksize=200000,npoints=ctx->npoints,cycles=0,maxnp,metric=0;
    struct snode *stack=tuning->stackdecoder ? ctx->stack : NULL;
    float df=375.0/256.0/2,dt=1.0/375.0,dt_print;
    float fsymbs[162],f1,fstep,sync1,drift1,dmin,psavg[512];
    double freq_print,dialfreq_error=tuning->calibration_error_hz;
    int wspr_type=options->waveform;
    float scale=wspr_type == 15 ? 8.0f : 1.0f;
    float center=wspr_type == 15 ? 1612.5f : 1500.0f;
    float fmin=(options->search_low_hz-center)*scale;
    float fmax=(options->search_high_hz-center)*scale;
    int quickmode=tuning->quickmode,more_candidates=tuning->more_candidates;
    int subtraction=tuning->subtraction;
    int npasses=tuning->npasses,ndepth=tuning->ndepth,verbose=tuning->verbose;
    unsigned int maxcycles=tuning->maxcycles;
    float bias=tuning->bias;
    float minsync1=0.10,minsync2=0.12;
    int iifac=8,symfac=50,delta=60;
    float minrms=52.0*(symfac/64.0);
    int uniques=0,noprint=0,ndecodes_pass=0;
    struct cand {float freq,snr; int shift; float drift,sync;} candidates[200];
    clock_t t0;
    float tfano=0,tcandidates=0,tsync0=0,tsync1=0,tsync2=0,tosd=0;
    fftwf_complex *fftin=ctx->fftin,*fftout=ctx->fftout;
#include "metric_tables.c"
    int mettab[2][256];
    for (i=0;i<256;i++) {
        mettab[0][i]=round(10*(metric_tables[2][i]-bias));
        mettab[1][i]=round(10*(metric_tables[2][255-i]-bias));
    }
    int nffts=4*floor(npoints/512)-1;
    float (*ps)[nffts]=(float (*)[nffts])ctx->spectrum;
    float w[512];
    for (i=0;i<512;i++) w[i]=sin(0.006147931*i);
    for (ipass=0; ipass<npasses; ipass++) {
        if(ipass==1 && ndecodes_pass == 0 && npasses>2) ipass=2;
        if(ipass < 2) {
            nblocksize=1;
            maxdrift=4;
            minsync2=0.12;
        }
        else {
            nblocksize=4;  // try 3 blocksizes plus bitbybit normalization
            maxdrift=0;    // no drift for smaller frequency estimator variance
            minsync2=0.10;
        }
        ndecodes_pass=0;   // still needed?

        for (i=0; i<nffts; i++) {
            for(j=0; j<512; j++ ) {
                k=i*128+j;
                fftin[j][0]=idat[k] * w[j];
                fftin[j][1]=qdat[k] * w[j];
            }
            fftwf_execute(ctx->spectrum_plan);
            for (j=0; j<512; j++ ) {
                k=j+256;
                if( k>511 )
                    k=k-512;
                ps[j][i]=fftout[k][0]*fftout[k][0]+fftout[k][1]*fftout[k][1];
            }
        }

        // Compute average spectrum
        for (i=0; i<512; i++) psavg[i]=0.0;
        for (i=0; i<nffts; i++) {
            for (j=0; j<512; j++) {
                psavg[j]=psavg[j]+ps[j][i];
            }
        }

        // Smooth with 7-point window and limit spectrum to +/-150 Hz
        int window[7]={1,1,1,1,1,1,1};
        float smspec[411];
        for (i=0; i<411; i++) {
            smspec[i]=0.0;
            for(j=-3; j<=3; j++) {
                k=256-205+i+j;
                smspec[i]=smspec[i]+window[j+3]*psavg[k];
            }
        }

        // Sort spectrum values, then pick off noise level as a percentile
        float tmpsort[411];
        for (j=0; j<411; j++) {
            tmpsort[j]=smspec[j];
        }
        qsort(tmpsort, 411, sizeof(float), floatcomp);

        // Noise level of spectrum is estimated as 123/411= 30'th percentile
        float noise_level = tmpsort[122];

        /* Renormalize spectrum so that (large) peaks represent an estimate of snr.
         * We know from experience that threshold snr is near -7dB in wspr bandwidth,
         * corresponding to -7-26.3=-33.3dB in 2500 Hz bandwidth.
         * The corresponding threshold is -42.3 dB in 2500 Hz bandwidth for WSPR-15. */

        float min_snr, snr_scaling_factor;
        min_snr = pow(10.0,-8.0/10.0); //this is min snr in wspr bw
        if( wspr_type == 2 ) {
            snr_scaling_factor=26.3;
        } else {
            snr_scaling_factor=35.3;
        }
        for (j=0; j<411; j++) {
            smspec[j]=smspec[j]/noise_level - 1.0;
            if( smspec[j] < min_snr) smspec[j]=0.1*min_snr;
            continue;
        }

        // Find all local maxima in smoothed spectrum.
        for (i=0; i<200; i++) {
            candidates[i].freq=0.0;
            candidates[i].snr=0.0;
            candidates[i].drift=0.0;
            candidates[i].shift=0;
            candidates[i].sync=0.0;
        }

        int npk=0;
        unsigned char candidate;
        for(j=1; j<410; j++) {
            candidate = (smspec[j]>smspec[j-1]) &&
            (smspec[j]>smspec[j+1]) &&
            (npk<200);
            if ( candidate ) {
                candidates[npk].freq = (j-205)*df;
                candidates[npk].snr  = 10*log10(smspec[j])-snr_scaling_factor;
                npk++;
            }
        }
        if( more_candidates ) {
            for(j=0; j<411; j=j+3) {
                candidate = (smspec[j]>min_snr) && (npk<200);
                if ( candidate ) {
                    candidates[npk].freq = (j-205)*df;
                    candidates[npk].snr  = 10*log10(smspec[j])-snr_scaling_factor;
                    npk++;
                }
            }
        }

        // Compute corrected fmin, fmax, accounting for dial frequency error
        fmin += dialfreq_error;    // dialfreq_error is in units of Hz
        fmax += dialfreq_error;

        // Don't waste time on signals outside of the range [fmin,fmax].
        i=0;
        for( j=0; j<npk; j++) {
            if( candidates[j].freq >= fmin && candidates[j].freq <= fmax ) {
                candidates[i]=candidates[j];
                i++;
            }
        }
        npk=i;

        // bubble sort on snr
        int pass;
        struct cand tmp;
        for (pass = 1; pass <= npk - 1; pass++) {
            for (k = 0; k < npk - pass ; k++) {
                if (candidates[k].snr < candidates[k+1].snr) {
                    tmp = candidates[k];
                    candidates[k]=candidates[k+1];
                    candidates[k+1] = tmp;
                }
            }
        }

        t0=clock();

        /* Make coarse estimates of shift (DT), freq, and drift

         * Look for time offsets up to +/- 8 symbols (about +/- 5.4 s) relative
         to nominal start time, which is 2 seconds into the file

         * Calculates shift relative to the beginning of the file

         * Negative shifts mean that signal started before start of file

         * The program prints DT = shift-2 s

         * Shifts that cause sync vector to fall off of either end of the data
         vector are accommodated by "partial decoding", such that missing
         symbols produce a soft-decision symbol value of 128

         * The frequency drift model is linear, deviation of +/- drift/2 over the
         span of 162 symbols, with deviation equal to 0 at the center of the
         signal vector.
         */

        int idrift,ifr,if0,ifd,k0;
        int kindex;
        float smax,ss,pow,p0,p1,p2,p3;
        for(j=0; j<npk; j++) {                              //For each candidate...
            smax=-1e30;
            if0=candidates[j].freq/df+256;
            for (ifr=if0-2; ifr<=if0+2; ifr++) {                      //Freq search
                for( k0=-10; k0<22; k0++) {                             //Time search
                    for (idrift=-maxdrift; idrift<=maxdrift; idrift++) {  //Drift search
                        ss=0.0;
                        pow=0.0;
                        for (k=0; k<162; k++) {                             //Sum over symbols
                            ifd=ifr+((float)k-81.0)/81.0*( (float)idrift )/(2.0*df);
                            kindex=k0+2*k;
                            if( kindex >= 0 && kindex < nffts ) {
                                p0=ps[ifd-3][kindex];
                                p1=ps[ifd-1][kindex];
                                p2=ps[ifd+1][kindex];
                                p3=ps[ifd+3][kindex];

                                p0=sqrt(p0);
                                p1=sqrt(p1);
                                p2=sqrt(p2);
                                p3=sqrt(p3);

                                ss=ss+(2*pr3[k]-1)*((p1+p3)-(p0+p2));
                                pow=pow+p0+p1+p2+p3;
                            }
                        }
                        sync1=ss/pow;
                        if( sync1 > smax ) {                  //Save coarse parameters
                            smax=sync1;
                            candidates[j].shift=128*(k0+1);
                            candidates[j].drift=idrift;
                            candidates[j].freq=(ifr-256)*df;
                            candidates[j].sync=sync1;
                        }
                    }
                }
            }
        }
        tcandidates += (float)(clock()-t0)/CLOCKS_PER_SEC;

        /*
         Refine the estimates of freq, shift using sync as a metric.
         Sync is calculated such that it is a float taking values in the range
         [0.0,1.0].

         Function sync_and_demodulate has three modes of operation
         mode is the last argument:

         0 = no frequency or drift search. find best time lag.
         1 = no time lag or drift search. find best frequency.
         2 = no frequency or time lag search. Calculate soft-decision
         symbols using passed frequency and shift.

         NB: best possibility for OpenMP may be here: several worker threads
         could each work on one candidate at a time.
         */
        for (j=0; j<npk; j++) {

            f1=candidates[j].freq;
            drift1=candidates[j].drift;
            shift1=candidates[j].shift;
            sync1=candidates[j].sync;

            // coarse-grid lag and freq search, then if sync>minsync1 continue
            fstep=0.0; ifmin=0; ifmax=0;
            lagmin=shift1-128;
            lagmax=shift1+128;
            lagstep=64;
            t0 = clock();
            sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                lagmin, lagmax, lagstep, &drift1, symfac, &sync1, 0);
            tsync0 += (float)(clock()-t0)/CLOCKS_PER_SEC;

            fstep=0.25; ifmin=-2; ifmax=2;
            t0 = clock();
            sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                lagmin, lagmax, lagstep, &drift1, symfac, &sync1, 1);

            if(ipass < 2) {
                // refine drift estimate
                fstep=0.0; ifmin=0; ifmax=0;
                float driftp,driftm,syncp,syncm;
                driftp=drift1+0.5;
                sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                    lagmin, lagmax, lagstep, &driftp, symfac, &syncp, 1);

                driftm=drift1-0.5;
                sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                    lagmin, lagmax, lagstep, &driftm, symfac, &syncm, 1);

                if(syncp>sync1) {
                    drift1=driftp;
                    sync1=syncp;
                } else if (syncm>sync1) {
                    drift1=driftm;
                    sync1=syncm;
                }
            }
            tsync1 += (float)(clock()-t0)/CLOCKS_PER_SEC;

            // fine-grid lag and freq search
            if( sync1 > minsync1 ) {

                lagmin=shift1-32; lagmax=shift1+32; lagstep=16;
                t0 = clock();
                sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                    lagmin, lagmax, lagstep, &drift1, symfac, &sync1, 0);
                tsync0 += (float)(clock()-t0)/CLOCKS_PER_SEC;

                // fine search over frequency
                fstep=0.05; ifmin=-2; ifmax=2;
                t0 = clock();
                sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                    lagmin, lagmax, lagstep, &drift1, symfac, &sync1, 1);
                tsync1 += (float)(clock()-t0)/CLOCKS_PER_SEC;

                candidates[j].freq=f1;
                candidates[j].shift=shift1;
                candidates[j].drift=drift1;
                candidates[j].sync=sync1;
            }
        }

        int nwat=0;
        int idupe;
        for ( j=0; j<npk; j++) {
            idupe=0;
            for (k=0;k<nwat;k++) {
                if( fabsf(candidates[j].freq - candidates[k].freq) < 0.05  &&
                   abs(candidates[j].shift - candidates[k].shift) < 16 ) {
                    idupe=1;
                    break;
                }
            }
            if( idupe == 1 ) {
                if(candidates[j].sync > candidates[k].sync) candidates[k]=candidates[j];
            } else if ( candidates[j].sync > minsync2 ) {
                candidates[nwat]=candidates[j];
                nwat++;
            }
        }

        int idt, ii, jittered_shift;
        float y,sq,rms;
        int ib, blocksize, bitmetric;
        int n1,n2,n3,nadd,nu,ntype;
        int osd_decode;
        for (j=0; j<nwat; j++) {
            memset(symbols,0,sizeof(char)*nbits*2);
            memset(callsign,0,sizeof(char)*13);
            memset(grid,0,sizeof(char)*5);
            memset(call_loc_pow,0,sizeof(char)*23);
            f1=candidates[j].freq;
            shift1=candidates[j].shift;
            drift1=candidates[j].drift;
            not_decoded=1;
            osd_decode=0;

            ib=1;
            while( ib <= nblocksize && not_decoded ) {
                if (ib < 4) { blocksize=ib; bitmetric=0; }
                if (ib == 4) { blocksize=1; bitmetric=1; }

                idt=0; ii=0;
                while ( not_decoded && idt<=(128/iifac)) {
                    ii=(idt+1)/2;
                    if( idt%2 == 1 ) ii=-ii;
                    ii=iifac*ii;
                    jittered_shift=shift1+ii;
                    nhardmin=0; dmin=0.0;

                    // Get soft-decision symbols
                    t0 = clock();
                    noncoherent_sequence_detection(idat, qdat, npoints, symbols, &f1,
                                                   &jittered_shift, &drift1, symfac, &blocksize, &bitmetric);
                    tsync2 += (float)(clock()-t0)/CLOCKS_PER_SEC;

                    sq=0.0;
                    for(i=0; i<162; i++) {
                        y=(float)symbols[i] - 128.0;
                        sq += y*y;
                    }
                    rms=sqrt(sq/162.0);

                    if(rms > minrms) {
                        deinterleave(symbols);
                        t0 = clock();

                        if ( stack ) {
                            not_decoded = jelinek_with_workspace(&metric, &cycles, decdata, symbols, nbits,
                                                  stacksize, stack, mettab,maxcycles,ctx->stack_buckets);
                        } else {
                            not_decoded = fano_with_workspace(&metric,&cycles,&maxnp,decdata,symbols,nbits,
                                               mettab,delta,maxcycles,ctx->fano_nodes);
                        }

                        tfano += (float)(clock()-t0)/CLOCKS_PER_SEC;

                        if( (ndepth >= 0) && not_decoded ) {
                            for(i=0; i<162; i++) {
                                fsymbs[i]=symbols[i]-128.0;
                            }
                            t0 = clock();
                            osdwspr_(fsymbs,apmask,&ndepth,cw,&nhardmin,&dmin);
                            tosd += (float)(clock()-t0)/CLOCKS_PER_SEC;

                            for(i=0; i<162; i++) {
                                symbols[i]=255*cw[i];
                            }
                            fano_with_workspace(&metric,&cycles,&maxnp,decdata,symbols,nbits,
                                 mettab,delta,maxcycles,ctx->fano_nodes);
                            for(i=0; i<11; i++) {
                                if( decdata[i]>127 ) {
                                    message[i]=decdata[i]-256;
                                } else {
                                    message[i]=decdata[i];
                                }
                            }
                            unpack50(message,&n1,&n2);
                            if( !unpackcall(n1,callsign) ) break;
                            callsign[12]=0;
                            if( !unpackgrid(n2, grid) ) break;
                            grid[4]=0;
                            ntype = (n2&127) - 64;
                            int itype;
                            if( (ntype >= 0) && (ntype <= 62) ) {
                                nu = ntype%10;
                                itype=1;
                                if( !(nu == 0 || nu == 3 || nu == 7) ) {
                                    nadd=nu;
                                    if( nu > 3 ) nadd=nu-3;
                                    if( nu > 7 ) nadd=nu-7;
                                    n3=n2/128+32768*(nadd-1);
                                    if( !unpackpfx(n3,callsign) ) {
                                        break;
                                    }
                                    itype=2;
                                }
                                ihash=nhash(callsign,strlen(callsign),(uint32_t)146);
                                if(strncmp(hashtab+ihash*13,callsign,13)==0) {
                                    if( (itype==1 && strncmp(loctab+ihash*5,grid,5)==0) ||
                                        (itype==2) ) {
                                       not_decoded=0;
                                       osd_decode =1;
                                    }
                                }
                            }
                        }

                    }
                    idt++;
                    if( quickmode ) break;
                }
                ib++;
            }

            if( !not_decoded ) {
                ndecodes_pass++;

                for(i=0; i<11; i++) {

                    if( decdata[i]>127 ) {
                        message[i]=decdata[i]-256;
                    } else {
                        message[i]=decdata[i];
                    }

                }

                // Unpack the decoded message, update the hashtable, apply
                // sanity checks on grid and power, and return
                // call_loc_pow string and also callsign (for de-duping).
                decoder_wspr_result interpreted;
                noprint=wsprd_interpret(message,hashtab,loctab,call_loc_pow,callsign,&interpreted);
                if( subtraction && !noprint ) {
                    // With subtraction on, a decode whose printed text no WSPR
                    // encoder makes, most likely a false decode, is neither
                    // subtracted nor printed, as before; the candidates after
                    // it still decode.
                    if( !get_wspr_channel_symbols(call_loc_pow, hashtab, loctab, channel_symbols) ) continue;
                    // Subtract the symbols of the bits decoded, not of the
                    // printed text: an unresolved type 3 call prints as <...>,
                    // whose hash is not the one sent.
                    wspr_channel_symbols_from_data(decdata, channel_symbols);
                    subtract_signal2(idat, qdat, npoints, f1, shift1, drift1, channel_symbols, ctx->subtraction);
                    if(!osd_decode) nhardmin=count_hard_errors(symbols,channel_symbols);
                }

                // Remove dupes (same callsign and freq within 4 Hz)
                int dupe=0;
                for (i=0; i<uniques; i++) {
                    if(!strcmp(callsign,allcalls[i]) &&
                       (fabs(f1-allfreqs[i]) <4.0)) dupe=1;
                }
                if( (verbose || !dupe) && !noprint &&
                    uniques < WSPR_DECODER_MAX_RESULTS ) {
                    strcpy(allcalls[uniques],callsign);
                    allfreqs[uniques]=f1;
                    uniques++;

                    if( wspr_type == 15 ) {
                        freq_print=1500+112.5+f1/8.0;
                        dt_print=shift1*8*dt-1.0;
                    } else {
                        freq_print=1500+f1;
                        dt_print=shift1*dt-1.0;
                    }

                    struct wspr_diagnostic *r = &decodes[uniques-1];
                    decoder_observation *ob = &r->observation;
                    ob->mode = DECODER_MODE_WSPR;
                    ob->variant = wspr_type;
                    ob->snr_db = (int32_t)roundf(candidates[j].snr);
                    ob->frequency_hz = (float)(freq_print - dialfreq_error);
                    ob->dt_seconds = dt_print;
                    ob->sync = candidates[j].sync;
                    memset(ob->message, 0, sizeof ob->message);
                    memcpy(ob->message, call_loc_pow, strlen(call_loc_pow));
                    ob->wspr = interpreted;
                    ob->wspr.snr_db = candidates[j].snr;
                    ob->wspr.drift_hz = wspr_type == 15 ? drift1 / 8.0f : drift1;
                    ob->wspr.waveform_start_seconds = dt_print + 1.0f;
                    strcpy(r->message,call_loc_pow);
                    r->audio_frequency_hz = freq_print;
                    r->drift = drift1;
                    decodes[uniques-1].cycles=cycles;
                    decodes[uniques-1].jitter=ii;
                    decodes[uniques-1].blocksize=blocksize+3*bitmetric;
                    decodes[uniques-1].metric=metric;
                    decodes[uniques-1].nhardmin=nhardmin;
                    decodes[uniques-1].ipass=ipass;
                    decodes[uniques-1].decodetype=osd_decode;
                }
            }
        }

        if (ipass == 0 && residual_callback) residual_callback(idat, qdat, residual_user);
    }

    // sort the result in order of increasing frequency
    struct wspr_diagnostic temp;
    for (j = 1; j <= uniques - 1; j++) {
        for (k = 0; k < uniques - j ; k++) {
            if (decodes[k].audio_frequency_hz > decodes[k+1].audio_frequency_hz) {
                temp = decodes[k];
                decodes[k]=decodes[k+1];;
                decodes[k+1] = temp;
            }
        }
    }

    *count=uniques;
    if (timing) *timing=(struct wspr_timing){tcandidates,tsync0,tsync1,tsync2,tfano,tosd};
    return DECODER_OK;
}

int32_t wspr_decoder_decode(wspr_decoder *ctx, const decoder_wspr_options *options,
                            decoder_observation_callback callback, void *user, int32_t *count)
{
    if (!options || options->effort < 0 || options->effort > 3) return DECODER_INVALID;
    struct wspr_tuning tuning;
    wspr_decoder_default_tuning(&tuning);
    tuning.calibration_error_hz=options->calibration_error_hz;
    if (options->effort == 1) {tuning.quickmode=1; tuning.npasses=2;}
    if (options->effort >= 2) {tuning.maxcycles=500; tuning.ndepth=4;}
    if (options->effort == 3) tuning.more_candidates=1;
    struct wspr_diagnostic results[WSPR_DECODER_MAX_RESULTS];
    int32_t status=wspr_decoder_run(ctx,options,&tuning,results,count,NULL,NULL,NULL);
    if (status == DECODER_OK && callback)
        for (int i=0;i<*count;i++) callback(&results[i].observation,user);
    return status;
}
