#ifndef WSJTX_DECODER_ENGINE_H
#define WSJTX_DECODER_ENGINE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void *decoder_engine_handle;
enum { DECODER_ENGINE_ABI = 8, DECODER_MODE_JT4 = 4, DECODER_MODE_FT4 = 5, DECODER_MODE_FT8 = 8,
       DECODER_MODE_JT9 = 9, DECODER_MODE_JT65 = 65, DECODER_MODE_Q65 = 66,
       DECODER_MODE_FST4 = 240, DECODER_MODE_FST4W = 241 };
enum { DECODER_SUPPORT_FT8 = 1, DECODER_SUPPORT_FT4 = 2, DECODER_SUPPORT_JT9 = 4,
       DECODER_SUPPORT_JT65 = 8, DECODER_SUPPORT_Q65 = 16, DECODER_SUPPORT_FST4 = 32, DECODER_SUPPORT_FST4W = 64, DECODER_SUPPORT_JT4 = 128 };
enum { DECODER_JT65_SYNC = 0, DECODER_JT65_MESSAGE = 1 };
enum { DECODER_JT65_METHOD_NONE = 0, DECODER_JT65_METHOD_FEC = 1,
       DECODER_JT65_METHOD_DEEP_SEARCH = 2 };
enum { DECODER_JT65_AVERAGE_CAPACITY = 64, DECODER_JT65_CALL_CAPACITY = 10000 };
enum { DECODER_OK = 0, DECODER_INVALID = 1, DECODER_BUSY = 2,
       DECODER_UNSUPPORTED = 3, DECODER_CAPACITY = 4, DECODER_INTERNAL_ERROR = 5 };
enum { DECODER_PHASE_EARLY = 1, DECODER_PHASE_NORMAL = 2, DECODER_PHASE_REPEAT = 3 };
enum { DECODER_SOURCE_LIVE = 0, DECODER_SOURCE_FILE = 1 };
enum { DECODER_FT8_CLASSIC = 0, DECODER_FT8_MTD = 1, DECODER_FT8_SUPERFOX = 2 };
enum { DECODER_PAYLOAD_NONE = 0, DECODER_PAYLOAD_DECODED = 1, DECODER_PAYLOAD_HYPOTHESIS = 2 };
enum { DECODER_SUPERFOX_CQ = 1, DECODER_SUPERFOX_EXCHANGE = 2,
       DECODER_SUPERFOX_FREE_TEXT = 3, DECODER_SUPERFOX_VERIFICATION = 4 };

typedef struct {
  int32_t abi_version;
} decoder_engine_options;

typedef struct {
  /* supported_modes is a bitmask of DECODER_SUPPORT_* values. */
  int32_t abi_version, supported_modes, cancellation, concurrent_sessions, evidence_capacity;
} decoder_engine_capabilities;

/* Option records use fixed-width, space-padded calls and grids. Booleans are 0 or 1.
   half_symbol_stage is the FT8 processing stage (41..50), independent of repeat
   intent and the number of available audio samples. */
typedef struct {
  int32_t half_symbol_stage;
  int32_t reuse_spectrum;
  int32_t utc;
  int32_t qso_progress;
  int32_t receive_frequency_hz;
  int32_t transmit_frequency_hz;
  int32_t search_low_hz;
  int32_t search_high_hz;
  int32_t tolerance_hz;
  int32_t depth;
  int32_t ap_width_hz;
  int32_t contest;
  int32_t candidate_thinning;
  int32_t time_center;
  int32_t cycles;
  int32_t trials;
  int32_t last_transmit;
  int32_t delay;
  int32_t threads;
  int32_t receive_sensitivity;
  int32_t discard_leading_seconds;
  int32_t date;
  int32_t ap_enabled;
  int32_t cq_only;
  int32_t even_sequence;
  int32_t superfox;
  int32_t filtered_retry;
  int32_t stop_hint;
  int32_t mtd;
  int32_t low_threshold;
  int32_t subtract_pass;
  int32_t transmitting;
  int32_t hide_duplicates;
  int32_t hound;
  int32_t standard_mycall;
  int32_t standard_hiscall;
  int32_t ap_mycall;
  int32_t mode_changed;
  int32_t band_changed;
  int32_t dx_search;
  int32_t wide_dx_search;
  int32_t multiple_instances;
  int32_t skip_first_message;
  float eme_delay_seconds;
  char mycall[12];
  char my_base_call[12];
  char hiscall[12];
  char his_base_call[12];
  char mygrid[6];
  char hisgrid[6];
} decoder_ft8_options;

