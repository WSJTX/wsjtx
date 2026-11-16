#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/wsprd/wspr_coherent.h"

static const unsigned char sync_bits[162]=
{1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
    0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
    0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
    0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
    0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
    0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
    0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0};

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        failures++;
    }
}

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void make_signal(float *real, float *imag, size_t count,
                         float frequency, int shift, float drift,
                         unsigned char bits[162])
{
    const double pi = 3.14159265358979323846;
    const double df = 375.0 / 256.0;
    double phase = 0.37;
    uint32_t random = 0x12345678;
    for (int i = 0; i < 162; i++) bits[i] = next_random(&random) & 1;
    for (size_t k = 0; k < count; k++) {
        int relative = (int)k - shift;
        int symbol = relative / 256;
        real[k] = 0.03f * ((int)(next_random(&random) >> 8) / 8388608.0f - 1.0f);
        imag[k] = 0.03f * ((int)(next_random(&random) >> 8) / 8388608.0f - 1.0f);
        if (relative < 0 || symbol >= 162) continue;
        int tone = sync_bits[symbol] + 2 * bits[symbol];
        double fp = frequency + (drift / 2.0) * (symbol - 81.0) / 81.0;
        double amplitude = 0.7 + 0.25 * cos(0.06 * symbol);
        real[k] += amplitude * cos(phase);
        imag[k] += amplitude * sin(phase);
        phase += 2.0 * pi * (fp + (tone - 1.5) * df) / 375.0;
    }
}


static void test_neutral_and_constant(const struct wspr_coherent_plan *plan)
{
    float real[700] = {0}, imag[700] = {0};
    struct wspr_phase_corrected_tones data;
    unsigned char symbols[162];
    wspr_coherent_prepare_tones(plan, real, imag, 700, -17.0f, -257, 1.5f, sync_bits, &data);
    wspr_coherent_soft_metrics(&data, sync_bits, 50, symbols);
    for (int i = 0; i < 162; ++i)
        check(symbols[i] == 128, "silence must produce neutral soft symbols");

    memset(&data, 0, sizeof data);
    for (int i = 0; i < 162; ++i) data.real[sync_bits[i] + 2][i] = 1.0;
    wspr_coherent_soft_metrics(&data, sync_bits, 50, symbols);
    for (int i = 0; i < 162; ++i)
        check(symbols[i] == 178, "constant metrics must normalize without a variance NaN");
}

static void test_signal(const struct wspr_coherent_plan *plan, float frequency, int shift, float drift)
{
    const size_t count = 45000;
    float *real = calloc(count, sizeof *real);
    float *imag = calloc(count, sizeof *imag);
    unsigned char bits[162], symbols[162], repeated[162];
    struct wspr_phase_corrected_tones data, saved;
    if (!real || !imag) exit(2);
    make_signal(real, imag, count, frequency, shift, drift, bits);
    wspr_coherent_prepare_tones(plan, real, imag, count, frequency, shift, drift, sync_bits, &data);
    saved = data;
    wspr_coherent_soft_metrics(&data, sync_bits, 50, symbols);
    for (int i = 0; i < 162; ++i) {
        for (int tone = 0; tone < 4; ++tone)
            check(isfinite(data.real[tone][i]) && isfinite(data.imag[tone][i]),
                  "prepared tones must be finite");
        check((symbols[i] > 128) == bits[i], "coherent metrics must recover transmitted bit polarity");
    }
    wspr_coherent_soft_metrics(&data, sync_bits, 50, repeated);
    check(memcmp(symbols, repeated, sizeof symbols) == 0, "repeated metrics must agree");
    check(memcmp(&saved, &data, sizeof data) == 0, "metrics must preserve prepared tones");
    free(real);
    free(imag);
}

int main(void)
{
    struct wspr_coherent_plan plan = {0};
    if (wspr_coherent_init(&plan)) return 2;
    test_neutral_and_constant(&plan);
    test_signal(&plan, 13.25f, 375, 0.0f);
    test_signal(&plan, 13.25f, 375, 0.000001f);
    test_signal(&plan, -101.0f, 619, 2.0f);
    test_signal(&plan, 109.5f, 19, -3.0f);
    wspr_coherent_destroy(&plan);
    wspr_coherent_destroy(&plan);
    return failures ? 1 : 0;
}
