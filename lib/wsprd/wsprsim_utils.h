#ifndef WSPRSIM_UTILS_H
#define WSPRSIM_UTILS_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

int get_wspr_channel_symbols_verbose(char *, char *, char *, unsigned char *, int);

int get_wspr_channel_symbols(char* message, char* hashtab, char*loctab, unsigned char* symbols);

/* The channel symbols for an 11-byte packed message (data with tail). */
void wspr_channel_symbols_from_data(unsigned char const *data, unsigned char *symbols);

#endif
