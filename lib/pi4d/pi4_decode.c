/*
 This file is part of program pi4d - see pi4_decode.h for a description
 and pi4_spec.h for license.

 File name: pi4_decode.c
*/

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "pi4_decode.h"
#include "pi4_demod.h"
#include "../wsprd/fano.h"

/* ---- Soft-decision Fano decode ----
   Same code family WSPR and JT9 use (see pi4_spec.h's module doc), so
   the branch-metric shape is the exact rate-1/2 Fano metric
   0.5 - log2(1 + e^-l), which saturates for a confident agreement and
   falls away without bound for a confident disagreement. lib/wsprd's
   fano() wants a static 256-entry mettab[hypothesis][soft-byte] rather
   than a metric computed per call, so the continuous LLR this decoder
   works in is quantised to one byte per symbol and the table is built
   once (pi4_decode_init) from the same branch-metric formula, sampled
   at each byte value's bin centre. */
#define PI4_LLR_TARGET_SD 2.8f
#define PI4_LLR_CLAMP_SD 2.54f
#define PI4_LLR_CLAMP (PI4_LLR_CLAMP_SD * PI4_LLR_TARGET_SD)
#define PI4_FANO_SCALE 50.0f
#define PI4_FANO_METRIC_FLOOR (-8.0f)
#define PI4_FANO_DELTA 170 /* (3.4 * FANO_SCALE) */
#define PI4_FANO_MAX_CYCLES 20000u
#define PI4_LOG2_E 1.4426950408889634f

/* Least fit a decode must show before it is reported - see fit_of.
   Set well above zero: unlike WSPR's equivalent gate this has no large
   synthesised corpus to tune against (PI4 is only ever one signal at a
   time in a receiver's passband), so a comfortable margin costs nothing
   and is the safer default until measured against real beacon
   recordings. */
#define PI4_MIN_FIT 0.20f

/* How far either side of the buffer's nominal boundary sample the
   start-time search runs. */
#define PI4_TIME_RADIUS_S 2.5f

/* Time-search step: an eighth of a symbol (~20.8 ms). */
#define PI4_TIME_STEP_SAMPLES (PI4_SYMBOL_SAMPLES / 8)

/* How far either side of a variant's conventional tone-0 frequency the
   frequency search runs. */
#define PI4_FREQ_RADIUS_HZ 150.0f

/* Coarse-stage sync scores below this are not worth refining. */
#define PI4_MIN_COARSE_SCORE 0.06f

/* How close two decodes' alignments have to be, in seconds, to be
   treated as the same underlying audio rather than two distinct
   signals. */
#define PI4_OVERLAP_WINDOW_S (PI4_TIME_RADIUS_S * 2.0f)

static int g_mettab[2][256];

static float branch_metric (float l)
{
  float log2_1p_exp, m;
  if (-l > 30.0f) {
    log2_1p_exp = -l * PI4_LOG2_E;
  } else {
    log2_1p_exp = log1pf (expf (-l)) * PI4_LOG2_E;
  }
  m = 0.5f - log2_1p_exp;
  return m < PI4_FANO_METRIC_FLOOR ? PI4_FANO_METRIC_FLOOR : m;
}

static int quantise (float m)
{
  return (int) lroundf (m * PI4_FANO_SCALE);
}

static void build_mettab (void)
{
  int b;
  for (b = 0; b < 256; b++) {
    float l = -PI4_LLR_CLAMP + ((float) b + 0.5f) * (2.0f * PI4_LLR_CLAMP / 256.0f);
    g_mettab[0][b] = quantise (branch_metric (l));
    g_mettab[1][b] = quantise (branch_metric (-l));
  }
}

/* Inverse of build_mettab's bin centring: the byte a (clamped) LLR
   value quantises to. */
