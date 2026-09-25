/*
 This file is part of program pi4d - see pi4_spec.h for a description
 and license.

 File name: pi4d_selftest.c

 Description: a standalone regression check, not part of the pi4d
 build itself. Confirms this port against the same fixed points the
 sdroxide PI4 decoder this was ported from checks itself against: the
 protocol page's own OZ7IGY worked example (source encoding, the
 convolutionally-encoded/interleaved bitstreams, and the full 146
 transmitted symbols), then a synthesised-audio round trip and a
 silence/noise null-result check, exercising the whole demod+decode
 pipeline rather than just the protocol constants.

 Build and run standalone (not wired into CMakeLists.txt - see
 CONTRIBUTING.md's request to test decode changes against known .wav
 files; this is that evidence for the parts of this change buildable
 without the rest of WSJT-X):

   gcc -std=gnu99 -O2 -Wall -Wextra -Ilib/pi4d -Ilib/wsprd \
       lib/pi4d/pi4d_selftest.c lib/pi4d/pi4_spec.c lib/pi4d/pi4_demod.c \
       lib/pi4d/pi4_decode.c lib/wsprd/fano.c -lfftw3f -lm -o /tmp/pi4d_selftest
   /tmp/pi4d_selftest
*/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pi4_decode.h"
#include "pi4_demod.h"
#include "pi4_spec.h"

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf (stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_failures++; } \
    else { printf ("ok:   %s\n", msg); } \
  } while (0)

/* Source encoding, straight from the protocol page: "OZ7IGY" padded to
   eight characters packs to 2 851 949 862 724. */
static void test_pack_message (void)
{
  uint64_t n;
  CHECK (pi4_pack_message ("OZ7IGY", &n) == 0 && n == 2851949862724ULL,
         "pack_message(OZ7IGY) matches the published worked example");
  CHECK (pi4_pack_message ("OZ7IGY  ", &n) == 0 && n == 2851949862724ULL,
         "pack_message with explicit trailing spaces matches too");
}

static void test_unpack_is_inverse_of_pack (void)
{
  uint64_t n;
  char buf[9];
  pi4_pack_message ("OZ7IGY", &n);
  pi4_unpack_message (n, buf);
  CHECK (strcmp (buf, "OZ7IGY") == 0, "unpack(pack(OZ7IGY)) round-trips");

  pi4_pack_message ("PE1ITR/B", &n);
  pi4_unpack_message (n, buf);
  CHECK (strcmp (buf, "PE1ITR/B") == 0, "a full eight characters round-trips with nothing to trim");
}

static void test_rejects_bad_messages (void)
{
  uint64_t n;
  CHECK (pi4_pack_message ("123456789", &n) != 0, "a message longer than eight characters is refused");
  CHECK (pi4_pack_message ("OZ7IGY!", &n) != 0, "a character outside the alphabet is refused");
}

static void test_tone_spacings (void)
{
  CHECK (fabsf (pi4_variant_tone_spacing_hz (PI4_VARIANT_PI4) - 234.375f) < 1e-2f, "PI4 tone spacing");
  CHECK (fabsf (pi4_variant_tone_spacing_hz (PI4_VARIANT_PI4_80) - 468.75f) < 1e-2f, "PI4-80 tone spacing");
  CHECK (fabsf (pi4_variant_tone_spacing_hz (PI4_VARIANT_PI4_96) - 562.5f) < 1e-2f, "PI4-96 tone spacing");
  CHECK (fabsf (pi4_variant_tone_spacing_hz (PI4_VARIANT_PI4_120) - 703.125f) < 1e-2f, "PI4-120 tone spacing");
  CHECK (fabsf (pi4_variant_conventional_tone0_hz (PI4_VARIANT_PI4) - 682.8125f) < 1e-2f,
         "PI4's conventional tone0 matches the protocol page's own figure");
}

/* The end-to-end protocol regression test: OZ7IGY's 144.471 MHz
   transmission, exactly as the protocol page's own worked example
   gives it. */
