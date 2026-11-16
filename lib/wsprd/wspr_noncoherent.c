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

static void sequence_powers(const struct wspr_noncoherent_tones *tones,
                            const unsigned char sync[162], int first, int count,
                            float *power)
{
    for (int sequence = 0; sequence < (1 << count); ++sequence) {
        float real = 0, imag = 0, phase_real = 1, phase_imag = 0;
        for (int bit = 0; bit < count; ++bit) {
            int symbol = first + bit;
            int tone = sync[symbol] + 2 * ((sequence >> (count - 1 - bit)) & 1);
            real = real + tones->real[tone][symbol] * phase_real + tones->imag[tone][symbol] * phase_imag;
            imag = imag + tones->imag[tone][symbol] * phase_real - tones->real[tone][symbol] * phase_imag;
            float next_real = tones->phase_real[tone][symbol] * phase_real
                            - tones->phase_imag[tone][symbol] * phase_imag;
            float next_imag = tones->phase_imag[tone][symbol] * phase_real
                            + tones->phase_real[tone][symbol] * phase_imag;
            phase_real = next_real;
            phase_imag = next_imag;
        }
        power[sequence] = real * real + imag * imag;
    }
}

static void scale_metrics(float metrics[162], int metric_scale, unsigned char soft_metrics[162])
{
    float mean = 0, second_moment = 0;
    for (int i = 0; i < 162; ++i) {
        mean += metrics[i] / 162.0;
        second_moment += metrics[i] * metrics[i] / 162.0;
    }
    float variance = second_moment - mean * mean;
    if (!(variance > 0) || !isfinite(variance)) {
        memset(soft_metrics, 128, 162);
        return;
    }
    float scale = sqrt(variance);
    for (int i = 0; i < 162; ++i) {
        float value = metric_scale * metrics[i] / scale;
        if (value > 127) value = 127;
        if (value < -128) value = -128;
        soft_metrics[i] = value + 128;
    }
}

void wspr_noncoherent_soft_metrics(const struct wspr_noncoherent_tones *tones,
                                 const unsigned char sync[162], int metric_scale,
                                 int block_symbols, int normalized_metric,
                                 unsigned char soft_metrics[162])
{
    float power[512], metrics[162];
    int sequences = 1 << block_symbols;
    for (int first = 0; first < 162; first += block_symbols) {
        sequence_powers(tones, sync, first, block_symbols, power);
        for (int sequence = 0; sequence < sequences; ++sequence)
            power[sequence] = sqrt(power[sequence]);
        for (int bit = 0; bit < block_symbols; ++bit) {
            int mask = 1 << (block_symbols - 1 - bit);
            float maximum[2] = {0, 0};
            for (int sequence = 0; sequence < sequences; ++sequence) {
                int value = (sequence & mask) != 0;
                if (power[sequence] > maximum[value]) maximum[value] = power[sequence];
            }
            metrics[first + bit] = maximum[1] - maximum[0];
            if (normalized_metric) {
                float largest = maximum[1] > maximum[0] ? maximum[1] : maximum[0];
                metrics[first + bit] = largest > 0 ? metrics[first + bit] / largest : 0;
            }
        }
    }
    scale_metrics(metrics, metric_scale, soft_metrics);
}