static unsigned char byte_of (float l)
{
  int b;
  if (l < -PI4_LLR_CLAMP) l = -PI4_LLR_CLAMP;
  if (l > PI4_LLR_CLAMP) l = PI4_LLR_CLAMP;
  b = (int) ((l + PI4_LLR_CLAMP) / (2.0f * PI4_LLR_CLAMP) * 256.0f);
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  return (unsigned char) b;
}

/* Scale a set of channel-order LLRs to a fixed spread so a weak and a
   strong transmission look the same size to a decoder whose bias is a
   constant. */
static void normalise (float *llrs, int n)
{
  float mean = 0.0f, var = 0.0f, sd, k;
  int i;
  for (i = 0; i < n; i++) mean += llrs[i];
  mean /= (float) n;
  for (i = 0; i < n; i++) { float d = llrs[i] - mean; var += d * d; }
  var /= (float) n;
  sd = sqrtf (var);
  if (!(sd > 0.0f) || !isfinite (sd)) return;
  k = PI4_LLR_TARGET_SD / sd;
  for (i = 0; i < n; i++) {
    float v = llrs[i] * k;
    if (v < -PI4_LLR_CLAMP) v = -PI4_LLR_CLAMP;
    if (v > PI4_LLR_CLAMP) v = PI4_LLR_CLAMP;
    llrs[i] = v;
  }
}

/* Build the 146 channel-order LLRs from measured tone powers, decode,
   and return the recovered 42-bit message value and the codeword's
   hard-error count. Returns 0 on success, -1 if the Fano search does
   not converge. */
static int fano_decode_pi4 (const float powers[PI4_N_SYMBOLS][4], uint64_t *info_n_out,
                             unsigned int *hard_errors_out)
{
  float llrs[PI4_N_SYMBOLS];
  float codeword_order[PI4_N_SYMBOLS];
  unsigned char symbols[PI4_N_SYMBOLS];
  unsigned char decdata[PI4_NBITS_BYTES];
  unsigned char coded[PI4_CODED_BITS];
  unsigned char recoded_channel[PI4_N_SYMBOLS];
  unsigned int metric, cycles, maxnp;
  uint64_t info_n;
  unsigned int hard_errors = 0;
  int i, rc;

  for (i = 0; i < PI4_N_SYMBOLS; i++) {
    int sync_bit = pi4_sync[i];
    /* Positive => data bit 0 more likely: the power at the "data=0"
       tone (sync bit alone) minus the "data=1" tone (sync bit + 2). */
    llrs[i] = powers[i][sync_bit] - powers[i][sync_bit + 2];
  }
  normalise (llrs, PI4_N_SYMBOLS);
  pi4_deinterleave_f (llrs, codeword_order);
  for (i = 0; i < PI4_N_SYMBOLS; i++) symbols[i] = byte_of (codeword_order[i]);

  rc = fano (&metric, &cycles, &maxnp, decdata, symbols, PI4_NBITS,
             g_mettab, PI4_FANO_DELTA, PI4_FANO_MAX_CYCLES);
  if (rc != 0) return -1;

  info_n = pi4_unpack_info_bits (decdata);

  /* Re-encode and count disagreements against the channel-order LLRs -
     purely informational, the caller uses this only to rank
     candidates, not to gate. */
  pi4_encode_coded_bits (info_n, coded);
  pi4_interleave_to_channel_u8 (coded, recoded_channel);
  for (i = 0; i < PI4_N_SYMBOLS; i++) {
    int bit_is_1 = llrs[i] < 0.0f;
    if ((recoded_channel[i] == 1) != bit_is_1) hard_errors++;
  }

  *info_n_out = info_n;
  *hard_errors_out = hard_errors;
  return 0;
}

/* Shape-only check on a decoded message: empty, or every character the
   same, is refused outright - the Fano search's degenerate fallback
   for a codeword it converges on but does not clearly support (see
   pi4_decode.h). */
static int is_plausible (const char *text)
{
  size_t len = strlen (text);
  size_t i;
  if (len == 0) return 0;
  if (len < 2) return 1;
  for (i = 1; i < len; i++) if (text[i] != text[0]) return 1;
  return 0;
}

