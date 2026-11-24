#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/wsprd/wspr_noncoherent.h"

unsigned char pr3[162];


static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void make_input(float *real, float *imag, long count, float frequency,
                        int shift, float drift, float amplitude,
                        unsigned char bits[162])
{
    const double pi = 3.14159265358979323846;
    double phase = 0.37;
    uint32_t random = 0x173abcd;
    for (int i = 0; i < 162; i++) {
        bits[i] = next_random(&random) & 1;
        pr3[i] = next_random(&random) & 1;
    }
    for (long k = 0; k < count; k++) {
        real[k] = ((int)(next_random(&random) >> 16) - 32768) / 262144.0f;
        imag[k] = ((int)(next_random(&random) >> 16) - 32768) / 262144.0f;
        long relative = k - shift;
        int symbol = relative / 256;
        if (relative < 0 || symbol >= 162) continue;
        int tone = pr3[symbol] + 2 * bits[symbol];
        double fp = frequency + (drift / 2.0) * (symbol - 81.0) / 81.0;
        real[k] += amplitude * cos(phase);
        imag[k] += amplitude * sin(phase);
        phase += 2.0 * pi * (fp + (tone - 1.5) * 375.0 / 256.0) / 375.0;
    }
}


static void fading_reference_kernel(double kernel[7][7])
{
    const double pi = acos(-1.0);
    double augmented[7][14] = {{0}};
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 7; ++col) {
            double lag = (row - col) * (256.0 / 375.0);
            double covariance = exp(-2.0 * pi * pi * 0.1 * 0.1 * lag * lag);
            augmented[row][col] = (row == col) + 1.5 * covariance;
        }
        augmented[row][7 + row] = 1.0;
    }
    /* I + 1.5 R is positive definite, so these elimination pivots are positive. */
    for (int pivot = 0; pivot < 7; ++pivot) {
        double diagonal = augmented[pivot][pivot];
        for (int col = 0; col < 14; ++col) augmented[pivot][col] /= diagonal;
        for (int row = 0; row < 7; ++row) {
            if (row == pivot) continue;
            double factor = augmented[row][pivot];
            for (int col = 0; col < 14; ++col)
                augmented[row][col] -= factor * augmented[pivot][col];
        }
    }
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 7; ++col)
            kernel[row][col] = (row == col) - augmented[row][7 + col];
}

static int fading_reference(const struct wspr_noncoherent_tones *tones,
                            const unsigned char sync[162], float metric_scale,
                            unsigned char expected[162])
{
    double kernel[7][7], noise = 0.0, metrics[162];
    fading_reference_kernel(kernel);
    for (int symbol = 0; symbol < 162; ++symbol) {
        for (int tone = 0; tone < 4; ++tone) {
            if (tone % 2 == sync[symbol]) continue;
            double real = tones->real[tone][symbol];
            double imag = tones->imag[tone][symbol];
            noise += real * real + imag * imag;
        }
    }
    noise /= 324.0;
    for (int symbol = 0; symbol < 162; ++symbol) {
        int first = symbol < 3 ? 0 : symbol > 158 ? 155 : symbol - 3;
        double score[128], maximum[2] = {-INFINITY, -INFINITY};
        for (int sequence = 0; sequence < 128; ++sequence) {
            double real[7], imag[7], phase = 0.0;
            for (int offset = 0; offset < 7; ++offset) {
                int bit = (sequence >> offset) & 1;
                int index = first + offset;
                int tone = sync[index] + 2 * bit;
                real[offset] = tones->real[tone][index] * cos(phase)
                             + tones->imag[tone][index] * sin(phase);
                imag[offset] = tones->imag[tone][index] * cos(phase)
                             - tones->real[tone][index] * sin(phase);
                phase += atan2(tones->phase_imag[0][index],
                               tones->phase_real[0][index]);
            }
            double quadratic = 0.0;
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 7; ++col)
                    quadratic += kernel[row][col]
                               * (real[row] * real[col] + imag[row] * imag[col]);
            score[sequence] = quadratic / noise;
            int bit = (sequence >> (symbol - first)) & 1;
            if (score[sequence] > maximum[bit]) maximum[bit] = score[sequence];
        }
        double probability[2] = {0.0, 0.0};
        for (int sequence = 0; sequence < 128; ++sequence) {
            int bit = (sequence >> (symbol - first)) & 1;
            probability[bit] += exp(score[sequence] - maximum[bit]);
        }
        metrics[symbol] = maximum[1] + log(probability[1])
                        - maximum[0] - log(probability[0]);
        if (!isfinite(metrics[symbol])) return 1;
    }
    double mean = 0.0, variance = 0.0;
    for (int symbol = 0; symbol < 162; ++symbol) mean += metrics[symbol] / 162.0;
    for (int symbol = 0; symbol < 162; ++symbol) {
        double residual = metrics[symbol] - mean;
        variance += residual * residual / 162.0;
    }
    if (!(variance > 0.0) || !isfinite(variance)) return 1;
    for (int symbol = 0; symbol < 162; ++symbol) {
        double value = metric_scale * metrics[symbol] / sqrt(variance);
        if (value < -128.0) value = -128.0;
        if (value > 127.0) value = 127.0;
        expected[symbol] = value + 128.0;
    }
    return 0;
}

