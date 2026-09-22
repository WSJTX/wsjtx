/*
 This file is part of program pi4d - see pi4_spec.h for a description
 and license.

 File name: pi4_demod.c
*/

#include <math.h>
#include <stdlib.h>
#include <fftw3.h>

#include "pi4_demod.h"
#include "pi4_spec.h"

#define PI4_PI 3.14159265358979323846f

static fftwf_plan g_fft_plan;
static float *g_fft_in;
static fftwf_complex *g_fft_out;
static float g_hann[PI4_SYMBOL_SAMPLES];
static int g_initialised = 0;

void pi4_demod_init (void)
{
  int i;
  if (g_initialised) return;
  g_fft_in = (float *) fftwf_malloc (sizeof (float) * PI4_SYMBOL_SAMPLES);
  g_fft_out = (fftwf_complex *) fftwf_malloc (sizeof (fftwf_complex) * (PI4_SYMBOL_SAMPLES / 2 + 1));
  g_fft_plan = fftwf_plan_dft_r2c_1d (PI4_SYMBOL_SAMPLES, g_fft_in, g_fft_out, FFTW_ESTIMATE);
  /* Periodic Hann window - see pi4_spec.h's module doc on the coarse
     scan needing one (sidelobe leakage across competing variant
     hypotheses) while the Goertzel refinement below does not. */
  for (i = 0; i < PI4_SYMBOL_SAMPLES; i++) {
    g_hann[i] = 0.5f * (1.0f - cosf (2.0f * PI4_PI * (float) i / (float) PI4_SYMBOL_SAMPLES));
  }
  g_initialised = 1;
}

void pi4_demod_cleanup (void)
{
  if (!g_initialised) return;
  fftwf_destroy_plan (g_fft_plan);
  fftwf_free (g_fft_in);
  fftwf_free (g_fft_out);
  g_initialised = 0;
}

void pi4_compute_spectra (pi4_spectra_t *out, const float *audio, long n_audio,
                           long start_sample, float max_hz)
{
  int nbins_raw = (int) ceilf (max_hz / PI4_BIN_HZ) + 1;
  int cap = PI4_SYMBOL_SAMPLES / 2;
  int nbins = nbins_raw < cap ? nbins_raw : cap;
  int sym, k, bin;

  out->nbins = nbins;
  out->mag2 = (float *) malloc (sizeof (float) * (size_t) PI4_N_SYMBOLS * (size_t) nbins);

  for (sym = 0; sym < PI4_N_SYMBOLS; sym++) {
    long base = start_sample + (long) sym * PI4_SYMBOL_SAMPLES;
    for (k = 0; k < PI4_SYMBOL_SAMPLES; k++) {
      long idx = base + k;
      float s = (idx >= 0 && idx < n_audio) ? audio[idx] : 0.0f;
      g_fft_in[k] = s * g_hann[k];
    }
    fftwf_execute (g_fft_plan);
    for (bin = 0; bin < nbins; bin++) {
      float re = g_fft_out[bin][0];
      float im = g_fft_out[bin][1];
      out->mag2[sym * nbins + bin] = re * re + im * im;
    }
  }
}

void pi4_spectra_free (pi4_spectra_t *s)
{
  free (s->mag2);
  s->mag2 = NULL;
}

float pi4_spectra_power_near (const pi4_spectra_t *s, int symbol, float hz)
{
  int bin = (int) (hz / PI4_BIN_HZ + 0.5f);
  if (bin < 0 || bin >= s->nbins) return 0.0f;
  return s->mag2[symbol * s->nbins + bin];
}

float pi4_goertzel_power (const float *audio, long n_audio, long start_sample,
                           int n, float sample_rate, float hz)
{
  float k = hz / sample_rate * (float) n;
  float w = 2.0f * PI4_PI * k / (float) n;
  float coeff = 2.0f * cosf (w);
  float s1 = 0.0f, s2 = 0.0f;
  float real, imag;
  int i;
  for (i = 0; i < n; i++) {
    long idx = start_sample + i;
    float x = (idx >= 0 && idx < n_audio) ? audio[idx] : 0.0f;
    float s0 = x + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  real = s1 - s2 * cosf (w);
  imag = s2 * sinf (w);
  return real * real + imag * imag;
}

void pi4_symbol_tone_powers (const float *audio, long n_audio, long start_sample,
                              int symbol, float tone0_hz, float spacing_hz,
                              float powers_out[4])
{
  long base = start_sample + (long) symbol * PI4_SYMBOL_SAMPLES;
  int t;
  for (t = 0; t < 4; t++) {
    powers_out[t] = pi4_goertzel_power (audio, n_audio, base, PI4_SYMBOL_SAMPLES,
                                         PI4_SAMPLE_RATE, tone0_hz + (float) t * spacing_hz);
  }
}