/* How much of the received tone energy the decoded message accounts
   for - the one check standing between a marginal candidate and an
   invented callsign, since this FEC carries no CRC. */
static float fit_of (const float powers[PI4_N_SYMBOLS][4], const unsigned char symbols[PI4_N_SYMBOLS])
{
  float num = 0.0f, den = 0.0f;
  int n;
  for (n = 0; n < PI4_N_SYMBOLS; n++) {
    int t = symbols[n];
    int alt = t ^ 2;
    float mt = sqrtf (powers[n][t]);
    float malt = sqrtf (powers[n][alt]);
    num += mt - malt;
    den += mt + malt;
  }
  return den > 0.0f ? num / den : 0.0f;
}

/* A per-6-Hz-bin SNR estimate, using the two tones a symbol's sync bit
   rules out entirely as the noise reference. */
static float snr_estimate (const float powers[PI4_N_SYMBOLS][4], const unsigned char symbols[PI4_N_SYMBOLS])
{
  float sig = 0.0f, noise = 0.0f, ratio;
  int i;
  for (i = 0; i < PI4_N_SYMBOLS; i++) {
    int t = symbols[i];
    int imp0 = t ^ 1;
    int imp1 = imp0 ^ 2;
    float noise_here = (powers[i][imp0] + powers[i][imp1]) / 2.0f;
    float s = powers[i][t] - noise_here;
    if (s < 0.0f) s = 0.0f;
    noise += noise_here;
    sig += s;
  }
  if (noise <= 0.0f) return -INFINITY;
  ratio = sig / noise;
  if (ratio < 1e-6f) ratio = 1e-6f;
  return 10.0f * log10f (ratio);
}

/* How well a hypothesis's sync bits agree with what the audio actually
   contains - large and positive at the true alignment, small elsewhere,
   without needing to know anything about the message. */
static float sync_score (const pi4_spectra_t *spectra, float tone0_hz, float spacing_hz)
{
  float consistent = 0.0f, total = 0.0f;
  int n;
  for (n = 0; n < PI4_N_SYMBOLS; n++) {
    int sync_bit = pi4_sync[n];
    float p[4], sum = 0.0f;
    int t;
    for (t = 0; t < 4; t++) {
      p[t] = pi4_spectra_power_near (spectra, n, tone0_hz + (float) t * spacing_hz);
      sum += p[t];
    }
    total += sum;
    consistent += p[sync_bit] + p[sync_bit + 2];
  }
  return total > 0.0f ? (2.0f * consistent - total) / total : 0.0f;
}

/* sync_score, but at an exact frequency/time rather than a Spectra's
   bin grid - the refinement pass's scoring function. */
static float exact_sync_score (const float *audio, long n_audio, long start_sample,
                                float tone0_hz, float spacing_hz)
{
  float consistent = 0.0f, total = 0.0f;
  int n;
  for (n = 0; n < PI4_N_SYMBOLS; n++) {
    int sync_bit = pi4_sync[n];
    float p[4], sum = 0.0f;
    int t;
    pi4_symbol_tone_powers (audio, n_audio, start_sample, n, tone0_hz, spacing_hz, p);
    for (t = 0; t < 4; t++) sum += p[t];
    total += sum;
    consistent += p[sync_bit] + p[sync_bit + 2];
  }
  return total > 0.0f ? (2.0f * consistent - total) / total : 0.0f;
}

typedef struct {
  long start_sample;
  pi4_variant_t variant;
  float tone0_hz;
  float score;
} coarse_hit_t;

/* The best (variant, tone-0 frequency) at one start time, over one
   shared Spectra. */