static int test_fading_reference(void)
{
    static const float amplitude[] = {0.125f, 1.0f, 16.0f};
    static const float metric_scale[] = {25.0f, 50.0f, 70.0f};
    for (int fixture = 0; fixture < 9; ++fixture) {
        struct wspr_noncoherent_tones tones, saved;
        unsigned char sync[162], actual[162], expected[162];
        uint32_t random = 0x473abeu + fixture;
        double phase = 0.27 * fixture;
        for (int symbol = 0; symbol < 162; ++symbol) {
            sync[symbol] = fixture % 3 == 0 ? 0 : fixture % 3 == 1 ? 1
                                        : next_random(&random) & 1;
            int signal_tone = sync[symbol] + 2 * (next_random(&random) & 1);
            double advance = fixture < 3 ? 0.0 : 0.41 * fixture + 0.003 * symbol;
            double envelope = 0.1 + 1.8 * (1.0 + sin(0.15 * symbol + fixture));
            for (int tone = 0; tone < 4; ++tone) {
                double real = ((int)(next_random(&random) >> 16) - 32768) / 32768.0;
                double imag = ((int)(next_random(&random) >> 16) - 32768) / 32768.0;
                if (tone == signal_tone) {
                    real += envelope * cos(phase + 0.6 * sin(0.21 * symbol));
                    imag += envelope * sin(phase + 0.6 * sin(0.21 * symbol));
                }
                tones.real[tone][symbol] = amplitude[fixture % 3] * real;
                tones.imag[tone][symbol] = amplitude[fixture % 3] * imag;
                tones.phase_real[tone][symbol] = cos(advance);
                tones.phase_imag[tone][symbol] = sin(advance);
            }
            phase += advance;
        }
        saved = tones;
        if (fading_reference(&tones, sync, metric_scale[fixture / 3], expected)) return 1;
        wspr_fading_soft_metrics(&tones, sync, metric_scale[fixture / 3], actual);
        if (memcmp(&tones, &saved, sizeof tones)) return 1;
        for (int symbol = 0; symbol < 162; ++symbol) {
            /* Float phase/metric rounding can cross one integer quantization boundary. */
            if (abs((int)actual[symbol] - expected[symbol]) > 1) {
                fprintf(stderr, "fading likelihood mismatch: fixture %d, symbol %d, got %u, expected %u\n",
                        fixture, symbol, actual[symbol], expected[symbol]);
                return 1;
            }
        }
    }
    return 0;
}

