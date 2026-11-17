/* SPDX-License-Identifier: GPL-3.0-or-later */
/* wsjt-codec -2: one WSPR .wav or .c2 file through wsprd's run over a file
   (lib/wsprd/wsprd_decode.h), with the GUI's wsprd options for the depth.
   jt9 links lib/jt9_wspr_stub.f90 in its place. */

#include <stdio.h>

#include "wsprd/wsprd_decode.h"

/* lib/fftw_locked.f90 */
int wsjt_fftw_locked(void (*fn)(void *), void *arg);

/* -2's data directory, checked before the first file as wsprd checks it:
   0, or 1 after wsprd's message. */
int wsjt_codec_wspr_data_dir(char const *data_dir)
{
    if (wsprd_data_dir_ok(data_dir) == 0) return 0;
    fprintf(stderr, "Error: inaccessible data directory: '%s'\n", data_dir);
    return 1;
}

int wsjt_codec_wspr_file(char const *infile, char const *data_dir,
                         double dial_mhz, int depth, int decode_finished)
{
    struct wsprd_run_args args;
    int rc;

    wsprd_run_args_init(&args);
    /* widgets/mainwindow.cpp: (m_ndepth&7) 1 -qB, 2 -C 500 -o 4,
       3 -C 500 -o 4 -d, anything else no options. */
    switch (depth & 7) {
    case 1:
        args.options.quickmode = 1;
        args.options.npasses = 2;
        break;
    case 2:
        args.options.maxcycles = 500;
        args.options.ndepth = 4;
        break;
    case 3:
        args.options.maxcycles = 500;
        args.options.ndepth = 4;
        args.options.more_candidates = 1;
        break;
    default:
        break;
    }
    args.data_dir = data_dir;
    args.dialfreq_cmdline = dial_mhz;
    args.usehashtable = 1;
    args.writec2 = 0;
    args.wisdom = 0;
    args.timer = 0;
    args.plan_guard = wsjt_fftw_locked;
    args.decode_finished = decode_finished;

    /* A name shorter than wsprd's date and time reads NULs for them. */
    args.infile = infile;
    rc = wsprd_run_file(&args);
    fflush(stdout);
    return rc;
}