void wspr_fading_soft_metrics(const struct wspr_noncoherent_tones *tones,
                             const unsigned char sync[162], int metric_scale,
                             unsigned char soft_metrics[162])
{
    enum { window = 7, sequences = 1 << window };
    /* K = I - (I + 1.5 R)^-1 for Gaussian channel correlation R at 256/375 s.
       A 0.2 Hz 2-sigma spread allows for quiet fading and residual frequency error. */
    static const double kernel[window][window] = {
        {0.40975263571,0.270246790252,0.111340858874,0.00453218200744,-0.0294107380489,-0.0178243133924,0.00209882624111},
        {0.270246790252,0.286026674379,0.2192056138,0.109161203789,0.0180141055206,-0.0208539042457,-0.0178243133924},
        {0.111340858874,0.2192056138,0.265562236183,0.21923883576,0.114572212792,0.0180141055206,-0.0294107380489},
        {0.00453218200744,0.109161203789,0.21923883576,0.266992908965,0.21923883576,0.109161203789,0.00453218200744},
        {-0.0294107380489,0.0180141055206,0.114572212792,0.21923883576,0.265562236183,0.2192056138,0.111340858874},
        {-0.0178243133924,-0.0208539042457,0.0180141055206,0.109161203789,0.2192056138,0.286026674379,0.270246790252},
        {0.00209882624111,-0.0178243133924,-0.0294107380489,0.00453218200744,0.111340858874,0.270246790252,0.40975263571}
    };
    double noise = 0;
    for (int symbol = 0; symbol < 162; ++symbol) {
        for (int tone = 1 - sync[symbol]; tone < 4; tone += 2)
            noise += tones->real[tone][symbol] * tones->real[tone][symbol]
                   + tones->imag[tone][symbol] * tones->imag[tone][symbol];
    }
    noise /= 324;
    if (!(noise > 0) || !isfinite(noise)) {
        memset(soft_metrics, 128, 162);
        return;
    }

    float metrics[162];
    for (int symbol = 0; symbol < 162; ++symbol) {
        int first = symbol - window / 2;
        if (first < 0) first = 0;
        if (first > 162 - window) first = 162 - window;
        int mask = 1 << (window - 1 - (symbol - first));
        double tone_real[window][2], tone_imag[window][2];
        double phase_real = 1, phase_imag = 0;
        for (int offset = 0; offset < window; ++offset) {
            for (int bit = 0; bit < 2; ++bit) {
                int tone = sync[first + offset] + 2 * bit;
                tone_real[offset][bit] = tones->real[tone][first + offset] * phase_real
                                       + tones->imag[tone][first + offset] * phase_imag;
                tone_imag[offset][bit] = tones->imag[tone][first + offset] * phase_real
                                       - tones->real[tone][first + offset] * phase_imag;
            }
            /* Integer tone spacing makes boundary phase independent of the data bit. */
            double next_real = phase_real * tones->phase_real[0][first + offset]
                             - phase_imag * tones->phase_imag[0][first + offset];
            phase_imag = phase_imag * tones->phase_real[0][first + offset]
                       + phase_real * tones->phase_imag[0][first + offset];
            phase_real = next_real;
        }

        double real[window], imag[window], projected_real[window] = {0}, projected_imag[window] = {0};
        for (int i = 0; i < window; ++i) {
            real[i] = tone_real[i][0];
            imag[i] = tone_imag[i][0];
        }
        for (int i = 0; i < window; ++i) {
            for (int j = 0; j < window; ++j) {
                projected_real[i] += kernel[i][j] * real[j];
                projected_imag[i] += kernel[i][j] * imag[j];
            }
        }
        double power = 0;
        for (int i = 0; i < window; ++i)
            power += real[i] * projected_real[i] + imag[i] * projected_imag[i];

        double score[sequences], maximum[2] = {-INFINITY, -INFINITY};
        int previous = 0;
        for (int index = 0; index < sequences; ++index) {
            int sequence = index ^ (index >> 1);
            if (index) {
                /* Gray order changes one tone, so updating Kz costs only one column. */
                int changed = sequence ^ previous, bit = 0;
                while (!(changed & (1 << bit))) ++bit;
                int offset = window - 1 - bit, value = (sequence >> bit) & 1;
                double delta_real = tone_real[offset][value] - real[offset];
                double delta_imag = tone_imag[offset][value] - imag[offset];
                power += 2 * (delta_real * projected_real[offset] + delta_imag * projected_imag[offset])
                       + kernel[offset][offset] * (delta_real * delta_real + delta_imag * delta_imag);
                for (int i = 0; i < window; ++i) {
                    projected_real[i] += kernel[i][offset] * delta_real;
                    projected_imag[i] += kernel[i][offset] * delta_imag;
                }
                real[offset] = tone_real[offset][value];
                imag[offset] = tone_imag[offset][value];
            }
            score[sequence] = power / noise;
            int value = (sequence & mask) != 0;
            if (score[sequence] > maximum[value]) maximum[value] = score[sequence];
            previous = sequence;
        }
        double probability[2] = {0, 0};
        for (int sequence = 0; sequence < sequences; ++sequence) {
            int value = (sequence & mask) != 0;
            probability[value] += exp(score[sequence] - maximum[value]);
        }
        metrics[symbol] = maximum[1] - maximum[0] + log(probability[1]) - log(probability[0]);
    }
    scale_metrics(metrics, metric_scale, soft_metrics);
}