typedef struct {
  int32_t utc;
  int32_t qso_progress;
  int32_t receive_frequency_hz;
  int32_t search_low_hz;
  int32_t search_high_hz;
  int32_t depth;
  int32_t cq_only;
  int32_t contest;
  char mycall[12];
  char hiscall[12];
} decoder_ft4_options;

/* JT4A-G use one-minute periods. UTC is HHMMSS; submode 0..6 selects A..G.
   One signal is searched within receive_frequency_hz +/- tolerance_hz.
   minimum_width_profile is 0..submode; depth is effort 1..3. */
typedef struct {
  int32_t utc, submode, receive_frequency_hz, tolerance_hz, depth, min_sync;
  int32_t minimum_width_profile, averaging, deep_search;
  char mycall[12], hiscall[12], hisgrid[6];
} decoder_jt4_options;

enum { DECODER_JT4_NO_SYNC = 0, DECODER_JT4_SYNC = 1, DECODER_JT4_MESSAGE = 2 };
enum { DECODER_JT4_METHOD_NONE = 0, DECODER_JT4_METHOD_FANO = 1,
       DECODER_JT4_METHOD_DEEP_SEARCH = 2 };
enum { DECODER_JT4_AVERAGE_CAPACITY = 64, DECODER_JT4_CALL_CAPACITY = 6999 };
typedef struct { char call[12], grid[4]; } decoder_jt4_call;
typedef struct {
  int32_t utc, frequency_hz, polarity, used;
  float sync, dt_seconds;
} decoder_jt4_average_entry;

/* Slow JT9A-H use one-minute periods; submode 0..7 selects A..H.
   Decoding spectra are prepared internally, independently of display settings. */
typedef struct {
  int32_t utc;
  int32_t receive_frequency_hz;
  int32_t search_low_hz;
  int32_t search_high_hz;
  int32_t tolerance_hz;
  int32_t depth;
  int32_t submode;
} decoder_jt9_options;

/* JT65A-C use one-minute periods; submode 0..2 selects A..C.
   depth selects effort (1..3), independently of averaging and deep search.
   passes (1..2) and trials (0..1000000) control the VHF search; aggressiveness is 0..11.
   min_sync is the synchronization threshold; negative values enable diagnostics. */
typedef struct {
  int32_t utc, qso_progress, receive_frequency_hz, search_low_hz, search_high_hz;
  int32_t tolerance_hz, depth, submode, min_sync, passes, trials, aggressiveness;
  int32_t single_decode, vhf, averaging, auto_clear, deep_search, ap_enabled;
  char mycall[12], hiscall[12], hisgrid[6];
} decoder_jt65_options;

typedef struct {
  char call[12], grid[4];
} decoder_jt65_call;

typedef struct {
  int32_t utc, frequency_hz, polarity, used;
  float sync, dt_seconds;
} decoder_jt65_average_entry;

/* Submode 0..5 selects A..F. UTC is HHMMSS, including periods of one minute
   or longer. Drift is measured
   in symbol rates over a transmission. now_seconds supplies Unix time for caller
   expiry; no clock is read by the engine. Frequencies and tolerance are bounded
   to 0..5000 Hz; search_low_hz must be less than search_high_hz. */
