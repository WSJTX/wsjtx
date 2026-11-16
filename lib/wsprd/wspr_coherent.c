/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wspr_coherent.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define PHASE_SEARCH_FFT_SIZE 2048

int wspr_coherent_init(struct wspr_coherent_plan *plan)
{
    fftwf_complex in[PHASE_SEARCH_FFT_SIZE], out[PHASE_SEARCH_FFT_SIZE];
    plan->fft = fftwf_plan_dft_1d(PHASE_SEARCH_FFT_SIZE, in, out, FFTW_FORWARD,
                                  FFTW_ESTIMATE | FFTW_UNALIGNED);
    return plan->fft ? 0 : -1;
}

void wspr_coherent_destroy(struct wspr_coherent_plan *plan)
{
    if (plan->fft) fftwf_destroy_plan(plan->fft);
    plan->fft = NULL;
}

static double find_frequency_peak(fftwf_plan plan,
                                  const double *tone_real, const double *tone_imag,
                                  int first_symbol, int symbol_count,
                                  const double *phase,
                                  double *frequency_cycles_per_symbol,
                                  fftwf_complex *input, fftwf_complex *output)
{
    for (int bin = 0; bin < PHASE_SEARCH_FFT_SIZE; bin++) {
        input[bin][0] = 0.f;
        input[bin][1] = 0.f;
    }
    for (int i = 0; i < symbol_count; i++) {
        double c = cos(-phase[first_symbol+i]), s = sin(-phase[first_symbol+i]);
        input[i][0] = (float)(tone_real[first_symbol+i]*c-tone_imag[first_symbol+i]*s);
        input[i][1] = (float)(tone_real[first_symbol+i]*s+tone_imag[first_symbol+i]*c);
    }
    /* New-array execution keeps the immutable plan safe for parallel callers. */
    fftwf_execute_dft(plan, input, output);
    double best_power = 0;
    int best_bin = 0;
    for (int bin = 0; bin < PHASE_SEARCH_FFT_SIZE; bin++) {
        double power = (double)output[bin][0]*output[bin][0]
                     + (double)output[bin][1]*output[bin][1];
        if (power > best_power) {
            best_power = power;
            best_bin = bin;
        }
    }
    if (frequency_cycles_per_symbol)
        *frequency_cycles_per_symbol =
            (best_bin < PHASE_SEARCH_FFT_SIZE/2 ? (double)best_bin
             : (double)best_bin-PHASE_SEARCH_FFT_SIZE) / PHASE_SEARCH_FFT_SIZE;
    return best_power;
}

static void correlate_symbol_tones(const float *samples_real, const float *samples_imag,
                                    long sample_count, float frequency_hz,
                                    int start_sample, float drift_hz,
                                    struct wspr_phase_corrected_tones *tones)
{
    const double dt=1.0/375.0, df=375.0/256.0;
    const double pi=3.14159265358979323846, twopidt=2*pi*dt;
    const double df15=df*1.5, df05=df*0.5;
    const int nsym=162;
    int i,j,k,t;
    float is[4][162],qs[4][162];
    double cfs[162],sfs[162];
    double c0[257],s0[257],c1[257],s1[257],c2[257],s2[257],c3[257],s3[257];
    double (*Zr)[162] = tones->real;
    double (*Zi)[162] = tones->imag;
    double f0=frequency_hz, drift=drift_hz;
    int lag=start_sample;
    double previous_frequency = 0.0;

    for (i=0; i<nsym; i++) {
        double fp = f0 + (drift/2.0)*((double)i-81.0)/81.0;
        if (i == 0 || fp != previous_frequency) {
            double d0=twopidt*(fp-df15), d1=twopidt*(fp-df05);
            double d2=twopidt*(fp+df05), d3=twopidt*(fp+df15);
            double cd0=cos(d0),sd0=sin(d0),cd1=cos(d1),sd1=sin(d1);
            double cd2=cos(d2),sd2=sin(d2),cd3=cos(d3),sd3=sin(d3);
            c0[0]=1;s0[0]=0;c1[0]=1;s1[0]=0;c2[0]=1;s2[0]=0;c3[0]=1;s3[0]=0;
            for (j=1;j<257;j++){
                c0[j]=c0[j-1]*cd0-s0[j-1]*sd0; s0[j]=c0[j-1]*sd0+s0[j-1]*cd0;
                c1[j]=c1[j-1]*cd1-s1[j-1]*sd1; s1[j]=c1[j-1]*sd1+s1[j-1]*cd1;
                c2[j]=c2[j-1]*cd2-s2[j-1]*sd2; s2[j]=c2[j-1]*sd2+s2[j-1]*cd2;
                c3[j]=c3[j-1]*cd3-s3[j-1]*sd3; s3[j]=c3[j-1]*sd3+s3[j-1]*cd3;
            }
            previous_frequency = fp;
        }
        /* Integer-spaced tones share the same symbol-boundary phase advance. */
        cfs[i]=c0[256]; sfs[i]=s0[256];
        for(t=0;t<4;t++){ is[t][i]=0.0f; qs[t][i]=0.0f; }
        for (j=0;j<256;j++){
            k=lag+i*256+j;
            if(k>0 && k<sample_count){
                float xi=samples_real[k], xq=samples_imag[k];
                is[0][i]+= xi*c0[j]+xq*s0[j];  qs[0][i]+= xq*c0[j]-xi*s0[j];
                is[1][i]+= xi*c1[j]+xq*s1[j];  qs[1][i]+= xq*c1[j]-xi*s1[j];
                is[2][i]+= xi*c2[j]+xq*s2[j];  qs[2][i]+= xq*c2[j]-xi*s2[j];
                is[3][i]+= xi*c3[j]+xq*s3[j];  qs[3][i]+= xq*c3[j]-xi*s3[j];
            }
        }
    }

