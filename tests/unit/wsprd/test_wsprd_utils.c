#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/wsprd/nhash.h"
#include "lib/wsprd/wsprd_utils.h"

enum {
    HASH_CALL_SIZE = 13,
    HASH_GRID_SIZE = 5,
    HASH_COUNT = 32768
};

struct wspr_vector {
    const char *name;
    signed char data[11];
    const char *message;
    const char *callsign;
};

static int failures = 0;

static void expect_string(const char *label, const char *actual,
                          const char *expected)
{
    if( strcmp(actual, expected) != 0 ) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n",
                label, expected, actual);
        failures++;
    }
}

static void expect_int(const char *label, int actual, int expected)
{
    if( actual != expected ) {
        fprintf(stderr, "%s: expected %d, got %d\n",
                label, expected, actual);
        failures++;
    }
}

static void decode_vector(const struct wspr_vector *vector,
                          char *hashtab, char *loctab)
{
    char call_loc_pow[23] = {0};
    char callsign[13] = {0};
    int noprint = unpk_((signed char *)vector->data, hashtab, loctab,
                        call_loc_pow, callsign);

    expect_int(vector->name, noprint, 0);
    expect_string(vector->name, call_loc_pow, vector->message);
    expect_string(vector->name, callsign, vector->callsign);
}

static void expect_hash_entry(const char *callsign, const char *grid,
                              char *hashtab, char *loctab)
{
    int ihash = nhash((char *)callsign, strlen(callsign), (uint32_t)146);
    expect_string(callsign, hashtab + ihash * HASH_CALL_SIZE, callsign);
    if( grid != NULL ) {
        expect_string(callsign, loctab + ihash * HASH_GRID_SIZE, grid);
    }
}

static void run_with_fresh_tables(void (*test)(char *, char *))
{
    char *hashtab = calloc(HASH_COUNT * HASH_CALL_SIZE, 1);
    char *loctab = calloc(HASH_COUNT * HASH_GRID_SIZE, 1);
    if( hashtab == NULL || loctab == NULL ) {
        fprintf(stderr, "failed to allocate WSPR hash tables\n");
        exit(2);
    }

    test(hashtab, loctab);

    free(hashtab);
    free(loctab);
}

static const struct wspr_vector k1abc = {
    "K1ABC FN42 33",
    {-9, 12, 35, -117, 13, 24, 64, 0, 0, 0, 0},
    "K1ABC FN42 33",
    "K1ABC"
};

static const struct wspr_vector n1d = {
    "N1D FN42 33",
    {-9, -99, 1, -117, 13, 24, 64, 0, 0, 0, 0},
    "N1D FN42 33",
    "N1D"
};

static const struct wspr_vector five_n = {
    "5N/6O0O 37",
    {45, 15, -83, 104, 42, -103, -64, 0, 0, 0, 0},
    "5N/6O0O 37",
    "5N/6O0O"
};

static const struct wspr_vector pj4 = {
    "PJ4/K1ABC 37",
    {-9, 12, 35, -127, 14, -103, -64, 0, 0, 0, 0},
    "PJ4/K1ABC 37",
    "PJ4/K1ABC"
};

static const struct wspr_vector rover = {
    "K1ABC/R 37",
    {-9, 12, 35, -115, 79, 121, -64, 0, 0, 0, 0},
    "K1ABC/R 37",
    "K1ABC/R"
};

static const struct wspr_vector k1abc_type3_unresolved = {
    "<K1ABC> FN42AA 33",
    {-100, 54, -76, -77, 47, 39, -128, 0, 0, 0, 0},
    "<...> FN42AA 33",
    "<...>"
};

static const struct wspr_vector k1abc_type3_resolved = {
    "<K1ABC> FN42AA 33",
    {-100, 54, -76, -77, 47, 39, -128, 0, 0, 0, 0},
    "<K1ABC> FN42AA 33",
    "<K1ABC>"
};

static const struct wspr_vector pj4_type3_unresolved = {
    "<PJ4/K1ABC> FK52UD 37",
    {-120, 36, 124, 105, -94, -26, -128, 0, 0, 0, 0},
    "<...> FK52UD 37",
    "<...>"
};

static const struct wspr_vector pj4_type3_resolved = {
    "<PJ4/K1ABC> FK52UD 37",
    {-120, 36, 124, 105, -94, -26, -128, 0, 0, 0, 0},
    "<PJ4/K1ABC> FK52UD 37",
    "<PJ4/K1ABC>"
};

static void test_standard_and_compound_messages(char *hashtab, char *loctab)
{
    decode_vector(&k1abc, hashtab, loctab);
    expect_hash_entry("K1ABC", "FN42", hashtab, loctab);

    decode_vector(&n1d, hashtab, loctab);
    expect_hash_entry("N1D", "FN42", hashtab, loctab);

    decode_vector(&five_n, hashtab, loctab);
    expect_hash_entry("5N/6O0O", NULL, hashtab, loctab);

    decode_vector(&pj4, hashtab, loctab);
    expect_hash_entry("PJ4/K1ABC", NULL, hashtab, loctab);

    decode_vector(&rover, hashtab, loctab);
    expect_hash_entry("K1ABC/R", NULL, hashtab, loctab);
}

static void test_type3_unresolved(char *hashtab, char *loctab)
{
    decode_vector(&k1abc_type3_unresolved, hashtab, loctab);
    decode_vector(&pj4_type3_unresolved, hashtab, loctab);
}

static void test_type3_resolved_k1abc(char *hashtab, char *loctab)
{
    decode_vector(&k1abc, hashtab, loctab);
    decode_vector(&k1abc_type3_resolved, hashtab, loctab);
}

static void test_type3_resolved_pj4(char *hashtab, char *loctab)
{
    decode_vector(&pj4, hashtab, loctab);
    decode_vector(&pj4_type3_resolved, hashtab, loctab);
}

int main(void)
{
    run_with_fresh_tables(test_standard_and_compound_messages);
    run_with_fresh_tables(test_type3_unresolved);
    run_with_fresh_tables(test_type3_resolved_k1abc);
    run_with_fresh_tables(test_type3_resolved_pj4);

    return failures == 0 ? 0 : 1;
}