typedef struct {
  int32_t utc, period_seconds, submode, receive_frequency_hz, tolerance_hz;
  int32_t search_low_hz, search_high_hz, depth, max_drift_symbol_rates;
  int32_t qso_progress, contest, averaging, auto_clear, single_decode;
  int32_t extended_eme_search, pileup, ap_cq_only, now_seconds;
  char mycall[12], hiscall[12], hisgrid[6];
} decoder_q65_options;

enum { DECODER_Q65_CALL_CAPACITY = 50 };
/* Q65 contest history currently supports calls of up to six characters. */
typedef struct {
  char call[12], grid[4];
  int32_t last_seen, frequency_hz;
} decoder_q65_caller;

/* Curves contain curve_count conditioned synchronization strengths; element i
   is at (i + 1) * frequency_step_hz, with zero-based C indexing.
   curve_average_count is the averaging count captured when the curves are
   computed, before any automatic clearing. */
typedef struct {
  int32_t even_count, odd_count, curve_count, curve_average_count;
  float frequency_step_hz, dt_seconds;
} decoder_q65_snapshot;

/* FST4/FST4W use HHMMSS UTC and periods of 15, 30, 60, 120, 300, 900,
   or 1800 seconds. blanker_mode is zero for a fixed percentage or 1/2/5
   for an automatic sweep from zero through 20 percent. */
typedef struct {
  int32_t utc, period_seconds, receive_frequency_hz, search_low_hz, search_high_hz;
  int32_t tolerance_hz, depth, qso_progress, single_decode, ap_cq_only;
  int32_t blanker_mode, blanker_percent, measure_doppler;
  float eme_delay_seconds;
  char mycall[12], hiscall[12];
} decoder_fst4_options;

enum { DECODER_FST4W_CALL_CAPACITY = 100 };
/* Known call/grid pairs are fixed-width, space-padded strings. */
typedef struct { char call_grid[20]; } decoder_fst4w_call;

/* Only the options for mode are read. EARLY is an FT8 phase; other modes use
   NORMAL or REPEAT. Mode support is reported by supported_modes. */
typedef struct {
  int64_t input_id, analysis_id;
  int32_t attempt_no, mode, phase, source;
  decoder_ft8_options ft8;
  decoder_ft4_options ft4;
  decoder_jt9_options jt9;
  decoder_jt65_options jt65;
  decoder_q65_options q65;
  decoder_fst4_options fst4;
  decoder_jt4_options jt4;
} decoder_attempt_request;

/* Borrowed read-only mono signed PCM. Only sample_count samples are read.
   All modes accept 12000 Hz. FT8 accepts 1..180000 samples;
   FT4 accepts 1..72576 samples (its analysis window within a 7.5-second period).
   JT4 accepts 11025 or 12000 Hz and 1..sample_rate_hz * 60 samples,
   analyzing the first 52 seconds. Its PCM, rate and extent stay fixed until release.
   Slow JT9 and JT65 accept 1..720000 samples (a one-minute period).
   JT65 analyzes up to 52 seconds and retains its silence-block rejection;
   short or gapped input may complete without observations.
   Q65 accepts up to period_seconds * 12000 samples, for periods of 15, 30, 60,
   120, or 300 seconds.
   FST4/FST4W accept up to period_seconds * 12000 samples, including
   periods of 900 and 1800 seconds.
   Short inputs are zero-padded. Release input before changing its identity or mode.
   Q65 caches preparation by input identity: its PCM and sample_count must remain
   unchanged until release; repeated attempts may change decoding options.
   FST4/FST4W retain the input period and sample count until release. Repeats
   perform fresh decoding with updated options; results are deduplicated within
   each attempt. */
typedef struct {
  const int16_t *samples;
  int32_t sample_count, sample_rate_hz;
} decoder_audio_view;

