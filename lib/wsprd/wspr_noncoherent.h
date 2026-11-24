/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WSPR_NONCOHERENT_H
#define WSPR_NONCOHERENT_H

struct wspr_noncoherent_tones {
    float real[4][162];
    float imag[4][162];
    float phase_real[4][162];
    float phase_imag[4][162];
};

/* Prepare one timing/frequency/drift hypothesis on an unchanged residual. */
void wspr_noncoherent_prepare_tones(const float *samples_real, const float *samples_imag,
                                    long sample_count,
                                    float frequency_hz, int start_sample, float drift_hz,
                                    struct wspr_noncoherent_tones *tones);

/* Block lengths 1, 2, 3, 6 and 9 divide the 162-symbol transmission. */
void wspr_noncoherent_soft_metrics(const struct wspr_noncoherent_tones *tones,
                                   const unsigned char sync[162], int metric_scale,
                                   int block_symbols, int normalized_metric,
                                   unsigned char soft_metrics[162]);

/* Seven-symbol overlapping likelihoods model locally correlated fading. */
void wspr_fading_soft_metrics(const struct wspr_noncoherent_tones *tones,
                             const unsigned char sync[162], int metric_scale,
                             unsigned char soft_metrics[162]);

#endif
