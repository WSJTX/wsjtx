/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WSPR_COHERENT_H
#define WSPR_COHERENT_H

#include <fftw3.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WSPR_COHERENT_SYMBOLS 162

struct wspr_coherent_plan {
    fftwf_plan fft;
};

struct wspr_phase_corrected_tones {
    double real[4][WSPR_COHERENT_SYMBOLS];
    double imag[4][WSPR_COHERENT_SYMBOLS];
};

/* Initialize before workers start; execution uses caller-local FFT buffers. */
int wspr_coherent_init(struct wspr_coherent_plan *plan);
void wspr_coherent_destroy(struct wspr_coherent_plan *plan);

/* Prepare phase-corrected tones for one 375 Hz hypothesis with binary sync. */
void wspr_coherent_prepare_tones(const struct wspr_coherent_plan *plan,
                                 const float *samples_real, const float *samples_imag,
                                 long sample_count, float frequency_hz,
                                 int start_sample, float drift_hz,
                                 const unsigned char sync[WSPR_COHERENT_SYMBOLS],
                                 struct wspr_phase_corrected_tones *tones);

/* Estimate each symbol against the other symbols in the transmission. */
void wspr_coherent_soft_metrics(const struct wspr_phase_corrected_tones *tones,
                                const unsigned char sync[WSPR_COHERENT_SYMBOLS],
                                int metric_scale,
                                unsigned char soft_metrics[WSPR_COHERENT_SYMBOLS]);

#ifdef __cplusplus
}
#endif

#endif