/* waveform_start_seconds places sample zero of the reference waveform in
   the identified input, before any engine cropping, and may be negative.
   It is an estimate, not a record of a subsequent subtraction refinement.
   frequency_hz is the reference waveform's tone-zero frequency.

   FT8: 79 GFSK symbols, 1920 samples/symbol at 12 kHz, BT=2; the
   gen_ft8wave template includes its edge ramps within those 79 symbols.
   FT4: 103 GFSK tones, 576 samples/symbol at 12 kHz, BT=1; the
   gen_ft4wave template includes an additional leading and trailing ramp
   interval, totaling 105 symbol intervals. Its start precedes the first
   synchronization-symbol reference by one symbol (0.048 seconds).
   Both origins are template sample zero, independent of initial amplitude.
   Payload bits are canonical message bits, before mode-specific scrambling. */
typedef struct {
  int8_t payload77[77], tones[79];
  int32_t payload_origin, has_tones, has_waveform_start, method;
  float waveform_start_seconds;
} decoder_ft8_evidence;

typedef struct {
  int8_t payload77[77], tones[103];
  int32_t payload_origin, has_tones, has_waveform_start, method;
  float waveform_start_seconds;
} decoder_ft4_evidence;

typedef struct {
  int8_t symbols[50];
  int32_t kind, child_index;
} decoder_superfox_evidence;

/* Drift is quantized by the JT9A estimator to 12000/16384 Hz over
   its 48.96-second analysis span, then expressed here in Hz/minute.
   JT9B-H do not estimate drift and leave has_drift zero. */
typedef struct {
  int32_t has_drift;
  float drift_hz_per_minute;
} decoder_jt9_result;

/* No-sync and synchronization-only observations have no message. width_profile
   is a zero-based profile index, meaningful for decoded messages only.
   average_count is zero for individual messages. */
typedef struct {
  int32_t kind, method, average_count, sync_polarity, width_profile;
} decoder_jt4_result;

/* Synchronization-only observations have no message. Width is available only
   for VHF processing. Drift describes the estimated frequency change across
   the AFC input span, not a waveform reconstruction parameter. */
typedef struct {
  int32_t kind, method, average_count, sync_polarity, smoothing;
  int32_t has_width, has_drift;
  float width_hz, drift_hz;
} decoder_jt65_result;

/* method is the displayed qN qualifier: 0 = no AP, 1 = CQ AP, 2 = own-call AP,
   3 = both-call AP or list decoding, 4 = both calls and RRR AP,
   5 = drift-compensated list decoding. */
typedef struct {
  int32_t period_seconds, method, average_count, recovered_bit78;
} decoder_q65_result;

/* effective_bits is 91 for FST4, or 66/50 for FST4W. Doppler values are
   available only when has_doppler is set. has_hash22 identifies an unresolved
   FST4W callsign; hash22 distinguishes results with the same display text. */
typedef struct {
  int32_t period_seconds, effective_bits, blanker_percent, has_doppler;
  int32_t has_hash22, hash22;
  float fmid_hz, width_hz;
} decoder_fst4_result;

/* dt_seconds retains the mode's operator-facing DT convention. Use the
   evidence's waveform_start_seconds for reconstruction when available.
   mode and variant select the result record; inactive records are zero.
   JT4 variant is its submode (0..6 for A..G); JT4 has no waveform evidence.
   JT9 variant is its submode (0..7 for A..H); JT9 has no 77-bit evidence.
   JT65 variant is its submode (0..2 for A..C); JT65 has no waveform evidence.
   Q65 variant is its submode (0..5 for A..F); Q65 has no waveform evidence.
   For Q65, ap_type is an alias of q65.method.
   Availability flags and payload_origin govern fields in the active record. */
typedef struct {
  int64_t input_id, analysis_id;
  int32_t attempt_no, mode, variant;
  int32_t snr_db, ap_type;
  float frequency_hz, dt_seconds, sync, quality;
  char message[38];
  decoder_ft8_evidence ft8;
  decoder_ft4_evidence ft4;
  decoder_superfox_evidence superfox;
  decoder_jt9_result jt9;
  decoder_jt65_result jt65;
  decoder_q65_result q65;
  decoder_fst4_result fst4;
  decoder_jt4_result jt4;
} decoder_observation;

