#include <stddef.h>
#include <stdint.h>

#include "../lookup3.h"

/* Legacy WSPR expects compiler-specific Fortran names and the raw hash. */
#ifdef STDCALL
uint32_t __stdcall NHASH(const void *key, size_t *length, uint32_t *initval)
{
  return wsjt_hashlittle(key, *length, *initval);
}
#else
uint32_t nhash_(const void *key, int *length, uint32_t *initval)
{
  return wsjt_hashlittle(key, (size_t)*length, *initval);
}
#endif
