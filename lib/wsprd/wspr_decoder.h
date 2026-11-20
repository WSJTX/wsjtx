#ifndef WSPR_DECODER_H
#define WSPR_DECODER_H

#include "../../DecoderEngine.h"
#include <stddef.h>

typedef struct wspr_decoder wspr_decoder;

int32_t wspr_decoder_create(wspr_decoder **);
void wspr_decoder_destroy(wspr_decoder *);
void wspr_decoder_release(wspr_decoder *);
void wspr_decoder_reset(wspr_decoder *);
int32_t wspr_decoder_prepare_pcm(wspr_decoder *, const int16_t *, int32_t, int32_t, int32_t);
int32_t wspr_decoder_decode(wspr_decoder *, const decoder_wspr_options *,
                            decoder_observation_callback, void *, int32_t *);
int32_t wspr_decoder_set_calls(wspr_decoder *, const decoder_wspr_call *, int32_t);
int32_t wspr_decoder_get_calls(wspr_decoder *, decoder_wspr_call *, int32_t, int32_t *);

/* Private host compatibility interfaces; none perform I/O. */
struct wspr_tuning {
    unsigned int maxcycles;
    int quickmode, more_candidates, stackdecoder, subtraction, npasses, ndepth, verbose;
    float bias;
    double calibration_error_hz;
};
struct wspr_diagnostic {
    decoder_observation observation;
    char message[23];
    double audio_frequency_hz;
    float drift;
    unsigned int cycles, metric;
    int jitter, blocksize, nhardmin, ipass, decodetype;
};
struct wspr_timing {
    float candidates, sync0, sync1, sync2, fano, osd;
};
typedef void (*wspr_residual_callback)(const float *, const float *, void *);
void wspr_decoder_default_tuning(struct wspr_tuning *);
/* wspr_decoder_run writes up to this many results. */
enum { WSPR_DECODER_MAX_RESULTS = 50 };
/* Runs fn(arg) and returns 0, or returns nonzero without running it. */
typedef int (*wspr_plan_guard)(void (*fn)(void *), void *arg);
/* wspr_decoder_create, with every FFTW plan the decoder makes or destroys run
   through guard, for hosts whose other threads plan transforms. */
int32_t wspr_decoder_create_guarded(wspr_decoder **, wspr_plan_guard);
int32_t wspr_decoder_prepare_iq(wspr_decoder *, const float *, const float *, int32_t, int32_t);
int32_t wspr_decoder_run(wspr_decoder *, const decoder_wspr_options *, const struct wspr_tuning *,
                         struct wspr_diagnostic *, int32_t *, struct wspr_timing *,
                         wspr_residual_callback, void *);
int32_t wspr_decoder_import_hash_line(wspr_decoder *, const char *);
int wspr_decoder_hash_entry(wspr_decoder *, int, const char **, const char **);

#endif
