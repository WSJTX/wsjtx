// SPDX-License-Identifier: GPL-3.0-or-later
#include "wspr_pack.h"
#include "nhash.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static int call_code(char c)
{
    if (is_digit(c)) return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c == ' ') return 36;
    return -1;
}

/* A rotated Type 3 locator also fits the mixed radix callsign field. */
static uint32_t pack_call_field(const char field[6])
{
    uint32_t n = call_code(field[0]);
    n = n * 36 + call_code(field[1]);
    n = n * 10 + call_code(field[2]);
    for (int i = 3; i < 6; ++i) n = n * 27 + (call_code(field[i]) - 10);
    return n;
}

static int pack_base_call(const char *call, uint32_t *packed)
{
    size_t len = strlen(call);
    if (len < 3 || len > 6) return 0;
    char field[6] = {' ', ' ', ' ', ' ', ' ', ' '};
    if (strncmp(call, "3DA0", 4) == 0) {
        /* On-air aliases inherited from packjt's callsign encoding. */
        memcpy(field, "3D0", 3);
        memcpy(field + 3, call + 4, len - 4);
    } else if (strncmp(call, "3X", 2) == 0 && call[2] >= 'A' && call[2] <= 'Z') {
        field[0] = 'Q';
        memcpy(field + 1, call + 2, len - 2);
    } else {
        size_t start = !is_digit(call[2]) && is_digit(call[1]);
        if (start + len > sizeof field) return 0;
        memcpy(field + start, call, len);
    }
    for (size_t i = 0; i < sizeof field; ++i) field[i] = upper(field[i]);

    int second = call_code(field[1]);
    if (call_code(field[0]) < 0 || second < 0 || second >= 36 || !is_digit(field[2])) return 0;
    for (int i = 3; i < 6; ++i) if (call_code(field[i]) < 10) return 0;

    *packed = pack_call_field(field);
    return 1;
}

static int pack_compound(char *call, char *slash, uint32_t *base, uint32_t *grid, int *offset)
{
    *slash = '\0';
    const char *suffix = slash + 1;
    size_t len = strlen(suffix);
    if (len == 0 || slash == call) return 0;
    if (len <= 2) {
        if (!pack_base_call(call, base)) return 0;
        int code = call_code(suffix[0]);
        if (code < 0 || code >= 36) return 0;
        if (len == 2) {
            /* Codes below /10 overlap the single-letter suffixes. */
            if (!is_digit(suffix[0]) || suffix[0] == '0' || !is_digit(suffix[1])) return 0;
            code = 26 + 10 * code + suffix[1] - '0';
        }
        *grid = 60000 - 32768 + code;
        *offset = 2;
    } else {
        size_t prefix_len = (size_t)(slash - call);
        if (prefix_len > 3 || !pack_base_call(suffix, base)) return 0;
        uint32_t prefix = 0;
        for (size_t i = prefix_len; i < 3; ++i) prefix = prefix * 37 + 36;
        for (size_t i = 0; i < prefix_len; ++i) {
            int code = call_code(call[i]);
            if (code < 0 || code >= 36) return 0;
            prefix = prefix * 37 + code;
        }
        *grid = prefix % 32768;
        *offset = 1 + (prefix >= 32768);
    }
    return 1;
}

static int valid_grid4(const char *grid)
{
    return grid[0] >= 'A' && grid[0] <= 'R' && grid[1] >= 'A' && grid[1] <= 'R'
        && is_digit(grid[2]) && is_digit(grid[3]);
}

static int pack_hashed(const char *call, const char *grid, uint32_t *locator, uint32_t *hash)
{
    size_t call_len = strlen(call);
    size_t grid_len = strlen(grid);
    if (call_len < 3 || call_len > 14 || call[call_len - 1] != '>') return 0;
    for (size_t i = 1; i < call_len - 1; ++i) {
        int code = call_code(upper(call[i]));
        if (code < 0 && call[i] != '/') return 0;
    }
    if (grid_len != 4 && grid_len != 6) return 0;

    char normalized[6] = {' ', ' ', ' ', ' ', ' ', ' '};
    for (size_t i = 0; i < grid_len; ++i) normalized[i] = upper(grid[i]);
    if (!valid_grid4(normalized)) return 0;
    if (grid_len == 6 && (normalized[4] < 'A' || normalized[4] > 'X'
                       || normalized[5] < 'A' || normalized[5] > 'X')) return 0;

    /* Preserve omitted subsquares as spaces in the rotated locator. */
    char rotated[6] = {normalized[1], normalized[2], normalized[3],
                       normalized[4], normalized[5], normalized[0]};
    *locator = pack_call_field(rotated);
    *hash = nhash(call + 1, call_len - 2, 146);
    return 1;
}

static int pack_power(const char *text)
{
    const char *digits = text;
    if (*digits == '-' || *digits == '+') ++digits;
    if (!*digits) return -1;
    for (; *digits; ++digits) if (!is_digit(*digits)) return -1;

    errno = 0;
    long dbm = strtol(text, NULL, 10);
    if (errno == ERANGE || dbm < INT_MIN || dbm > INT_MAX) return -1;
    if (dbm < 0) dbm = 0;
    if (dbm > 60) dbm = 60;
    static const int adjustment[10] = {0, -1, 1, 0, -1, 2, 1, 0, -1, 1};
    return (int)dbm + adjustment[dbm % 10];
}

int wspr_pack_message(const char *message, size_t length, void *data)
{
    unsigned char *bytes = data;
    memset(bytes, 0, WSPR_PACKED_SIZE);

    char text[23];
    while (length && message[length - 1] == ' ') --length;
    if (length == 0 || length >= sizeof text || message[0] == ' ' ||
        memchr(message, '\0', length)) return WSPR_PACK_INVALID;
    memcpy(text, message, length);
    text[length] = '\0';

    char *fields[3];
    size_t count = 0;
    char *p = text;
    while (*p) {
        if (count == 3) return WSPR_PACK_INVALID;
        fields[count++] = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = '\0';
        while (*p == ' ') ++p;
    }
    if (count < 2) return WSPR_PACK_INVALID;

    int ntype = pack_power(fields[count - 1]);
    if (ntype < 0) return WSPR_PACK_INVALID;

    uint32_t n, grid;
    char *call = fields[0];
    char *slash = strchr(call, '/');
    if (call[0] == '<') {
        if (count != 3 || !pack_hashed(call, fields[1], &n, &grid)) return WSPR_PACK_INVALID;
        ntype = -(ntype + 1);
    } else if (slash) {
        int offset;
        if (count != 2 || !pack_compound(call, slash, &n, &grid, &offset)) return WSPR_PACK_INVALID;
        ntype += offset;
    } else {
        if (count != 3 || strlen(fields[1]) != 4 || !valid_grid4(fields[1])
            || !pack_base_call(call, &n)) return WSPR_PACK_INVALID;
        const char *loc = fields[1];
        grid = (179 - 10 * (loc[0] - 'A') - (loc[2] - '0')) * 180
             + 10 * (loc[1] - 'A') + (loc[3] - '0');
    }

    uint32_t m = 128 * grid + (ntype + 64);
    bytes[0] = n >> 20;
    bytes[1] = n >> 12;
    bytes[2] = n >> 4;
    bytes[3] = (n << 4) | (m >> 18);
    bytes[4] = m >> 10;
    bytes[5] = m >> 2;
    bytes[6] = m << 6;
    return ntype;
}