    /* rotate out the cumulative inter-symbol phase ramp */
    double pr=1.0, pq=0.0;
    for (i=0;i<nsym;i++){
        for(t=0;t<4;t++){
            Zr[t][i]= is[t][i]*pr + qs[t][i]*pq;
            Zi[t][i]= qs[t][i]*pr - is[t][i]*pq;
        }
        double a=pr*cfs[i]-pq*sfs[i], b=pr*sfs[i]+pq*cfs[i];
        double m=sqrt(a*a+b*b); if(m>0){a/=m;b/=m;}
        pr=a; pq=b;
    }
}

static void estimate_residual_phase(const struct wspr_coherent_plan *plan,
                                    const struct wspr_phase_corrected_tones *tones,
                                    const unsigned char sync[WSPR_COHERENT_SYMBOLS],
                                    double phase[WSPR_COHERENT_SYMBOLS])
{
    const double dt=1.0/375.0;
    const double pi=3.14159265358979323846, tsym=256.0*dt;
    const int nsym=162;
    const double (*Zr)[162] = tones->real;
    const double (*Zi)[162] = tones->imag;
    double wr[162],wi[162];
    int i;

    for (i=0;i<nsym;i++){
        int t0=sync[i], t1=sync[i]+2;
        double p0=Zr[t0][i]*Zr[t0][i]+Zi[t0][i]*Zi[t0][i];
        double p1=Zr[t1][i]*Zr[t1][i]+Zi[t1][i]*Zi[t1][i];
        int tb = (p1>p0)? t1 : t0;
        wr[i]=Zr[tb][i]; wi[i]=Zi[tb][i];
    }

    fftwf_complex input[PHASE_SEARCH_FFT_SIZE], output[PHASE_SEARCH_FFT_SIZE];
    double best_drift_hz = 0.0, best_frequency_cycles_per_symbol = 0.0;
    double best_peak_power = -1.0;
    for (int stage = 0; stage < 2; stage++) {
        double drift_min_hz, drift_max_hz, drift_step_hz;
        if (stage == 0) {
            drift_min_hz = -1.2;
            drift_max_hz = 1.2;
            drift_step_hz = 0.01;
        } else {
            drift_min_hz = best_drift_hz-0.012;
            drift_max_hz = best_drift_hz+0.012;
            drift_step_hz = 0.0015;
        }
        for (double drift_hz = drift_min_hz; drift_hz <= drift_max_hz+1e-12;
             drift_hz += drift_step_hz) {
            double accumulated_phase = 0.0;
            for (i = 0; i < nsym; i++) {
                phase[i] = accumulated_phase;
                accumulated_phase += 2.0*pi*tsym*drift_hz
                                   * ((double)i-(double)(nsym-1)/2.0)/(double)nsym;
            }
            double frequency_cycles_per_symbol;
            double peak_power = find_frequency_peak(plan->fft, wr, wi, 0, nsym,
                                                      phase, &frequency_cycles_per_symbol,
                                                      input, output);
            if (peak_power > best_peak_power) {
                best_peak_power = peak_power;
                best_drift_hz = drift_hz;
                best_frequency_cycles_per_symbol = frequency_cycles_per_symbol;
            }
        }
    }
    double accumulated_phase = 0.0;
    for (i = 0; i < nsym; i++) {
        phase[i] = accumulated_phase+2.0*pi*best_frequency_cycles_per_symbol*i;
        accumulated_phase += 2.0*pi*tsym*best_drift_hz
                           * ((double)i-(double)(nsym-1)/2.0)/(double)nsym;
    }
}

