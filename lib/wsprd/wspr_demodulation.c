/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wspr_demodulation.h"

#include <stdlib.h>
#include <string.h>

#include "wspr_coherent.h"
#include "wspr_noncoherent.h"

static const struct wspr_demodulation_trial trials[] = {
    {WSPR_DEMODULATION_NONCOHERENT, 1, WSPR_MAGNITUDE_DIFFERENCE},
    {WSPR_DEMODULATION_NONCOHERENT, 2, WSPR_MAGNITUDE_DIFFERENCE},
    {WSPR_DEMODULATION_NONCOHERENT, 3, WSPR_MAGNITUDE_DIFFERENCE},
    {WSPR_DEMODULATION_NONCOHERENT, 1, WSPR_NORMALIZED_MAGNITUDE_DIFFERENCE},
    {WSPR_DEMODULATION_COHERENT, 0, WSPR_MAGNITUDE_DIFFERENCE}
};

const struct wspr_demodulation_trial *wspr_demodulation_trial(size_t index)
{
    return index < WSPR_DEMODULATION_TRIAL_COUNT ? &trials[index] : NULL;
}

int wspr_demodulation_legacy_code(const struct wspr_demodulation_trial *trial)
{
    return trial->kind == WSPR_DEMODULATION_COHERENT
        ? -WSPR_COHERENT_SYMBOLS
        : trial->block_symbols + 3 * (trial->metric == WSPR_NORMALIZED_MAGNITUDE_DIFFERENCE);
}

void wspr_demodulation_init(struct wspr_demodulation_workspace *workspace,
                            const struct wspr_demodulation_input *input,
                            size_t timing_count, size_t trial_count)
{
    memset(workspace, 0, sizeof *workspace);
    workspace->input = *input;
    workspace->timing_count = timing_count;
    if (trial_count > 1)
        workspace->noncoherent = malloc(timing_count * sizeof *workspace->noncoherent);
}

void wspr_demodulation_destroy(struct wspr_demodulation_workspace *workspace)
{
    free(workspace->noncoherent);
    memset(workspace, 0, sizeof *workspace);
}

void wspr_demodulation_begin_candidate(struct wspr_demodulation_workspace *workspace,
                                       float frequency_hz, float drift_hz)
{
    wspr_demodulation_end_candidate(workspace);
    workspace->frequency_hz = frequency_hz;
    workspace->drift_hz = drift_hz;
}

void wspr_demodulation_end_candidate(struct wspr_demodulation_workspace *workspace)
{
    workspace->noncoherent_ready = 0;
}

void wspr_demodulation_soft_metrics(struct wspr_demodulation_workspace *workspace,
                                    const struct wspr_demodulation_trial *trial,
                                    size_t timing_index, int start_sample,
                                    unsigned char soft_metrics[162])
{
    const struct wspr_demodulation_input *input = &workspace->input;
    if (trial->kind == WSPR_DEMODULATION_NONCOHERENT) {
        struct wspr_noncoherent_tones scratch;
        int cached = workspace->noncoherent && timing_index < workspace->timing_count &&
                     timing_index < 32;
        struct wspr_noncoherent_tones *tones = cached
            ? workspace->noncoherent + timing_index : &scratch;
        uint32_t mask = cached ? UINT32_C(1) << timing_index : 0;
        if (!cached || !(workspace->noncoherent_ready & mask)) {
            wspr_noncoherent_prepare_tones(input->real, input->imag, input->sample_count,
                                            workspace->frequency_hz, start_sample,
                                            workspace->drift_hz, tones);
            workspace->noncoherent_ready |= mask;
        }
        wspr_noncoherent_soft_metrics(tones, input->sync, input->metric_scale,
                                       trial->block_symbols,
                                       trial->metric == WSPR_NORMALIZED_MAGNITUDE_DIFFERENCE,
                                       soft_metrics);
    } else {
        struct wspr_phase_corrected_tones tones;
        wspr_coherent_prepare_tones(input->coherent_plan, input->real, input->imag,
                                     input->sample_count, workspace->frequency_hz,
                                     start_sample, workspace->drift_hz, input->sync, &tones);
        wspr_coherent_soft_metrics(&tones, input->sync, input->metric_scale, soft_metrics);
    }
}