typedef struct {
  /* Includes JT4 no-sync/synchronization-only and JT65 synchronization observations. */
  int32_t status, observation_count, retained_count, evidence_dropped;
} decoder_attempt_outcome;

/* One session per process, with lifecycle and decode calls confined to one thread.
   Callbacks are serialized and borrowed until return; copy records to retain them.
   Mutating engine calls during a callback return DECODER_BUSY. */
typedef void (*decoder_observation_callback)(const decoder_observation *, void *);

int32_t decoder_engine_create(const decoder_engine_options *, decoder_engine_handle *);
int32_t decoder_engine_get_capabilities(decoder_engine_handle, decoder_engine_capabilities *);
int32_t decoder_engine_decode(decoder_engine_handle, const decoder_attempt_request *,
                             const decoder_audio_view *, decoder_observation_callback,
                             void *, decoder_attempt_outcome *);
/* Released input IDs may be reused for a new reception.
   Release retains JT4/JT65/Q65 averages and caller knowledge, including FST4W
   call/grid history. Reset clears reception history while
   retaining reusable capacity and plans. Incompatible mode configuration clears
   averages. Hosts clear JT65 averages when changing band or operating context. */
int32_t decoder_engine_release_input(decoder_engine_handle, int64_t input_id);
int32_t decoder_engine_reset_session(decoder_engine_handle);
/* Replaces the copied JT4 deep-search call/grid list; count zero clears it. */
int32_t decoder_engine_set_jt4_calls(decoder_engine_handle, const decoder_jt4_call *, int32_t count);
/* Uses the capacity/query convention of decoder_engine_get_jt65_averages. */
int32_t decoder_engine_get_jt4_averages(decoder_engine_handle, decoder_jt4_average_entry *,
                                     int32_t capacity, int32_t *count);
/* Clear allows the retained reception to contribute again on its next attempt. */
int32_t decoder_engine_clear_jt4_averages(decoder_engine_handle);
/* Replaces the copied deep-search list; count zero clears it. */
int32_t decoder_engine_set_jt65_calls(decoder_engine_handle, const decoder_jt65_call *, int32_t count);
/* Returns the total count, copying up to capacity entries. Insufficient nonzero
   capacity returns DECODER_CAPACITY; capacity zero queries size and returns OK. */
int32_t decoder_engine_get_jt65_averages(decoder_engine_handle, decoder_jt65_average_entry *,
                                       int32_t capacity, int32_t *count);
int32_t decoder_engine_clear_jt65_averages(decoder_engine_handle);
/* Caller records are copied. A zero count clears the history. The getter follows
   the same capacity/query convention as decoder_engine_get_jt65_averages. */
int32_t decoder_engine_set_q65_callers(decoder_engine_handle, const decoder_q65_caller *, int32_t count);
int32_t decoder_engine_get_q65_callers(decoder_engine_handle, decoder_q65_caller *, int32_t capacity, int32_t *count);
/* capacity zero queries snapshot metadata without copying curves. Otherwise both
   arrays receive up to capacity samples; insufficient capacity returns CAPACITY. */
int32_t decoder_engine_get_q65_snapshot(decoder_engine_handle, decoder_q65_snapshot *,
                                       float *instant, float *averaged, int32_t capacity);
/* Clear does not re-add the current reception on a subsequent repeat. */
int32_t decoder_engine_clear_q65_averages(decoder_engine_handle);
/* History is copied; count zero clears it. The getter uses the capacity/query
   convention above. FST4W depth 3 accepts CRC-free results only from this history. */
int32_t decoder_engine_set_fst4w_calls(decoder_engine_handle, const decoder_fst4w_call *, int32_t count);
int32_t decoder_engine_get_fst4w_calls(decoder_engine_handle, decoder_fst4w_call *, int32_t capacity, int32_t *count);
int32_t decoder_engine_destroy(decoder_engine_handle);

#ifdef __cplusplus
}
#endif
#endif