static void best_at_start (const float *audio, long n_audio, long start, float max_hz,
                            coarse_hit_t *out)
{
  pi4_spectra_t spectra;
  int vi;
  coarse_hit_t best;
  best.score = -3.0e38f;
  best.start_sample = start;
  best.variant = PI4_VARIANT_PI4;
  best.tone0_hz = 0.0f;

  pi4_compute_spectra (&spectra, audio, n_audio, start, max_hz);
  for (vi = 0; vi < PI4_VARIANT_COUNT; vi++) {
    pi4_variant_t variant = pi4_variant_all[vi];
    float spacing = pi4_variant_tone_spacing_hz (variant);
    float centre = pi4_variant_conventional_tone0_hz (variant);
    int bin_steps = (int) (PI4_FREQ_RADIUS_HZ / PI4_BIN_HZ);
    int k;
    for (k = -bin_steps; k <= bin_steps; k++) {
      float tone0 = centre + (float) k * PI4_BIN_HZ;
      float score = sync_score (&spectra, tone0, spacing);
      if (score > best.score) {
        best.score = score;
        best.start_sample = start;
        best.variant = variant;
        best.tone0_hz = tone0;
      }
    }
  }
  pi4_spectra_free (&spectra);
  *out = best;
}

static int cmp_coarse_desc (const void *a, const void *b)
{
  const coarse_hit_t *ha = (const coarse_hit_t *) a;
  const coarse_hit_t *hb = (const coarse_hit_t *) b;
  if (ha->score < hb->score) return 1;
  if (ha->score > hb->score) return -1;
  return 0;
}

/* Stage 1: a grid over start time, beacon variant and tone-0 frequency,
   reduced to the handful of cells worth a real decode attempt. */
static int coarse_search (const float *audio, long n_audio, long boundary_sample,
                           coarse_hit_t *kept)
{
  long radius_samples = (long) (PI4_TIME_RADIUS_S * PI4_SAMPLE_RATE);
  long steps = radius_samples / PI4_TIME_STEP_SAMPLES;
  long n_starts = 2 * steps + 1;
  float max_hz = 0.0f;
  int vi, kept_count = 0, i;
  long k, n_hits = 0;
  coarse_hit_t *hits;

  for (vi = 0; vi < PI4_VARIANT_COUNT; vi++) {
    pi4_variant_t v = pi4_variant_all[vi];
    float m = pi4_variant_conventional_tone0_hz (v) + PI4_FREQ_RADIUS_HZ + 3.0f * pi4_variant_tone_spacing_hz (v);
    if (m > max_hz) max_hz = m;
  }

  hits = (coarse_hit_t *) malloc (sizeof (coarse_hit_t) * (size_t) n_starts);
  for (k = -steps; k <= steps; k++) {
    long start = boundary_sample + k * PI4_TIME_STEP_SAMPLES;
    if (start + (long) PI4_N_SYMBOLS * PI4_SYMBOL_SAMPLES <= 0) continue;
    best_at_start (audio, n_audio, start, max_hz, &hits[n_hits]);
    n_hits++;
  }

  qsort (hits, (size_t) n_hits, sizeof (coarse_hit_t), cmp_coarse_desc);

  for (i = 0; i < (int) n_hits && kept_count < PI4_MAX_COARSE_KEEP; i++) {
    int dup = 0, j;
    if (hits[i].score < PI4_MIN_COARSE_SCORE) break;
    for (j = 0; j < kept_count; j++) {
      if (kept[j].variant == hits[i].variant &&
          labs (kept[j].start_sample - hits[i].start_sample) < (long) (PI4_SAMPLE_RATE * 0.5f)) {
        dup = 1;
        break;
      }
    }
    if (!dup) kept[kept_count++] = hits[i];
  }
  free (hits);
  return kept_count;
}

/* Stage 2 and 3: close in on the coarse hit's exact alignment, then
   attempt a full decode there. Returns 1 and fills *out on a
   successful, plausible, well-fitting decode; 0 otherwise. */