static void apply_residual_phase(struct wspr_phase_corrected_tones *tones,
                                 const double phase[WSPR_COHERENT_SYMBOLS])
{
    const int nsym=162;
    double (*Zr)[162] = tones->real;
    double (*Zi)[162] = tones->imag;
    int i,t;

    for(i=0;i<nsym;i++){
        double c=cos(-phase[i]), s=sin(-phase[i]);
        for(t=0;t<4;t++){
            double a=Zr[t][i]*c-Zi[t][i]*s, b=Zr[t][i]*s+Zi[t][i]*c;
            Zr[t][i]=a; Zi[t][i]=b;
        }
    }
}

void wspr_coherent_prepare_tones(const struct wspr_coherent_plan *plan,
                                 const float *samples_real, const float *samples_imag,
                                 long sample_count, float frequency_hz,
                                 int start_sample, float drift_hz,
                                 const unsigned char sync[WSPR_COHERENT_SYMBOLS],
                                 struct wspr_phase_corrected_tones *tones)
{
    double phase[WSPR_COHERENT_SYMBOLS];
    correlate_symbol_tones(samples_real, samples_imag, sample_count, frequency_hz,
                            start_sample, drift_hz, tones);
    estimate_residual_phase(plan, tones, sync, phase);
    apply_residual_phase(tones, phase);
}

void wspr_coherent_soft_metrics(const struct wspr_phase_corrected_tones *tones,
                           const unsigned char sync[WSPR_COHERENT_SYMBOLS],
                           int metric_scale,
                           unsigned char soft_metrics[WSPR_COHERENT_SYMBOLS])
{
    const int nsym = WSPR_COHERENT_SYMBOLS;
    const double (*Zr)[162] = tones->real;
    const double (*Zi)[162] = tones->imag;
    double wr[162], wi[162];
    int i;
    /* Exclude each symbol from its own phase reference to avoid fitting its
       noise; uncertain tentative decisions receive less channel-estimation weight. */
    double fsymb[162], csr[163], csi[163];
    int half = nsym/2;
    for(int iter=0; iter<2; iter++){
        if(iter>0){
            for(i=0;i<nsym;i++){
                int t0=sync[i], t1=sync[i]+2;
                int tb = (fsymb[i]>0.0)? t1 : t0;
                double a0=sqrt(Zr[t0][i]*Zr[t0][i]+Zi[t0][i]*Zi[t0][i]);
                double a1=sqrt(Zr[t1][i]*Zr[t1][i]+Zi[t1][i]*Zi[t1][i]);
                double rel=fabs(a1-a0)/(a1+a0+1e-30);
                wr[i]=Zr[tb][i]*rel; wi[i]=Zi[tb][i]*rel;
            }
        } else {
            for(i=0;i<nsym;i++){
                int t0=sync[i], t1=sync[i]+2;
                double a0=sqrt(Zr[t0][i]*Zr[t0][i]+Zi[t0][i]*Zi[t0][i]);
                double a1=sqrt(Zr[t1][i]*Zr[t1][i]+Zi[t1][i]*Zi[t1][i]);
                int tb = (a1>a0)? t1 : t0;
                double rel=fabs(a1-a0)/(a1+a0+1e-30);
                wr[i]=Zr[tb][i]*rel; wi[i]=Zi[tb][i]*rel;
            }
        }
        csr[0]=0; csi[0]=0;
        for(i=0;i<nsym;i++){ csr[i+1]=csr[i]+wr[i]; csi[i+1]=csi[i]+wi[i]; }
        for(i=0;i<nsym;i++){
            int a=i-half, b=i+half;
            if(a<0){ b-=a; a=0; }
            if(b>nsym-1){ a-=(b-(nsym-1)); b=nsym-1; if(a<0)a=0; }
            double hr=csr[b+1]-csr[a]-wr[i];      /* leave this symbol out */
            double hi=csi[b+1]-csi[a]-wi[i];
            double hm=sqrt(hr*hr+hi*hi);
            if(hm<1e-20){ fsymb[i]=0.0; continue; }
            hr/=hm; hi/=hm;
            int t0=sync[i], t1=sync[i]+2;
            double r1=Zr[t1][i]*hr+Zi[t1][i]*hi;   /* Re{ z . conj(h) } */
            double r0=Zr[t0][i]*hr+Zi[t0][i]*hi;
            fsymb[i]=r1-r0;
        }
    }

    double fsum=0.0,f2sum=0.0;
    for(i=0;i<nsym;i++){ fsum+=fsymb[i]/nsym; f2sum+=fsymb[i]*fsymb[i]/nsym; }
    double variance=f2sum-fsum*fsum;
    /* Constant metrics can leave roundoff-sized variance after cancellation. */
    double fac=variance>256.0*DBL_EPSILON*f2sum ? sqrt(variance) : 1.0;
    for(i=0;i<nsym;i++){
        double v=metric_scale*fsymb[i]/fac;
        if(v>127.0) v=127.0;
        if(v<-128.0) v=-128.0;
        soft_metrics[i]=(unsigned char)(v+128.5);
    }
}
