// SPDX-License-Identifier: GPL-3.0-or-later
/* stream_wspr.c — the stream's WSPR decode (lib/streaming_wspr.f90): wsprd's
 * decode library (lib/wsprd/wsprd_decode.h) on a period's 12 kHz samples,
 * with the session's hash table, and each decode's numbers as wsprd's lines
 * print them.
 *
 * Runs on the decode thread, or inline without one, one call at a time: the
 * context and its hash table are this file's, and only that thread touches
 * them. The context's FFTW plans and destroys go through wsjt_fftw_locked
 * (lib/fftw_locked.f90), the lock four2a takes, because the frame loop plans
 * spectrum rows while a WSPR decode runs.
 *
 * Linked into wsjt-codec only, as its own object: its references to
 * wsjt_fftw_locked and to the decoder under wsprd_decode (wsjt_wspr's objects),
 * both in libwsjt_fort_omp.a or libwsjt_fort.a, resolve from archives that
 * follow it on any link line.
 * Called via bind(C) from Fortran.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wsprd/wsprd_decode.h"

int wsjt_fftw_locked(void (*fn)(void *), void *arg);

/* One decode, the fields of the codec's WSPR decode event. */
struct stream_wspr_decode {
    int snr;              /* lrintf(snr): what wsprd's %3.0f prints, as an integer */
    int freq;             /* lrintf(1500 + f1), Hz */
    int drift;            /* (int)drift, as wsprd prints it */
    int sync10;           /* (int)(10*sync), wspr_spots.txt's column */
    int pass;             /* ipass + 1 */
    int blocksize;
    int jitter;
    int osd;              /* decodetype */
    int hard_errors;      /* nhardmin */
    int cycles;           /* cycles/81 */
    int metric;           /* the unsigned metric as %5d prints it */
    float dt;
    float snr_real;
    float sync;
    char audio_freq[32];  /* the float 1500 + f1, exact in decimal */
    char message[24];
};

static struct wsprd_context *context;

/* <data_dir>/name in path (capacity bytes); 0, or -1 when it does not fit. */
static int data_path(char *path, size_t capacity, char const *data_dir,
                     int data_dir_len, char const *name)
{
    int written = snprintf(path, capacity, "%.*s/%s", data_dir_len, data_dir, name);
    return written < 0 || (size_t)written >= capacity ? -1 : 0;
}

/* The GUI's wsprd options for its depth, m_ndepth&7 (MainWindow::decode):
 * 1 -qB, 2 -C 500 -o 4, 3 -C 500 -o 4 -d, anything else none. */
static void depth_options(int ndepth, struct wsprd_decode_options *options)
{
    wsprd_decode_options_init(options);
    switch (ndepth & 7) {
    case 1:
        options->quickmode = 1;
        options->npasses = 2;
        break;
    case 3:
        options->more_candidates = 1;
        /* fall through */
    case 2:
        options->maxcycles = 500;
        options->ndepth = 4;
        break;
    default:
        break;
    }
}

/* The float 1500 + f1 as a JSON number a double parse reads exactly: it has
 * 24 significant bits, below 2^11, so %.17g prints its whole expansion. */
static void exact_text(float value, char *text, size_t capacity)
{
    snprintf(text, capacity, "%.17g", (double)value);
    if (!strchr(text, '.')) strncat(text, ".0", capacity - strlen(text) - 1);
}

/* Decodes the period's first k samples (wsprd reads the first 1,368,000, and
 * zeros past k) at the GUI's depth, into out (WSPRD_DECODE_MAX_RESULTS) in
 * wsprd's order, ascending frequency. The hash table is loaded from
 * <data_dir>/hashtable.txt before the session's first decode and saved there
 * after every one. Returns the decode count, or -1 on failure, with a
 * message on stderr. */
int stream_wspr_decode(short const *samples, int k, int ndepth,
                       char const *data_dir, int data_dir_len,
                       struct stream_wspr_decode *out)
{
    struct wsprd_decode_options options;
    struct wsprd_decode_result results[WSPRD_DECODE_MAX_RESULTS];
    char path[1024];
    size_t take;
    int n, i;

    if (data_path(path, sizeof path, data_dir, data_dir_len, "hashtable.txt")) {
        fprintf(stderr, "wsjt-codec: the WSPR hash table's path is too long\n");
        return -1;
    }
    if (!context) {
        context = wsprd_context_new(wsjt_fftw_locked);
        if (!context) {
            fprintf(stderr, "wsjt-codec: no memory for the WSPR decoder\n");
            return -1;
        }
        wsprd_hash_load(context, path);
    }
    take = wsprd_downsample_points(2);
    if (k < 0) k = 0;
    if ((size_t)k < take) take = (size_t)k;
    depth_options(ndepth, &options);
    /* Dial 0: the client adds its own; the date and time label nothing here. */
    n = wsprd_decode_pcm(context, &options, NULL, samples, take,
                         0.0, "000000", "0000", results);
    if (n < 0) return -1;
    for (i = 0; i < n; i++) {
        struct wsprd_decode_result const *r = &results[i];
        struct stream_wspr_decode *d = &out[i];
        float audio = 1500 + r->f1;
        memset(d, 0, sizeof *d);
        d->snr = (int)lrintf(r->snr);
        d->freq = (int)lrintf(audio);
        d->drift = (int)r->drift;
        d->sync10 = (int)(10 * r->sync);
        d->pass = r->ipass + 1;
        d->blocksize = r->blocksize;
        d->jitter = r->jitter;
        d->osd = r->decodetype;
        d->hard_errors = r->nhardmin;
        d->cycles = (int)(r->cycles / 81);
        d->metric = (int)r->metric;
        d->dt = r->dt;
        d->snr_real = r->snr;
        d->sync = r->sync;
        exact_text(audio, d->audio_freq, sizeof d->audio_freq);
        strncpy(d->message, r->message, sizeof d->message - 1);
    }
    if (wsprd_hash_save(context, path))
        fprintf(stderr, "wsjt-codec: cannot write %s\n", path);
    return n;
}

/* The GUI's Erase WSPR hashtable: the session's table empties and
 * <data_dir>/hashtable.txt goes. */
void stream_wspr_hash_clear(char const *data_dir, int data_dir_len)
{
    char path[1024];
    if (context) wsprd_hash_clear(context);
    if (data_path(path, sizeof path, data_dir, data_dir_len, "hashtable.txt") == 0)
        remove(path);
}
