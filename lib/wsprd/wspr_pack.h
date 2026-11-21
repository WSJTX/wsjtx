// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WSPR_PACK_H
#define WSPR_PACK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { WSPR_PACKED_SIZE = 11, WSPR_PACK_INVALID = -999 };

/* Return the encoded type/power field. On failure return WSPR_PACK_INVALID
   and clear data. The message may be blank padded. Data includes the zero tail. */
int wspr_pack_message(const char *message, size_t length, void *data);

#ifdef __cplusplus
}
#endif

#endif
