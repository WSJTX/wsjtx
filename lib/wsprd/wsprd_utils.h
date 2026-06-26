#ifndef WSPRD_UTILS_H
#define WSPRD_UTILS_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "nhash.h"

enum {
    WSPRD_CALLSIGN_SIZE = 13,
    WSPRD_GRID4_SIZE = 5,
    WSPRD_GRID6_SIZE = 7,
    WSPRD_MESSAGE_SIZE = 23,
    WSPRD_HASH_COUNT = 32768
};

void unpack50( signed char *dat, int32_t *n1, int32_t *n2 );

int unpackcall( int32_t ncall, char *call );

int unpackgrid( int32_t ngrid, char *grid);

int unpackpfx( int32_t nprefix, char *call);

void deinterleave(unsigned char *sym);

// used by qsort
int doublecomp(const void* elem1, const void* elem2);
int floatcomp(const void* elem1, const void* elem2);

int unpk_( signed char *message, char* hashtab, char* loctab, char *call_loc_pow, char *callsign);

int wsprd_load_hash_line(char const *line, char *hashtab, char *loctab);

#endif
