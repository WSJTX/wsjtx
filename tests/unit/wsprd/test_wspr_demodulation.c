/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/wsprd/wspr_noncoherent.h"
#include "lib/wsprd/wspr_demodulation.h"

static int failures;
static int deny_allocations;
static size_t allocation_attempts;
static size_t noncoherent_preparations;

static void *workspace_malloc(size_t bytes)
{
    ++allocation_attempts;
    return deny_allocations ? NULL : malloc(bytes);
}

static void counted_noncoherent_prepare(const float *real, const float *imag,
                                         long sample_count, float frequency_hz,
                                         int start_sample, float drift_hz,
                                         struct wspr_noncoherent_tones *tones)
{
    ++noncoherent_preparations;
    wspr_noncoherent_prepare_tones(real, imag, sample_count, frequency_hz,
                                    start_sample, drift_hz, tones);
}

/* Interpose only the workspace's allocation and preparation calls. */
#define malloc workspace_malloc
#define wspr_noncoherent_prepare_tones counted_noncoherent_prepare
#include "lib/wsprd/wspr_demodulation.c"
#undef wspr_noncoherent_prepare_tones
#undef malloc

static void expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void make_input(float *real, float *imag, unsigned char sync[162])
{
    uint32_t state = UINT32_C(0x351b804d);
    for (int symbol = 0; symbol < 162; ++symbol)
        sync[symbol] = next_random(&state) & 1;
    for (long sample = 0; sample < 45000; ++sample) {
        real[sample] = ((int)(next_random(&state) >> 16) - 32768) / 32768.0f;
        imag[sample] = ((int)(next_random(&state) >> 16) - 32768) / 32768.0f;
        real[sample] += 0.3 * cos(0.027 * sample);
        imag[sample] += 0.3 * sin(0.027 * sample);
    }
}

static void direct_metrics(const struct wspr_demodulation_input *input,
                            const struct wspr_demodulation_trial *trial,
                            float frequency_hz, int start_sample, float drift_hz,
                            unsigned char soft_metrics[162])
{
    struct wspr_noncoherent_tones tones;
    wspr_noncoherent_prepare_tones(input->real, input->imag, input->sample_count,
                                    frequency_hz, start_sample, drift_hz, &tones);
    if (trial->kind == WSPR_DEMODULATION_FADING)
        wspr_fading_soft_metrics(&tones, input->sync, input->metric_scale, soft_metrics);
    else
        wspr_noncoherent_soft_metrics(&tones, input->sync, input->metric_scale,
                                       trial->block_symbols,
                                       trial->metric == WSPR_NORMALIZED_MAGNITUDE_DIFFERENCE,
                                       soft_metrics);
}

static void compare_candidate(struct wspr_demodulation_workspace *workspace,
                               size_t trial_count, float frequency_hz,
                               int start_sample, float drift_hz)
{
    unsigned char actual[162], expected[162];
    size_t before_noncoherent = noncoherent_preparations;
    wspr_demodulation_begin_candidate(workspace, frequency_hz, drift_hz);
    for (size_t trial_index = 0; trial_index < trial_count; ++trial_index) {
        const struct wspr_demodulation_trial *trial = wspr_demodulation_trial(trial_index);
        for (size_t timing = 0; timing < 2; ++timing) {
            int shifted_start = start_sample - (int)timing * 8;
            wspr_demodulation_soft_metrics(workspace, trial, timing, shifted_start, actual);
            direct_metrics(&workspace->input, trial, frequency_hz, shifted_start,
                             drift_hz, expected);
            expect(!memcmp(actual, expected, sizeof actual),
                   "workspace and independent preparation produce identical soft bytes");
        }
    }
    expect(noncoherent_preparations - before_noncoherent ==
           (deny_allocations ? 2 * trial_count : 2),
           "noncoherent preparation is shared only while storage is available");
    wspr_demodulation_end_candidate(workspace);
}

static void test_workspace(int fail_allocations,
                            size_t trial_count)
{
    float *real = malloc(45000 * sizeof *real);
    float *imag = malloc(45000 * sizeof *imag);
    unsigned char sync[162];
    if (!real || !imag) exit(2);
    make_input(real, imag, sync);
    struct wspr_demodulation_input input = {real, imag, 45000, sync, 50};
    struct wspr_demodulation_workspace workspace;
    deny_allocations = fail_allocations;
    allocation_attempts = 0;
    wspr_demodulation_init(&workspace, &input, 2, trial_count);
    compare_candidate(&workspace, trial_count, 37.625f, 375, 0.0f);
    compare_candidate(&workspace, trial_count, -13.125f, -17, 2.25f);
    for (long sample = 0; sample < 45000; ++sample) {
        real[sample] += 0.2 * sin(0.051 * sample);
        imag[sample] -= 0.2 * cos(0.051 * sample);
    }
    compare_candidate(&workspace, trial_count, -13.125f, -17, 2.25f);
    if (fail_allocations)
        expect(allocation_attempts == 1,
               "failed allocation is attempted once per storage lifetime");
    wspr_demodulation_destroy(&workspace);
    deny_allocations = 0;
    free(real);
    free(imag);
}

int main(void)
{
    test_workspace(0, 4);
    test_workspace(0, 5);
    test_workspace(1, 4);
    test_workspace(1, 5);
    return failures != 0;
}
