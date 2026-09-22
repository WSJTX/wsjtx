/*
 This file is part of program pi4d - see pi4_spec.h for a description
 and license.

 File name: pi4_demod.h

 Description: turning 12 kHz audio into per-symbol tone energy. Two
 demodulators, for two different jobs. pi4_spectra_t is a coarse,
 FFT-bin-resolution scan built once per candidate start time and reused
 for every tone-spacing variant and base-frequency hypothesis scored
 against it. pi4_goertzel_power is the opposite trade: one exact (not
 bin-snapped) frequency, used only to refine and then finally measure
 the handful of candidates the coarse scan narrows the search down to.
*/

#ifndef PI4_DEMOD_H
#define PI4_DEMOD_H

/* Decode sample rate: everything here works in 12 kHz audio, matching
   how WSJT-X itself saves its .wav files. */
#define PI4_SAMPLE_RATE 12000.0f

/* Samples in one symbol: 12000*0.166667s, exactly - 2000 samples/symbol
   at 12 kHz, "exactly 360 symbol widths per minute" per the protocol
   page. */
#define PI4_SYMBOL_SAMPLES 2000

/* FFT bin width of a pi4_spectra_t: SAMPLE_RATE/SYMBOL_SAMPLES, which is
   also the symbol rate - the matched-filter bin spacing for this
   waveform. */
#define PI4_BIN_HZ (PI4_SAMPLE_RATE / (float) PI4_SYMBOL_SAMPLES)

typedef struct {
  int nbins;    /* bins per symbol, covering 0..nbins*PI4_BIN_HZ Hz */
  float *mag2;  /* mag2[symbol*nbins+bin], malloc'd, PI4_N_SYMBOLS*nbins floats */
} pi4_spectra_t;

/* Call once before any decode attempt; frees FFTW plan/buffers reused
   across every pi4_compute_spectra call. */
void pi4_demod_init (void);
void pi4_demod_cleanup (void);

/* Build a spectra covering 0..max_hz for the PI4_N_SYMBOLS symbols
   starting at start_sample of audio[0..n_audio). Missing samples past
   either end of audio read as silence rather than being out of bounds.
   Caller must pi4_spectra_free the result. */
void pi4_compute_spectra (pi4_spectra_t *out, const float *audio, long n_audio,
                           long start_sample, float max_hz);
void pi4_spectra_free (pi4_spectra_t *s);

/* Power in symbol's spectrum at the bin nearest hz. Out of range reads
   as zero rather than faulting. */
float pi4_spectra_power_near (const pi4_spectra_t *s, int symbol, float hz);

/* The Goertzel algorithm: power at one exact frequency over n samples of
   audio starting at start_sample, without being limited to FFT bin
   spacing. start_sample may run past either end of audio, read as
   silence. */
float pi4_goertzel_power (const float *audio, long n_audio, long start_sample,
                           int n, float sample_rate, float hz);

/* pi4_goertzel_power at each of a symbol's four candidate tones for a
   given tone-0 frequency and spacing - the primitive the refinement
   stage and the final soft-metric extraction both build on. */
void pi4_symbol_tone_powers (const float *audio, long n_audio, long start_sample,
                              int symbol, float tone0_hz, float spacing_hz,
                              float powers_out[4]);

#endif
