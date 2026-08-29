#ifndef WSJT_LOOKUP3_H_
#define WSJT_LOOKUP3_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the full 32-bit Jenkins lookup3 hash for a byte sequence. */
uint32_t wsjt_hashlittle(const void *key, size_t length, uint32_t initval);

#ifdef __cplusplus
}
#endif

#endif