static int test_fading_metrics(void)
{
    float *real = malloc(45000 * sizeof *real);
    float *imag = malloc(45000 * sizeof *imag);
    if (!real || !imag) exit(2);
    unsigned char bits[162], actual[162];
    make_input(real, imag, 45000, 37.625f, 375, 0.0f, 0.7f, bits);
    for (int sample = 0; sample < 45000; ++sample) {
        double phase = 1.5 * sin(8 * atan(1.0) * 0.035 * sample / 375.0);
        float re = real[sample], im = imag[sample];
        real[sample] = re * cos(phase) - im * sin(phase);
        imag[sample] = re * sin(phase) + im * cos(phase);
    }
    struct wspr_noncoherent_tones tones, saved;
    wspr_noncoherent_prepare_tones(real, imag, 45000, 37.625f, 375, 0.0f, &tones);
    saved = tones;
    wspr_fading_soft_metrics(&tones, pr3, 50, actual);
    if (memcmp(&tones, &saved, sizeof tones)) return 1;
    for (int i = 0; i < 162; ++i) {
        if ((actual[i] > 127) != bits[i]) {
            fprintf(stderr, "wrong fading-signal bit %d\n", i);
            return 1;
        }
    }
    memset(&tones, 0, sizeof tones);
    wspr_fading_soft_metrics(&tones, pr3, 50, actual);
    for (int i = 0; i < 162; ++i) {
        if (actual[i] != 128) return 1;
    }
    free(real);
    free(imag);
    return 0;
}

int main(void)
{
    static const int blocks[] = {3, 1, 2, 1};
    static const int metrics[] = {0, 1, 0, 0};
    float *real = malloc(45000 * sizeof *real);
    float *imag = malloc(45000 * sizeof *imag);
    if (!real || !imag) {
        free(real);
        free(imag);
        return 1;
    }
    for (int fixture = 0; fixture < 12; fixture++) {
        float frequency = fixture % 2 == 0 ? -37.625f : 73.125f;
        float drift = fixture % 3 == 0 ? 0.0f : fixture % 3 == 1 ? 2.25f : 0.000001f;
        int shift = fixture % 4 == 0 ? 375 : -23 + 17 * fixture;
        long count = fixture % 3 == 2 ? shift + 162 * 256 - 73 : 45000;
        unsigned char bits[162], actual[162], expected[162];
        make_input(real, imag, count, frequency, shift, drift,
                    fixture < 6 ? 0.7f : 0.0f, bits);
        struct wspr_noncoherent_tones shared, saved, fresh;
        wspr_noncoherent_prepare_tones(real, imag, count, frequency, shift, drift, &shared);
        saved = shared;
        for (size_t setting = 0; setting < sizeof blocks / sizeof blocks[0]; setting++) {
            int block = blocks[setting], metric = metrics[setting];
            wspr_noncoherent_soft_metrics(&shared, pr3, 50, block, metric, actual);
            if (memcmp(&shared, &saved, sizeof shared) != 0) {
                fprintf(stderr, "metric changed shared preparation: fixture %d, block %d\n",
                        fixture, block);
                return 1;
            }
            wspr_noncoherent_prepare_tones(real, imag, count, frequency, shift, drift, &fresh);
            wspr_noncoherent_soft_metrics(&fresh, pr3, 50, block, metric, expected);
            if (memcmp(actual, expected, sizeof actual) != 0) {
                fprintf(stderr, "shared/fresh soft bytes differ: fixture %d, block %d\n",
                        fixture, block);
                return 1;
            }
            if (fixture == 0) {
                for (int i = 0; i < 162; i++) {
                    if ((actual[i] > 127) != bits[i]) {
                        fprintf(stderr, "wrong strong-signal bit %d, block %d\n", i, block);
                        return 1;
                    }
                }
            }
        }
    }
    free(real);
    free(imag);
    return test_fading_reference() || test_fading_metrics();
}
