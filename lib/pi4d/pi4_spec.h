/*
 This file is part of program pi4d, a decoder for PI4, the 4-FSK
 propagation-beacon mode used by the IARU Region 1 "Next Generation Beacon"
 network (see https://www.rudius.net/oz2m/ngnb/pi4_.htm).

 File name: pi4_spec.h

 Description: the PI4 protocol itself - the message alphabet and its
 base-38 packing, the fixed 146-bit sync vector, the bit-reversal
 interleaver, and the four beacon channel-spacing variants. PI4 is
 explicitly "based on JT4": its rate-1/2 K=32 convolutional code is the
 same Layland-Lushbaugh pair (POLY1=0xF2D05351, POLY2=0xE4613C47) that
 WSPR and JT9 already use, so encode()/fano() from lib/wsprd/fano.c are
 reused directly here rather than re-implemented - only the message
 length differs (42 info bits here against WSPR's 50).

 Copyright 2026, Dawid SQ6EMM

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

#ifndef PI4_SPEC_H
#define PI4_SPEC_H

#include <stdint.h>

/* Symbols in one transmission: 146, at 166.667 ms each = 24.333 s. */
#define PI4_N_SYMBOLS 146

/* Information bits: eight characters from a 38-symbol alphabet packed as
   one base-38 integer (38^8 fits in 42 bits). */
#define PI4_INFO_BITS 42

/* Bits the Fano decoder runs over: 42 message bits + the 31-bit zero tail
   the K=32 code needs to flush. */
#define PI4_NBITS (PI4_INFO_BITS + 31)

/* Coded bits leaving the convolutional encoder: 2*PI4_NBITS, one per
   transmitted symbol. Must equal PI4_N_SYMBOLS. */
#define PI4_CODED_BITS (2 * PI4_NBITS)

/* Bytes needed to hold PI4_NBITS bits, MSB-first, tail left zero - the
   shape encode()/fano() (lib/wsprd/fano.h) expect. */
#define PI4_NBITS_BYTES ((PI4_NBITS + 7) / 8)

/* Characters a PI4 message may use, in the order the protocol numbers
   them: digits, then capital letters, then space and '/'. */
extern const char pi4_charset[38];

/* The fixed 146-bit pseudorandom synchronisation word (Klaus DJ5HG).
   Bit n is the low bit of transmitted symbol n:
   Symbol[n] = Sync[n] + 2*Data[n]. One byte (0 or 1) per bit. */
extern const unsigned char pi4_sync[PI4_N_SYMBOLS];

typedef enum {
  PI4_VARIANT_PI4 = 0,
  PI4_VARIANT_PI4_80,
  PI4_VARIANT_PI4_96,
  PI4_VARIANT_PI4_120,
  PI4_VARIANT_COUNT
} pi4_variant_t;

extern const pi4_variant_t pi4_variant_all[PI4_VARIANT_COUNT];

const char *pi4_variant_label (pi4_variant_t v);

/* Spacing between adjacent tones, Hz: K*12000/2048. */
float pi4_variant_tone_spacing_hz (pi4_variant_t v);

/* Tone 0's audio frequency under the beacon network's own listening
   convention: dial tuned so the CW ID sits at 800 Hz, tone 0 half a
   tone-spacing below that. */
float pi4_variant_conventional_tone0_hz (pi4_variant_t v);

/* A character's value in the 38-symbol alphabet, or -1 outside it.
   Lower-case letters are folded to upper. */
int pi4_value_of (char c);

/* The character a value from 0..37 stands for. */
char pi4_char_of (int v);

/* Pack up to eight characters into the 42-bit N the protocol builds by
   N = char0; N = N*38 + char1; ...; N = N*38 + char7, space-padding a
   shorter message on the right. Returns 0 and sets *n_out on success,
   -1 if text is too long or uses a character outside the charset. */
int pi4_pack_message (const char *text, uint64_t *n_out);

/* The message n packs, trimmed of trailing space padding. buf must have
   room for 9 bytes (8 chars + NUL). n must be < 38^8. */
void pi4_unpack_message (uint64_t n, char *buf);

/* The 42-bit n as PI4_NBITS MSB-first bits packed into PI4_NBITS_BYTES
   bytes, with the 31-bit tail (and any padding past PI4_NBITS) left
   zero - ready for encode()/fano() from lib/wsprd/fano.h. */
void pi4_pack_info_bits (uint64_t n, unsigned char *out);

/* The inverse of pi4_pack_info_bits over PI4_NBITS_BYTES of recovered
   message bits: the leading PI4_INFO_BITS bits (MSB-first) as the
   42-bit n, dropping the 31-bit tail. */
uint64_t pi4_unpack_info_bits (const unsigned char *bits);

/* Undo the transmitted bit-reversal interleave: given PI4_N_SYMBOLS
   values indexed by *transmission* order (channel position n), write
   them reordered into *codeword* order (out). One element per array
   entry (e.g. a 0/1 bit, or a soft LLR represented as a float - see the
   _f variant below). */
void pi4_deinterleave_u8 (const unsigned char *channel_order, unsigned char *out);
void pi4_deinterleave_f (const float *channel_order, float *out);

/* The forward interleave - codeword order to transmission (channel)
   order. Only needed for the encode side (fit-checking a candidate
   decode, and the self-test's synthetic transmissions). */
void pi4_interleave_to_channel_u8 (const unsigned char *codeword_order, unsigned char *out);

/* Convolutionally encode a 42-bit message value n into its
   PI4_CODED_BITS coded bits (one byte per bit, 0 or 1), in codeword
   (pre-interleave) order, using encode() from lib/wsprd/fano.h. */
void pi4_encode_coded_bits (uint64_t n, unsigned char *coded);

/* The PI4_N_SYMBOLS transmitted symbols (tone indices 0..3) a message
   value n produces: convolutional encode, interleave, merge with
   pi4_sync - the spec's PI4MakeSymbols in full. */
void pi4_encode_symbols_for (uint64_t n, unsigned char *symbols);

#endif
