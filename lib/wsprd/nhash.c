#include "nhash.h"

#include "../lookup3.h"

uint32_t nhash(const void *key, size_t length, uint32_t initval)
{
  /* WSPR table indices use 15 bits; empty keys historically bypass the mask. */
  uint32_t hash = wsjt_hashlittle(key, length, initval);
  return length == 0 ? hash : hash & (uint32_t)0x7fff;
}
