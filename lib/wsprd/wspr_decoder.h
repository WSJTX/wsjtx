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
    /* Threads for the candidate loops, the decode attempts and subtraction's
       filter, by ft8_threads' rule (lib/wsprd/wspr_parallel.f90): 0 picks
       from the CPU count. One where the Fortran has no OpenMP. Decodes are
       the same whatever the count. */
    int threads;
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
/* osdwspr's pattern boxes (lib/wsprd/osdwspr.f90): its indexes(4000,2),
   fp(0:525000) and np(4000) in one block of ints, which it reads at ndepth
   WSPR_OSD_BOX_DEPTH and up. wspr_decoder_run allocates each thread's
   before it decodes, and without them returns DECODER_INTERNAL_ERROR with
   the hash table as it was, as for its other workspaces. */
enum { WSPR_OSD_BOX_INTS = 8000 + 525001 + 4000, WSPR_OSD_BOX_DEPTH = 3 };
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
/* The most threads any of the last wspr_decoder_run's loops ran on. */
int wspr_decoder_threads_ran(const wspr_decoder *);
int wspr_decoder_hash_entry(wspr_decoder *, int, const char **, const char **);

#endif