static void test_oz7igy_worked_example (void)
{
  static const unsigned char want_symbols[PI4_N_SYMBOLS] = {
    2,0,1,0,0,3,3,3,3,2,3,2,1,2,1,2,0,3,2,2,0,3,2,2,0,1,1,0,0,1,
    3,1,3,0,2,1,1,3,3,1,2,0,1,3,2,1,3,3,3,2,1,2,3,1,2,1,1,0,3,2,
    0,2,0,0,1,3,3,1,3,2,3,2,3,0,2,0,0,2,1,3,3,3,1,2,3,0,0,3,0,2,
    3,2,1,0,2,0,2,1,0,0,1,1,0,2,0,2,2,3,3,2,2,2,2,3,1,0,0,1,3,3,
    0,1,3,1,2,1,3,0,3,0,3,0,1,2,2,0,2,3,1,3,2,0,0,2,1,1,
  };
  uint64_t n;
  unsigned char symbols[PI4_N_SYMBOLS];
  int i, ok;

  pi4_pack_message ("OZ7IGY", &n);
  pi4_encode_symbols_for (n, symbols);
  ok = 1;
  for (i = 0; i < PI4_N_SYMBOLS; i++) if (symbols[i] != want_symbols[i]) ok = 0;
  CHECK (ok, "encode_symbols_for(OZ7IGY) matches the protocol page's worked-example symbols");
}

/* The receiver's half of the same round trip: deinterleaving the
   worked-example's transmitted (post-sync-merge, tone low bit)
   convolutional output must recover the coded bitstream, bit for
   bit. */
static void test_deinterleave_recovers_conv_output (void)
{
  static const unsigned char conv[PI4_N_SYMBOLS] = {
    1,1,0,1,1,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0,1,
    0,1,1,0,0,0,1,0,1,1,1,0,1,0,0,0,1,0,1,0,1,0,1,1,1,1,1,1,1,0,
    1,0,1,1,0,0,0,1,1,1,1,0,1,0,0,1,0,0,1,0,1,1,1,1,1,0,0,1,0,1,
    0,0,1,1,1,1,0,1,0,0,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,1,1,0,1,
    1,0,1,1,0,1,1,0,1,0,1,0,1,1,1,0,1,1,1,1,1,1,0,0,0,0,
  };
  static const unsigned char interleaved[PI4_N_SYMBOLS] = {
    1,0,0,0,0,1,1,1,1,1,1,1,0,1,0,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,
    1,0,1,0,1,0,0,1,1,0,1,0,0,1,1,0,1,1,1,1,0,1,1,0,1,0,0,0,1,1,
    0,1,0,0,0,1,1,0,1,1,1,1,1,0,1,0,0,1,0,1,1,1,0,1,1,0,0,1,0,1,
    1,1,0,0,1,0,1,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,1,1,0,0,0,0,1,1,
    0,0,1,0,1,0,1,0,1,0,1,0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,
  };
  unsigned char got[PI4_N_SYMBOLS];
  int i, ok;
  pi4_deinterleave_u8 (interleaved, got);
  ok = 1;
  for (i = 0; i < PI4_N_SYMBOLS; i++) if (got[i] != conv[i]) ok = 0;
  CHECK (ok, "deinterleave recovers the published convolutional output");
}

static void synth (const char *text, float tone0_hz, float spacing_hz, float amp,
                    float *audio, long n_audio, long offset)
{
  uint64_t n;
  unsigned char symbols[PI4_N_SYMBOLS];
  int sym, k;
  pi4_pack_message (text, &n);
  pi4_encode_symbols_for (n, symbols);
  for (sym = 0; sym < PI4_N_SYMBOLS; sym++) {
    float hz = tone0_hz + (float) symbols[sym] * spacing_hz;
    for (k = 0; k < PI4_SYMBOL_SAMPLES; k++) {
      long idx = offset + (long) sym * PI4_SYMBOL_SAMPLES + k;
      float t = (float) (sym * PI4_SYMBOL_SAMPLES + k) / PI4_SAMPLE_RATE;
      if (idx >= 0 && idx < n_audio) {
        audio[idx] = amp * sinf (2.0f * (float) M_PI * hz * t);
      }
    }
  }
}

