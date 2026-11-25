#include "wsprsim_utils.h"
#include "fano.h"

#include <string.h>

static void interleave(unsigned char *sym)
{
    unsigned char tmp[162];
    unsigned char p, i, j;
    
    p=0;
    i=0;
    while (p<162) {
        j=((i * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
        if (j < 162 ) {
            tmp[j]=sym[p];
            p=p+1;
        }
        i=i+1;
    }
    for (i=0; i<162; i++) {
        sym[i]=tmp[i];
    }
}

static const unsigned char pr3[162]=
    {1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
        0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
        0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
        1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
        0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
        0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
        0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
        0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
        0,0};

/* The channel symbols that carry an 11-byte packed message, tail included:
   the convolutional code, interleaved, with the sync vector. */
void wspr_channel_symbols_from_data(unsigned char const *data, unsigned char *symbols)
{
    unsigned int i, nbytes=11; // The message with tail is packed into almost 11 bytes.
    unsigned char packed[11], channelbits[11*8*2]; /* 162 rounded up */
    memcpy(packed,data,sizeof packed);
    memset(channelbits,0,sizeof channelbits);
    encode(channelbits,packed,nbytes);
    interleave(channelbits);
    for (i=0; i<162; i++) {
        symbols[i]=2*channelbits[i]+pr3[i];
    }
}
