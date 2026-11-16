/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WSPR_DEMODULATION_H
#define WSPR_DEMODULATION_H

#include <stddef.h>
#include <stdint.h>

struct wspr_coherent_plan;
struct wspr_noncoherent_tones;

enum wspr_demodulation_kind {
    WSPR_DEMODULATION_NONCOHERENT,
    WSPR_DEMODULATION_COHERENT
};

enum wspr_noncoherent_metric {
    WSPR_MAGNITUDE_DIFFERENCE,
    WSPR_NORMALIZED_MAGNITUDE_DIFFERENCE
};

struct wspr_demodulation_trial {
    enum wspr_demodulation_kind kind;
    int block_symbols;
    enum wspr_noncoherent_metric metric;
};

enum { WSPR_DEMODULATION_TRIAL_COUNT = 5 };

struct wspr_demodulation_input {
    const float *real;
    const float *imag;
    long sample_count;
    const unsigned char *sync;
    const struct wspr_coherent_plan *coherent_plan;
    int metric_scale;
};

struct wspr_demodulation_workspace {
    struct wspr_demodulation_input input;
    size_t timing_count;
    struct wspr_noncoherent_tones *noncoherent;
    uint32_t noncoherent_ready;
    float frequency_hz;
    float drift_hz;
};

const struct wspr_demodulation_trial *wspr_demodulation_trial(size_t index);
int wspr_demodulation_legacy_code(const struct wspr_demodulation_trial *trial);

/* Borrow input and plan storage; allocation failures retain uncached demodulation. */
void wspr_demodulation_init(struct wspr_demodulation_workspace *workspace,
                            const struct wspr_demodulation_input *input,
                            size_t timing_count, size_t trial_count);
void wspr_demodulation_destroy(struct wspr_demodulation_workspace *workspace);

/* Invalidate signal-dependent preparation before changing a candidate or residual. */
void wspr_demodulation_begin_candidate(struct wspr_demodulation_workspace *workspace,
                                       float frequency_hz, float drift_hz);
void wspr_demodulation_end_candidate(struct wspr_demodulation_workspace *workspace);

/* Each timing index must keep the same start sample until the next candidate. */
void wspr_demodulation_soft_metrics(struct wspr_demodulation_workspace *workspace,
                                    const struct wspr_demodulation_trial *trial,
                                    size_t timing_index, int start_sample,
                                    unsigned char soft_metrics[162]);

#endif
