/*
 This file is part of program wsprd, a detector/demodulator/decoder
 for the Weak Signal Propagation Reporter (WSPR) mode.
 
 File name: wsprd.c
 
 Copyright 2001-2018, Joe Taylor, K1JT
 
 Much of the present code is based on work by Steven Franke, K9AN,
 which in turn was based on earlier work by K1JT.
 
 Copyright 2014-2018, Steven Franke, K9AN
 
 License: GNU GPL v3
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "wsprd_decode.h"

//***************************************************************************
void usage(void)
{
    printf("Usage: wsprd [options...] infile\n");
    printf("       infile must have suffix .wav or .c2\n");
    printf("\n");
    printf("Options:\n");
    printf("       -a <path> path to writeable data files, default=\".\"\n");
    printf("       -B disable block demodulation - use single-symbol noncoherent demod\n");
    printf("       -c write .c2 file at the end of the first pass\n");
    printf("       -C maximum number of decoder cycles per bit, default 10000\n");
    printf("       -d deeper search. Slower, a few more decodes\n");
    printf("       -e x (x is transceiver dial frequency error in Hz)\n");
    printf("       -f x (x is transceiver dial frequency in MHz)\n");
    printf("       -H do not use (or update) the hash table\n");
    printf("       -J use the stack decoder instead of Fano decoder\n");
    printf("       -m decode wspr-15 .wav file\n");
    printf("       -o n (0<=n<=5), decoding depth for OSD, default is disabled\n");
    printf("       -q quick mode - doesn't dig deep for weak signals\n");
    printf("       -s single pass mode, no subtraction (same as original wsprd)\n");
    printf("       -v verbose mode (shows dupes)\n");
    printf("       -w wideband mode - decode signals within +/- 150 Hz of center\n");
    printf("       -z x (x is fano metric table bias, default is 0.45)\n");
}

//***************************************************************************
int main(int argc, char *argv[])
{
    char cr[] = "(C) 2018, Steven Franke - K9AN";
    (void)cr;
    extern char *optarg;
    extern int optind;
    int c;
    struct wsprd_run_args args;
    
    wsprd_run_args_init(&args);
    
    while ( (c = getopt(argc, argv, "a:BcC:de:f:HJmo:qstwvz:")) !=-1 ) {
        switch (c) {
            case 'a':
                args.data_dir = optarg;
                break;
            case 'B':
                args.options.npasses=2;
                break;
            case 'c':
                args.writec2=1;
                break;
            case 'C':
                args.options.maxcycles=(unsigned int) strtoul(optarg,NULL,10);
                break;
            case 'd':
                args.options.more_candidates=1;
                break;
            case 'e':
                args.options.dialfreq_error = strtod(optarg,NULL);   // units of Hz
                // dialfreq_error = dial reading - actual, correct frequency
                break;
            case 'f':
                args.dialfreq_cmdline = strtod(optarg,NULL); // units of MHz
                break;
            case 'H':
                args.usehashtable = 0;
                break;
            case 'J': //Stack (Jelinek) decoder, Fano decoder is the default
                args.options.stackdecoder = 1;
                break;
            case 'm':  //15-minute wspr mode
                args.options.wspr_type = 15;
                break;
            case 'o':  //use ordered-statistics-decoder
                args.options.ndepth=(int) strtol(optarg,NULL,10);
                break;
            case 'q':  //no shift jittering
                args.options.quickmode = 1;
                break;
            case 's':  //single pass mode
                args.options.subtraction = 0;
                args.options.npasses = 1;
                break;
            case 'v':
                args.options.verbose = 1;
                break;
            case 'w':
                args.options.fmin=-150.0;
                args.options.fmax=150.0;
                break;
            case 'z':
                args.options.bias=strtod(optarg,NULL); //fano metric bias (default is 0.45)
                break;
            case '?':
                usage();
                return 1;
        }
    }
    
    // wsprd_run_file checks again; here the usage text follows the message.
    if( wsprd_data_dir_ok(args.data_dir) ) {
      fprintf(stderr, "Error: inaccessible data directory: '%s'\n", args.data_dir);
      usage();
      return EXIT_FAILURE;
    }

    if( optind+1 > argc) {
        usage();
        return 1;
    } else {
        args.infile=argv[optind];
    }
    
    return wsprd_run_file(&args);
}
