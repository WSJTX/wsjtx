/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2001-2018 Joe Taylor, K1JT
 * Copyright 2014-2018 Steven Franke, K9AN
 */
#include <math.h>
#include <string.h>

#include "wspr_noncoherent.h"

void wspr_noncoherent_prepare_tones(const float *samples_real, const float *samples_imag, long sample_count,
                              float frequency_hz, int start_sample, float drift_hz,
                              struct wspr_noncoherent_tones *tones)
{
    float previous_frequency = 0.0f;
    static const float dt=1.0/375.0, df=375.0/256.0;
    static const float pi=3.14159265358979323846;
    float twopidt, df15=df*1.5, df05=df*0.5;
    int i, j, k, lag=start_sample;
    float is[4][162],qs[4][162],cf[4][162],sf[4][162];
    float c0[257],s0[257],c1[257],s1[257],c2[257],s2[257],c3[257],s3[257];
    float dphi0, cdphi0, sdphi0, dphi1, cdphi1, sdphi1, dphi2, cdphi2, sdphi2,
          dphi3, cdphi3, sdphi3;
    float f0=frequency_hz, fp;
    twopidt=2*pi*dt;

    for (i=0; i<162; i++) {
        fp = f0 + (drift_hz/2.0)*((float)i-81.0)/81.0;
        if (i == 0 || fp != previous_frequency) {
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

            previous_frequency = fp;
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
            if( (k>0) && (k<sample_count) ) {
                is[0][i]=is[0][i] + samples_real[k]*c0[j] + samples_imag[k]*s0[j];
                qs[0][i]=qs[0][i] - samples_real[k]*s0[j] + samples_imag[k]*c0[j];
                is[1][i]=is[1][i] + samples_real[k]*c1[j] + samples_imag[k]*s1[j];
                qs[1][i]=qs[1][i] - samples_real[k]*s1[j] + samples_imag[k]*c1[j];
                is[2][i]=is[2][i] + samples_real[k]*c2[j] + samples_imag[k]*s2[j];
                qs[2][i]=qs[2][i] - samples_real[k]*s2[j] + samples_imag[k]*c2[j];
                is[3][i]=is[3][i] + samples_real[k]*c3[j] + samples_imag[k]*s3[j];
                qs[3][i]=qs[3][i] - samples_real[k]*s3[j] + samples_imag[k]*c3[j];
            }
        }
    }

    memcpy(tones->real, is, sizeof is);
    memcpy(tones->imag, qs, sizeof qs);
    memcpy(tones->phase_real, cf, sizeof cf);
    memcpy(tones->phase_imag, sf, sizeof sf);
}

void wspr_noncoherent_soft_metrics(const struct wspr_noncoherent_tones *tones,
                              const unsigned char sync[162], int metric_scale,
                              int block_symbols, int normalized_metric, unsigned char soft_metrics[162])
{
    int i, j, ib, b, itone, imask, nseq=1<<block_symbols;
    float xi[512],xq[512],p[512];
    float cm,sm,cmp,smp,fac,xm1,xm0;
    float fsum=0.0, f2sum=0.0, fsymb[162];

    for (i=0; i<162; i=i+block_symbols) {
        for (j=0;j<nseq;j++) {
            xi[j]=0.0; xq[j]=0.0;
            cm=1; sm=0;
            for (ib=0; ib<block_symbols; ib++) {
                b=(j&(1<<(block_symbols-1-ib)))>>(block_symbols-1-ib);
                itone=sync[i+ib]+2*b;
                xi[j]=xi[j]+tones->real[itone][i+ib]*cm + tones->imag[itone][i+ib]*sm;
                xq[j]=xq[j]+tones->imag[itone][i+ib]*cm - tones->real[itone][i+ib]*sm;
                cmp=tones->phase_real[itone][i+ib]*cm - tones->phase_imag[itone][i+ib]*sm;
                smp=tones->phase_imag[itone][i+ib]*cm + tones->phase_real[itone][i+ib]*sm;
                cm=cmp; sm=smp;
            }
            p[j]=xi[j]*xi[j]+xq[j]*xq[j];
            p[j]=sqrt(p[j]);
        }
        for (ib=0; ib<block_symbols; ib++) {
            imask=1<<(block_symbols-1-ib);
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
            if( normalized_metric == 1 ) {
                fsymb[i+ib]=fsymb[i+ib]/(xm1 > xm0 ? xm1 : xm0);
            }
        }
    }
    for (i=0; i<162; i++) {
        fsum=fsum+fsymb[i]/162.0;
        f2sum=f2sum+fsymb[i]*fsymb[i]/162.0;
    }
    fac=sqrt(f2sum-fsum*fsum);
    for (i=0; i<162; i++) {
        fsymb[i]=metric_scale*fsymb[i]/fac;
        if( fsymb[i] > 127) fsymb[i]=127.0;
        if( fsymb[i] < -128 ) fsymb[i]=-128.0;
        soft_metrics[i]=fsymb[i] + 128;
    }
    return;
}
