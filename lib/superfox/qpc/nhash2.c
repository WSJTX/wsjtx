#include "nhash2.h"

#include "../../lookup3.h"

uint32_t nhash2(const void *key, uint64_t length, uint32_t initval)
{
  /* SuperFox applies its 21-bit CRC mask after hashing. */
  return wsjt_hashlittle(key, (size_t)length, initval);
}
