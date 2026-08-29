#ifndef SUPERFOX_NHASH2_H_
#define SUPERFOX_NHASH2_H_

#ifdef Win32
#include "win_stdint.h"	/* defines uint32_t etc */
#else
#include <stddef.h>
#include <stdint.h>	/* defines uint32_t etc */
#endif

#ifdef __cplusplus
extern "C" {
#endif

uint32_t nhash2(const void *key, uint64_t length, uint32_t initval);

#ifdef __cplusplus
}
#endif

#endif
