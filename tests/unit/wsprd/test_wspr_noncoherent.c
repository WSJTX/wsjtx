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
    return 0;
}