static int refine_and_decode (const float *audio, long n_audio, long boundary_sample,
                               const coarse_hit_t *hit, pi4_decode_t *out)
{
  static const long time_offsets[5] = { -100, -50, 0, 50, 100 };
  static const float freq_offsets[5] = { -6.0f, -3.0f, 0.0f, 3.0f, 6.0f };
  float spacing = pi4_variant_tone_spacing_hz (hit->variant);
  float best_score = -3.0e38f;
  long best_start = hit->start_sample;
  float best_tone0 = hit->tone0_hz;
  float powers[PI4_N_SYMBOLS][4];
  uint64_t info_n;
  unsigned int hard_errors;
  char text[9];
  unsigned char symbols[PI4_N_SYMBOLS];
  float fit;
  int i, j;

  for (i = 0; i < 5; i++) {
    for (j = 0; j < 5; j++) {
      long start = hit->start_sample + time_offsets[i];
      float tone0 = hit->tone0_hz + freq_offsets[j];
      float score = exact_sync_score (audio, n_audio, start, tone0, spacing);
      if (score > best_score) {
        best_score = score;
        best_start = start;
        best_tone0 = tone0;
      }
    }
  }

  for (i = 0; i < PI4_N_SYMBOLS; i++) {
    pi4_symbol_tone_powers (audio, n_audio, best_start, i, best_tone0, spacing, powers[i]);
  }

  if (fano_decode_pi4 (powers, &info_n, &hard_errors) != 0) return 0;
  pi4_unpack_message (info_n, text);
  if (!is_plausible (text)) return 0;

  pi4_encode_symbols_for (info_n, symbols);
  fit = fit_of (powers, symbols);
  if (fit < PI4_MIN_FIT) return 0;

  strcpy (out->text, text);
  out->variant = hit->variant;
  out->tone0_hz = best_tone0;
  out->dt_sec = (float) (best_start - boundary_sample) / PI4_SAMPLE_RATE;
  out->snr_db = snr_estimate (powers, symbols);
  out->fit = fit;
  out->hard_errors = hard_errors;
  return 1;
}

static int cmp_decode_fit_desc (const void *a, const void *b)
{
  const pi4_decode_t *da = (const pi4_decode_t *) a;
  const pi4_decode_t *db = (const pi4_decode_t *) b;
  if (da->fit < db->fit) return 1;
  if (da->fit > db->fit) return -1;
  return 0;
}

/* Keep only the best-fit decode among any that overlap in time - a
   strong, clean, highly-structured signal can spuriously satisfy the
   sync correlation (and even the fit gate) under a *wrong* tone-spacing
   hypothesis; a physical beacon transmits one variant at a time, so two
   convincing decodes claiming overlapping windows are never both real. */
static int dedup_overlapping (pi4_decode_t *decodes, int n)
{
  pi4_decode_t kept[PI4_MAX_COARSE_KEEP];
  int kept_n = 0, i, j;
  qsort (decodes, (size_t) n, sizeof (pi4_decode_t), cmp_decode_fit_desc);
  for (i = 0; i < n; i++) {
    int dup = 0;
    for (j = 0; j < kept_n; j++) {
      if (fabsf (kept[j].dt_sec - decodes[i].dt_sec) < PI4_OVERLAP_WINDOW_S) { dup = 1; break; }
    }
    if (!dup) kept[kept_n++] = decodes[i];
  }
  memcpy (decodes, kept, sizeof (pi4_decode_t) * (size_t) kept_n);
  return kept_n;
}

void pi4_decode_init (void)
{
  pi4_demod_init ();
  build_mettab ();
}

void pi4_decode_cleanup (void)
{
  pi4_demod_cleanup ();
}

int pi4_decode_window (const float *audio, long n_audio, long boundary_sample,
                       pi4_decode_t *out_decodes)
{
  coarse_hit_t hits[PI4_MAX_COARSE_KEEP];
  int n_hits = coarse_search (audio, n_audio, boundary_sample, hits);
  int n_out = 0;
  int i;
  for (i = 0; i < n_hits; i++) {
    if (refine_and_decode (audio, n_audio, boundary_sample, &hits[i], &out_decodes[n_out])) n_out++;
  }
  return dedup_overlapping (out_decodes, n_out);
}
