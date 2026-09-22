/*
 This file is part of program pi4d - see pi4_spec.h for a description
 and license.

 File name: pi4_spec.c
*/

#include <string.h>

#include "pi4_spec.h"
#include "../wsprd/fano.h"

const char pi4_charset[38] = {
  '0','1','2','3','4','5','6','7','8','9',
  'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
  ' ','/'
};

/* Bit n is the low bit of transmitted symbol n; contributed by Klaus
   DJ5HG for its auto-correlation properties. Checked bit-for-bit
   against the protocol page's own OZ7IGY worked example - see
   pi4d_selftest.c. */
const unsigned char pi4_sync[PI4_N_SYMBOLS] = {
  0,0,1,0,0,1,1,1,1,0,1,0,1,0,1,0,0,1,0,0,0,1,0,0,0,1,1,0,0,1,
  1,1,1,0,0,1,1,1,1,1,0,0,1,1,0,1,1,1,1,0,1,0,1,1,0,1,1,0,1,0,
  0,0,0,0,1,1,1,1,1,0,1,0,1,0,0,0,0,0,1,1,1,1,1,0,1,0,0,1,0,0,
  1,0,1,0,0,0,0,1,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,1,1,1,
  0,1,1,1,0,1,1,0,1,0,1,0,1,0,0,0,0,1,1,1,0,0,0,0,1,1,
};

const pi4_variant_t pi4_variant_all[PI4_VARIANT_COUNT] = {
  PI4_VARIANT_PI4, PI4_VARIANT_PI4_80, PI4_VARIANT_PI4_96, PI4_VARIANT_PI4_120
};

static unsigned int variant_k (pi4_variant_t v)
{
  switch (v) {
  case PI4_VARIANT_PI4:     return 40;
  case PI4_VARIANT_PI4_80:  return 80;
  case PI4_VARIANT_PI4_96:  return 96;
  case PI4_VARIANT_PI4_120: return 120;
  default:                  return 40;
  }
}

const char *pi4_variant_label (pi4_variant_t v)
{
  switch (v) {
  case PI4_VARIANT_PI4:     return "PI4";
  case PI4_VARIANT_PI4_80:  return "PI4-80";
  case PI4_VARIANT_PI4_96:  return "PI4-96";
  case PI4_VARIANT_PI4_120: return "PI4-120";
  default:                  return "PI4";
  }
}

float pi4_variant_tone_spacing_hz (pi4_variant_t v)
{
  return variant_k (v) * 12000.0f / 2048.0f;
}

float pi4_variant_conventional_tone0_hz (pi4_variant_t v)
{
  return 800.0f - 0.5f * pi4_variant_tone_spacing_hz (v);
}

int pi4_value_of (char c)
{
  int i;
  if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
  for (i = 0; i < 38; i++) {
    if (pi4_charset[i] == c) return i;
  }
  return -1;
}

char pi4_char_of (int v)
{
  return pi4_charset[v];
}

int pi4_pack_message (const char *text, uint64_t *n_out)
{
  size_t len = strlen (text);
  uint64_t n = 0;
  int i;
  if (len > 8) return -1;
  for (i = 0; i < 8; i++) {
    int v;
    if ((size_t) i < len) {
      v = pi4_value_of (text[i]);
      if (v < 0) return -1;
    } else {
      v = 36; /* space */
    }
    n = n * 38 + (uint64_t) v;
  }
  *n_out = n;
  return 0;
}

void pi4_unpack_message (uint64_t n, char *buf)
{
  char chars[8];
  int i, end;
  for (i = 7; i >= 0; i--) {
    chars[i] = pi4_char_of ((int) (n % 38));
    n /= 38;
  }
  end = 8;
  while (end > 0 && chars[end - 1] == ' ') end--;
  memcpy (buf, chars, (size_t) end);
  buf[end] = '\0';
}

void pi4_pack_info_bits (uint64_t n, unsigned char *out)
{
  int i;
  memset (out, 0, PI4_NBITS_BYTES);
  for (i = 0; i < PI4_INFO_BITS; i++) {
    unsigned int bit = (unsigned int) ((n >> (PI4_INFO_BITS - 1 - i)) & 1);
    if (bit) out[i / 8] |= (unsigned char) (1u << (7 - (i % 8)));
  }
}

uint64_t pi4_unpack_info_bits (const unsigned char *bits)
{
  uint64_t n = 0;
  int i;
  for (i = 0; i < PI4_INFO_BITS; i++) {
    unsigned int bit = (unsigned int) ((bits[i / 8] >> (7 - (i % 8))) & 1);
    n = (n << 1) | bit;
  }
  return n;
}

/* Forward map (spec pseudocode, and the reference C PI4MakeSymbols):
   scan i from 0 to 255, bit-reverse it over 8 bits to get j; whenever
   j < N_SYMBOLS, the next sequential codeword bit is written to
   transmitted position j. */
static void bit_reverse_scan (int *positions /* PI4_N_SYMBOLS ints, codeword index p -> channel index j */)
{
  int p = 0;
  int i;
  for (i = 0; i < 256 && p < PI4_N_SYMBOLS; i++) {
    unsigned char b = (unsigned char) i;
    unsigned char j = (unsigned char)
      (((b & 0x01) << 7) | ((b & 0x02) << 5) | ((b & 0x04) << 3) | ((b & 0x08) << 1) |
       ((b & 0x10) >> 1) | ((b & 0x20) >> 3) | ((b & 0x40) >> 5) | ((b & 0x80) >> 7));
    if (j < PI4_N_SYMBOLS) {
      positions[p] = j;
      p++;
    }
  }
}

void pi4_deinterleave_u8 (const unsigned char *channel_order, unsigned char *out)
{
  int positions[PI4_N_SYMBOLS];
  int p;
  bit_reverse_scan (positions);
  for (p = 0; p < PI4_N_SYMBOLS; p++) out[p] = channel_order[positions[p]];
}

void pi4_deinterleave_f (const float *channel_order, float *out)
{
  int positions[PI4_N_SYMBOLS];
  int p;
  bit_reverse_scan (positions);
  for (p = 0; p < PI4_N_SYMBOLS; p++) out[p] = channel_order[positions[p]];
}

void pi4_interleave_to_channel_u8 (const unsigned char *codeword_order, unsigned char *out)
{
  int positions[PI4_N_SYMBOLS];
  int p;
  bit_reverse_scan (positions);
  for (p = 0; p < PI4_N_SYMBOLS; p++) out[positions[p]] = codeword_order[p];
}

void pi4_encode_coded_bits (uint64_t n, unsigned char *coded)
{
  unsigned char info[PI4_NBITS_BYTES];
  unsigned char full[2 * PI4_NBITS_BYTES * 8];
  pi4_pack_info_bits (n, info);
  encode (full, info, PI4_NBITS_BYTES);
  memcpy (coded, full, PI4_CODED_BITS);
}

void pi4_encode_symbols_for (uint64_t n, unsigned char *symbols)
{
  unsigned char coded[PI4_CODED_BITS];
  unsigned char interleaved[PI4_N_SYMBOLS];
  int i;
  pi4_encode_coded_bits (n, coded);
  pi4_interleave_to_channel_u8 (coded, interleaved);
  for (i = 0; i < PI4_N_SYMBOLS; i++) {
    symbols[i] = (unsigned char) (pi4_sync[i] | (interleaved[i] << 1));
  }
}
