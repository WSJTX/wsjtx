/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 This file is part of program wsprd, a detector/demodulator/decoder
 for the Weak Signal Propagation Reporter (WSPR) mode.

 File name: wsprd_decode.h

 wsprd's run over a file, and the WSPR calls wsjt-codec makes, over the
 decoder in wspr_decoder.h. Callers see only the types declared here.

 Copyright 2001-2018, Joe Taylor, K1JT

 Much of the present code is based on work by Steven Franke, K9AN,
 which in turn was based on earlier work by K1JT.

 Copyright 2014-2018, Steven Franke, K9AN
*/

#ifndef WSPRD_DECODE_H
#define WSPRD_DECODE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    WSPRD_DECODE_MAX_RESULTS = 50,   /* decodes kept per call */
    WSPRD_C2_POINTS = 45000          /* 375 Hz complex points a .c2 file holds */
};

/* What wsprd's command-line options set; wsprd_decode_options_init gives
   wsprd's defaults. */
struct wsprd_decode_options {
    int wspr_type;           /* 2 (WSPR-2) or 15 (WSPR-15, -m) */
    int npasses;             /* 3; -B 2; -s 1 */
    int subtraction;         /* 1; -s 0 */
    int quickmode;           /* -q */
    int more_candidates;     /* -d */
    int stackdecoder;        /* -J */
    int verbose;             /* -v: keep dupes */
    unsigned int maxcycles;  /* -C, 10000 */
    int ndepth;              /* -o, -1 = OSD off */
    float fmin, fmax;        /* Hz, -110/110; -w -150/150 */
    float bias;              /* -z, 0.45 */
    double dialfreq_error;   /* -e, Hz: shifts fmin and fmax once per pass */
};

/* wsprd's struct result, plus f1. */
struct wsprd_decode_result {
    char date[7];
    char time[5];
    float sync;
    float snr;
    float dt;
    double freq;             /* MHz: dialfreq + (1500 + f1)/1e6; WSPR-15: dialfreq + (1612.5 + f1/8)/1e6 */
    char message[23];
    float drift;
    unsigned int cycles;
    int jitter;
    int blocksize;
    unsigned int metric;
    int nhardmin;
    int ipass;
    int decodetype;          /* 1 when OSD decoded it */
    float f1;                /* audio frequency less 1500 Hz, so that 1500 + f1 in float is
                                wsprd's; WSPR-15: (audio frequency - 1612.5) * 8 */
};

/* CPU seconds the decode adds per code section (wspr_timer.out's rows). */
struct wsprd_decode_timing {
    float tcandidates, tsync0, tsync1, tsync2, tfano, tosd;
    float tdownsample;       /* wsprd_decode_pcm's 12 kHz to 375 Hz step */
};

struct wsprd_decode_hooks {
    /* When set, called at the end of the first pass with the I/Q that pass's
       subtraction left (wsprd -c). */
    void (*after_first_pass)(float const *idat, float const *qdat, void *user);
    void *user;                         /* passed to after_first_pass */
    struct wsprd_decode_timing *timing; /* when set, the decode adds to it */
};

/* Runs fn(arg) once and returns 0, or returns nonzero without running it,
   which counts as a planning failure. */
typedef int (*wsprd_plan_guard)(void (*fn)(void *), void *arg);

/* Owned by the caller: the decoder's workspace and its hash and locator
   tables, which carry calls from one decode to the next. One decode at a
   time per process: osdwspr keeps state of its own. */
struct wsprd_context;

void wsprd_decode_options_init(struct wsprd_decode_options *options);

/* With plan_guard set, every FFTW plan the context makes or destroys runs
   through it; NULL calls FFTW directly. NULL when memory or planning fails. */
struct wsprd_context *wsprd_context_new(wsprd_plan_guard plan_guard);
void wsprd_context_free(struct wsprd_context *ctx);

void wsprd_hash_clear(struct wsprd_context *ctx);
/* Adds one hashtable.txt line: 0, or -1 when it is not an entry. */
int wsprd_hash_load_line(struct wsprd_context *ctx, char const *line);
/* Adds every line of a hashtable.txt file. Returns 0, or -1 when the file
   cannot be opened. */
