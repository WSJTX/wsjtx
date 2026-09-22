/*
 This file is part of program pi4d - see pi4_spec.h for a description
 and license.

 File name: pi4_decode.h

 Description: the PI4 decode search - from a window of 12 kHz audio to
 zero or more pi4_decode_t results. Three stages, cheapest first: a
 coarse grid over start time x beacon variant x tone-0 frequency scored
 by how well each cell's measured tones agree with the fixed *sync*
 half of every symbol; a Goertzel refinement of the handful of cells
 that survive; and only then a full soft-decision Fano decode over the
 K=32 rate-1/2 code shared with WSPR/JT9 (lib/wsprd/fano.c, reused
 unmodified). Because this FEC carries no CRC, a converged codeword is
 checked against the audio (fit_of) before being reported, with two
 further gates: is_plausible refuses the decoder's own degenerate
 all-repeated-character fallback, and dedup_overlapping keeps only the
 best-fit decode when a strong signal spuriously "converges" under more
 than one wrong-variant hypothesis at an overlapping alignment - both
 gates carried over from the sdroxide PI4 decoder this was ported from,
 which found them necessary against a real 23cm beacon recording.
*/

#ifndef PI4_DECODE_H
#define PI4_DECODE_H

#include "pi4_spec.h"

/* Coarse-stage candidates carried into refinement - also the most
   pi4_decode_window can return in one call. */
#define PI4_MAX_COARSE_KEEP 8

typedef struct {
  char text[9];             /* decoded message, trimmed, NUL-terminated */
  pi4_variant_t variant;
  float tone0_hz;
  float dt_sec;              /* offset of symbol 0 from boundary_sample */
  float snr_db;               /* per-6-Hz-bin SNR estimate */
  float fit;                   /* how much of the tone energy this message explains, see pi4_decode.c */
  unsigned int hard_errors;
} pi4_decode_t;

/* Call once before any decode attempt (builds the FFT plan and the
   Fano metric table); call cleanup once done. */
void pi4_decode_init (void);
void pi4_decode_cleanup (void);

/* Search audio[0..n_audio) for PI4 transmissions around boundary_sample
   (the nominal message-start sample this window was captured against;
   the time search runs +/-2.5s either side of it). Writes up to
   PI4_MAX_COARSE_KEEP results into out_decodes and returns how many. */
int pi4_decode_window (const float *audio, long n_audio, long boundary_sample,
                       pi4_decode_t *out_decodes);

#endif