static void test_clean_signal_decodes (void)
{
  long n_audio = 32L * (long) PI4_SAMPLE_RATE;
  float *audio = (float *) calloc ((size_t) n_audio, sizeof (float));
  long boundary = (long) PI4_SAMPLE_RATE;
  pi4_decode_t decodes[PI4_MAX_COARSE_KEEP];
  int n, i, found = 0;
  float tone0 = pi4_variant_conventional_tone0_hz (PI4_VARIANT_PI4);

  synth ("OZ7IGY", tone0, pi4_variant_tone_spacing_hz (PI4_VARIANT_PI4), 0.3f, audio, n_audio, boundary);
  n = pi4_decode_window (audio, n_audio, boundary, decodes);
  for (i = 0; i < n; i++) {
    if (strcmp (decodes[i].text, "OZ7IGY") == 0) {
      found = 1;
      CHECK (decodes[i].variant == PI4_VARIANT_PI4, "clean signal identified as PI4 variant");
      CHECK (fabsf (decodes[i].dt_sec) < 0.1f, "clean signal's dt is near zero");
      CHECK (decodes[i].fit > 0.5f, "clean signal's fit is well above the gate");
    }
  }
  CHECK (found, "a clean synthesised OZ7IGY beacon decodes back with its message");
  free (audio);
}

static void test_silence_decodes_to_nothing (void)
{
  long n_audio = 3L * (long) PI4_SAMPLE_RATE;
  float *audio = (float *) calloc ((size_t) n_audio, sizeof (float));
  pi4_decode_t decodes[PI4_MAX_COARSE_KEEP];
  int n = pi4_decode_window (audio, n_audio, (long) PI4_SAMPLE_RATE, decodes);
  CHECK (n == 0, "silence decodes to nothing");
  free (audio);
}

/* A deterministic xorshift/Gaussian source, so the check is
   reproducible without pulling in a PRNG dependency. */
static uint32_t g_rng_state;
static uint32_t next_u32 (void)
{
  g_rng_state ^= g_rng_state << 13;
  g_rng_state ^= g_rng_state >> 17;
  g_rng_state ^= g_rng_state << 5;
  return g_rng_state;
}
static float gaussian (void)
{
  double u1 = ((double) next_u32 () + 0.5) / 4294967296.0;
  double u2 = ((double) next_u32 () + 0.5) / 4294967296.0;
  return (float) (sqrt (-2.0 * log (u1)) * cos (6.283185307179586 * u2));
}

static void test_noise_decodes_to_nothing (void)
{
  long n_audio = 3L * (long) PI4_SAMPLE_RATE;
  float *audio = (float *) malloc (sizeof (float) * (size_t) n_audio);
  pi4_decode_t decodes[PI4_MAX_COARSE_KEEP];
  int n, i, any_bad = 0;
  unsigned seed;
  for (seed = 0xA5A50001u; seed <= 0xA5A50003u; seed++) {
    g_rng_state = seed;
    for (i = 0; i < n_audio; i++) audio[i] = gaussian () * 0.05f;
    n = pi4_decode_window (audio, n_audio, (long) PI4_SAMPLE_RATE, decodes);
    if (n != 0) any_bad = 1;
  }
  CHECK (!any_bad, "Gaussian noise never invents a decode out of pure noise");
  free (audio);
}

int main (void)
{
  pi4_decode_init ();

  test_pack_message ();
  test_unpack_is_inverse_of_pack ();
  test_rejects_bad_messages ();
  test_tone_spacings ();
  test_oz7igy_worked_example ();
  test_deinterleave_recovers_conv_output ();
  test_clean_signal_decodes ();
  test_silence_decodes_to_nothing ();
  test_noise_decodes_to_nothing ();

  pi4_decode_cleanup ();

  if (g_failures) {
    fprintf (stderr, "\n%d check(s) FAILED\n", g_failures);
    return 1;
  }
  printf ("\nall checks passed\n");
  return 0;
}