int wsprd_hash_load(struct wsprd_context *ctx, char const *path);
/* Writes the table in hashtable.txt's format. Returns 0, or -1 when the file
   cannot be opened. */
int wsprd_hash_save(struct wsprd_context *ctx, char const *path);

/* The number of 12 kHz samples a period holds for wspr_type (2 or 15), the
   most wsprd reads from a .wav file, or 0 for any other type. */
size_t wsprd_downsample_points(int wspr_type);

/* Decodes nsamples 12 kHz samples, at most wsprd_downsample_points of the
   options' type; the rest of the period counts as zero. dialfreq (MHz, after
   any dial error correction) only offsets freq. date is YYMMDD and uttime
   HHMM. Fills up to WSPRD_DECODE_MAX_RESULTS results sorted by freq and
   returns their count, or -1 on failure (with a message on stderr). hooks
   may be NULL. */
int wsprd_decode_pcm(struct wsprd_context *ctx,
                     struct wsprd_decode_options const *options,
                     struct wsprd_decode_hooks const *hooks,
                     short const *samples, size_t nsamples,
                     double dialfreq, char const *date, char const *uttime,
                     struct wsprd_decode_result *results);

/* As wsprd_decode_pcm, on the WSPRD_C2_POINTS points of 375 Hz I/Q a .c2
   file holds (wsprd_read_c2). */
int wsprd_decode_iq(struct wsprd_context *ctx,
                    struct wsprd_decode_options const *options,
                    struct wsprd_decode_hooks const *hooks,
                    float const *idat, float const *qdat,
                    double dialfreq, char const *date, char const *uttime,
                    struct wsprd_decode_result *results);

/* readc2file: reads a .c2 file's WSPRD_C2_POINTS points into idat and qdat
   (WSPRD_C2_POINTS floats each), with its dial frequency (MHz) and type.
   Returns WSPRD_C2_POINTS, or 1 on error. */
unsigned long wsprd_read_c2(char const *path, float *idat, float *qdat,
                            double *freq, int *wspr_type);

/* Writes a .c2 file as wsprd -c does: the file name's first 14 bytes, trmin,
   freq (MHz) and WSPRD_C2_POINTS I/Q points. Returns 90000, or 0 on error. */
unsigned long wsprd_write_c2(char const *c2filename, int trmin, double freq,
                             float const *idat, float const *qdat);

/* wsprd's run over one file; wsprd_run_args_init gives wsprd's defaults. */
struct wsprd_run_args {
    struct wsprd_decode_options options;
    char const *data_dir;       /* -a: side files; NULL means "." */
    char const *infile;         /* .wav or .c2, named ...YYMMDD_HHMM.<suffix> */
    double dialfreq_cmdline;    /* -f, MHz; a .c2 file carries its own */
    int writec2;                /* -c: 000000_0001.c2 in the working directory */
    int usehashtable;           /* 1; -H 0 */
    int wisdom;                 /* 1: import and export <data_dir>/wspr_wisdom.dat */
    int timer;                  /* 1: read and write <data_dir>/wspr_timer.out */
    wsprd_plan_guard plan_guard; /* as for wsprd_context_new */
    int decode_finished;        /* 1: print <DecodeFinished>, as wsprd always does */
};

void wsprd_run_args_init(struct wsprd_run_args *args);

/* 0 when dir is a directory the run can read, write and search (no search
   check on Windows), else -1. */
int wsprd_data_dir_ok(char const *dir);

/* Does what wsprd does for a file after parsing its options, in its order:
   checks data_dir, imports the FFTW wisdom, opens ALL_WSPR.TXT (append),
   wspr_spots.txt (truncate) and wspr_timer.out, reads the input, loads
   hashtable.txt, decodes, prints wsprd's stdout lines and <DecodeFinished>,
   appends to ALL_WSPR.TXT and wspr_spots.txt, then writes the wisdom,
   wspr_timer.out and hashtable.txt. The date and time come from the 11 and 4
   bytes before the name's suffix; bytes before a shorter name read as NUL.
   A side file that cannot be opened goes unwritten. Every message is wsprd's.
   Returns wsprd's exit code. */
int wsprd_run_file(struct wsprd_run_args const *args);

#ifdef __cplusplus
}
#endif

#endif
