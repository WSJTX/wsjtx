/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * WSPR's decoder (lib/wsprd/wspr_decoder.c, included below with its malloc,
 * calloc and realloc counted and, when a row asks, refused) when it cannot
 * have the memory a decode needs. Each refused decode fails as wsprd and
 * wsjt-codec report a decoder that cannot run: wsprd_decode_pcm returns -1,
 * with wsprd's "Error: WSPR decoding failed" on stderr. The process carries
 * on, and the next decode is the one it would have been without the failed
 * one.
 *
 *   test_wsprd_memory osd <wav> <scratch dir>
 *     On a context that has decoded at depth 1, a decode at the GUI's depth 3
 *     (-C 500 -o 4 -d) on two threads with every allocation refused, then
 *     with only the second thread's OSD pattern boxes refused: each fails
 *     with the hash table as it was, having been refused one allocation,
 *     the size of OSD's boxes, and the context's next depth 3 decode is a
 *     fresh context's, field for field.
 *   test_wsprd_memory each_allocation <wav>
 *     A fresh context's first decode with the stack decoder and OSD (-J
 *     -C 500 -o 4) on two threads, with each of its allocations refused in
 *     turn: each fails, and the context's next decode is a fresh context's.
 *   test_wsprd_memory depth3 <wav>
 *     OSD at depth 3 (-C 500 -o 3 -d), the least that reads the pattern
 *     boxes (WSPR_OSD_BOX_DEPTH), on two threads: a fresh context's decode
 *     asks for each thread's boxes, and is field for field the decode of a
 *     context that already has them, from a depth 4 decode, with its hash
 *     table cleared.
 *   test_wsprd_memory wsprd_file <wav> <scratch dir>
 *     wsprd's run over a file (wsprd -C 500 -o 4 -d) without memory for the
 *     OSD pattern boxes returns 1 and leaves hashtable.txt as it was, as for
 *     wsprd's other decode failures; with it, 0 and the table rewritten.
 */

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <fftw3.h>

#include "lib/wsprd/wsprd_decode.h"
#include "lib/wsprd/wspr_decoder.h"     /* WSPR_OSD_BOX_INTS */

static int failures;

#define CHECK(cond, ...) do { \
    if( !(cond) ) { \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
        failures++; \
    } \
} while (0)

/* One thread's OSD pattern boxes: osdwspr's indexes(4000,2), fp(0:525000)
   and np(4000), a size no other decoder allocation has. */
static size_t const box_bytes = WSPR_OSD_BOX_INTS * sizeof(int);

/* The decoder's allocations: counted, and from the deny_from'th after
   arm(), or any of deny_bytes bytes, refused; with the size of the first
   refused, and how many asked for one thread's OSD boxes. */
static int deny_from;
static size_t deny_bytes;
static int attempts, denied, box_asks;
static size_t first_denied_bytes;

static void arm(int from, size_t bytes)
{
    deny_from = from;
    deny_bytes = bytes;
    attempts = 0;
    denied = 0;
    box_asks = 0;
    first_denied_bytes = 0;
}

static int refused(size_t bytes)
{
    ++attempts;
    if( bytes == box_bytes ) ++box_asks;
    if( (deny_from > 0 && attempts >= deny_from) || (deny_bytes && bytes == deny_bytes) ) {
        if( !denied++ ) first_denied_bytes = bytes;
        return 1;
    }
    return 0;
}

static void *decoder_malloc(size_t bytes)
{
    return refused(bytes) ? NULL : malloc(bytes);
}

static void *decoder_calloc(size_t count, size_t size)
{
    return refused(count * size) ? NULL : calloc(count, size);
}

static void *decoder_realloc(void *block, size_t bytes)
{
    return refused(bytes) ? NULL : realloc(block, bytes);
}

#define malloc decoder_malloc
#define calloc decoder_calloc
#define realloc decoder_realloc
#include "lib/wsprd/wspr_decoder.c"
#undef realloc
#undef calloc
#undef malloc

/* wsprd's readwavfile: skip the 44-byte header, then up to 114 s. */
static short *read_wav(char const *path, size_t *npoints)
{
    size_t n = wsprd_downsample_points(2);
    short *buf = calloc(n, sizeof *buf);
    FILE *fp = fopen(path, "rb");
    if( !buf || !fp ) {
        fprintf(stderr, "cannot read '%s'\n", path);
        free(buf);
        if( fp ) fclose(fp);
        return NULL;
    }
    size_t nr = fread(buf, 2, 22, fp);
    nr = fread(buf, 2, n, fp);
    fclose(fp);
    if( nr == 0 ) {
        free(buf);
        return NULL;
    }
    *npoints = n;
    return buf;
}

/* The GUI's depths (MainWindow::decode's wsprd options), on two threads. */
static struct wsprd_decode_options depth_options(int depth)
{
    struct wsprd_decode_options o;
    wsprd_decode_options_init(&o);
    if( depth == 1 ) {
        o.quickmode = 1;
        o.npasses = 2;
    } else if( depth >= 2 ) {
        o.maxcycles = 500;
        o.ndepth = 4;
        o.more_candidates = depth == 3;
    }
    o.threads = 2;
    return o;
}

struct decode {
    int n;
    struct wsprd_decode_result r[WSPRD_DECODE_MAX_RESULTS];
};

static void decode(struct wsprd_context *ctx, struct wsprd_decode_options const *o,
                   short const *samples, size_t nsamples, struct decode *out)
{
    memset(out, 0, sizeof *out);
    out->n = wsprd_decode_pcm(ctx, o, NULL, samples, nsamples, 14.0956, "150426", "0918", out->r);
}

static int same(struct decode const *a, struct decode const *b)
{
    return a->n == b->n && a->n >= 0 && memcmp(a->r, b->r, a->n * sizeof a->r[0]) == 0;
}

/* The bytes of a file (*size of them), or NULL. */
static char *slurp(char const *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    char *buf = NULL;
    long n;
    if( !fp ) return NULL;
    if( fseek(fp, 0, SEEK_END) == 0 && (n = ftell(fp)) >= 0 && fseek(fp, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)n + 1)) && fread(buf, 1, (size_t)n, fp) == (size_t)n ) {
        *size = (size_t)n;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(fp);
    return buf;
}

/* The context's hash table as hashtable.txt holds it, in a file of the
   scratch directory. */
static char *hash_table(struct wsprd_context *ctx, char const *dir, char const *name, size_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if( wsprd_hash_save(ctx, path) ) return NULL;
    return slurp(path, size);
}

static int row_osd(char const *wav, char const *dir)
{
    size_t nsamples;
    short *samples = read_wav(wav, &nsamples);
    CHECK(samples != NULL, "cannot read the sample");
    if( !samples ) return 1;
    struct wsprd_decode_options fast = depth_options(1), deep = depth_options(3);
    static struct decode expected, got;
    struct wsprd_context *ref = wsprd_context_new(NULL);
    CHECK(ref != NULL, "no context");
    if( !ref ) return 1;
    decode(ref, &fast, samples, nsamples, &got);
    decode(ref, &deep, samples, nsamples, &expected);
    CHECK(expected.n > 0, "the reference decoded nothing");
    wsprd_context_free(ref);

    int threads = wspr_parallel_threads(2);
    for( int from = 1; from <= 2; from++ ) {
        if( from == 2 && threads < 2 ) {
            printf("osd: one thread here; the second thread's boxes are not tried\n");
            break;
        }
        struct wsprd_context *ctx = wsprd_context_new(NULL);
        CHECK(ctx != NULL, "no context");
        if( !ctx ) break;
        decode(ctx, &fast, samples, nsamples, &got);
        CHECK(got.n > 0, "depth 1 decoded nothing");
        size_t before_size = 0, after_size = 0;
        char *before = hash_table(ctx, dir, "before.txt", &before_size);
        CHECK(before != NULL, "cannot save the hash table");
        /* At depth 3 the warmed context allocates only OSD's boxes, one per
           thread, and the decode ends at the first refused: from 1 refuses
           the first thread's, from 2 the second thread's. */
        arm(from, 0);
        decode(ctx, &deep, samples, nsamples, &got);
        int seen = attempts, refusals = denied;
        size_t refused_bytes = first_denied_bytes;
        arm(0, 0);
        CHECK(refusals > 0, "from %d: depth 3 asked the decoder's allocator for nothing to refuse"
              " (%d allocations)", from, seen);
        CHECK(refusals == 1 && refused_bytes == box_bytes, "from %d: %d allocations refused, the"
              " first of %lu bytes, not one of OSD's boxes (%lu bytes)", from, refusals,
              (unsigned long)refused_bytes, (unsigned long)box_bytes);
        CHECK(got.n == -1, "from %d: the decode without memory returned %d, not -1", from, got.n);
        char *after = hash_table(ctx, dir, "after.txt", &after_size);
        CHECK(after && before && after_size == before_size && memcmp(after, before, after_size) == 0,
              "from %d: the failed decode changed the hash table", from);
        free(before);
        free(after);
        decode(ctx, &deep, samples, nsamples, &got);
        CHECK(same(&got, &expected), "from %d: the next decode (%d decodes) is not a fresh"
              " context's (%d)", from, got.n, expected.n);
        printf("osd: from %d, %d of %d allocations refused; then %d decodes\n", from, refusals,
               seen, got.n);
        wsprd_context_free(ctx);
    }
    free(samples);
    return 0;
}

static int row_each_allocation(char const *wav)
{
    size_t nsamples;
    short *samples = read_wav(wav, &nsamples);
    CHECK(samples != NULL, "cannot read the sample");
    if( !samples ) return 1;
    struct wsprd_decode_options stack = depth_options(2);
    stack.stackdecoder = 1;
    static struct decode expected, got;
    struct wsprd_context *ref = wsprd_context_new(NULL);
    CHECK(ref != NULL, "no context");
    if( !ref ) return 1;
    decode(ref, &stack, samples, nsamples, &expected);
    CHECK(expected.n > 0, "the reference decoded nothing");
    wsprd_context_free(ref);

    int k;
    for( k = 1; k < 100; k++ ) {
        struct wsprd_context *ctx = wsprd_context_new(NULL);
        CHECK(ctx != NULL, "no context");
        if( !ctx ) break;
        arm(k, 0);
        decode(ctx, &stack, samples, nsamples, &got);
        int refusals = denied;
        arm(0, 0);
        if( !refusals ) {
            CHECK(same(&got, &expected), "with every allocation given, %d decodes, not %d",
                  got.n, expected.n);
            wsprd_context_free(ctx);
            break;
        }
        CHECK(got.n == -1, "allocation %d refused: the decode returned %d, not -1", k, got.n);
        decode(ctx, &stack, samples, nsamples, &got);
        CHECK(same(&got, &expected), "allocation %d refused: the next decode (%d decodes) is not"
              " a fresh context's (%d)", k, got.n, expected.n);
        wsprd_context_free(ctx);
    }
    printf("each_allocation: %d allocations, each refused in turn\n", k - 1);
    free(samples);
    return 0;
}

static int row_depth3(char const *wav)
{
    size_t nsamples;
    short *samples = read_wav(wav, &nsamples);
    CHECK(samples != NULL, "cannot read the sample");
    if( !samples ) return 1;
    struct wsprd_decode_options four = depth_options(3), three = depth_options(3);
    three.ndepth = 3;       /* wsprd -o 3 */
    static struct decode expected, got;
    /* A context with each thread's boxes, from a depth 4 decode, and its hash
       table cleared, as a fresh context's is. */
    struct wsprd_context *warm = wsprd_context_new(NULL);
    CHECK(warm != NULL, "no context");
    if( !warm ) return 1;
    decode(warm, &four, samples, nsamples, &got);
    CHECK(got.n > 0, "depth 4 decoded nothing");
    wsprd_hash_clear(warm);
    decode(warm, &three, samples, nsamples, &expected);
    CHECK(expected.n > 0, "depth 3, with the boxes there, decoded nothing");
    wsprd_context_free(warm);

    int threads = wspr_parallel_threads(2);
    struct wsprd_context *ctx = wsprd_context_new(NULL);
    CHECK(ctx != NULL, "no context");
    if( !ctx ) return 1;
    arm(0, 0);
    decode(ctx, &three, samples, nsamples, &got);
    int asks = box_asks;
    CHECK(asks == threads, "a fresh context's depth 3 decode asked for OSD's boxes %d times,"
          " not once for each of its %d threads", asks, threads);
    CHECK(same(&got, &expected), "a fresh context's depth 3 decode (%d decodes) is not that of"
          " a context with the boxes (%d)", got.n, expected.n);
    printf("depth3: %d threads, %d asked for OSD's boxes; %d decodes\n", threads, asks, got.n);
    wsprd_context_free(ctx);
    free(samples);
    return 0;
}

static int row_wsprd_file(char const *wav, char const *dir)
{
    char hash[1024];
    /* A table with one station, whom the sample does not hold. */
    static char const seed[] = "12345 K1ABC FN42\n";
    snprintf(hash, sizeof hash, "%s/hashtable.txt", dir);
    struct wsprd_run_args args;
    for( int with_memory = 0; with_memory <= 1; with_memory++ ) {
        FILE *fp = fopen(hash, "wb");
        CHECK(fp && fputs(seed, fp) >= 0, "cannot write %s", hash);
        if( fp ) fclose(fp);
        wsprd_run_args_init(&args);
        args.options = depth_options(3);
        args.options.threads = 0;       /* wsprd's */
        args.data_dir = dir;
        args.infile = wav;
        args.dialfreq_cmdline = 14.0956;
        args.wisdom = 0;
        args.timer = 0;
        arm(0, with_memory ? 0 : box_bytes);
        int rc = wsprd_run_file(&args);
        int refusals = denied;
        arm(0, 0);
        size_t size = 0;
        char *table = slurp(hash, &size);
        int kept = table && size == strlen(seed) && memcmp(table, seed, size) == 0;
        if( with_memory ) {
            CHECK(rc == 0, "with memory, wsprd_run_file returned %d", rc);
            CHECK(table && !kept, "with memory, wsprd left hashtable.txt as it was");
        } else {
            CHECK(refusals > 0, "the run asked the decoder's allocator for no OSD boxes");
            CHECK(rc == 1, "without OSD boxes, wsprd_run_file returned %d, not 1", rc);
            CHECK(kept, "without OSD boxes, wsprd rewrote hashtable.txt");
        }
        free(table);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int rc = 2;
    if( argc == 4 && strcmp(argv[1], "osd") == 0 ) {
        rc = row_osd(argv[2], argv[3]);
    } else if( argc == 3 && strcmp(argv[1], "each_allocation") == 0 ) {
        rc = row_each_allocation(argv[2]);
    } else if( argc == 3 && strcmp(argv[1], "depth3") == 0 ) {
        rc = row_depth3(argv[2]);
    } else if( argc == 4 && strcmp(argv[1], "wsprd_file") == 0 ) {
        rc = row_wsprd_file(argv[2], argv[3]);
    } else {
        fprintf(stderr, "usage: %s osd <wav> <dir> | each_allocation <wav> | depth3 <wav>"
                " | wsprd_file <wav> <dir>\n", argv[0]);
        return 2;
    }
    if( rc == 0 && failures == 0 ) {
        printf("%s: passed\n", argv[1]);
        return 0;
    }
    fprintf(stderr, "%s: %d failure(s)\n", argv[1], failures);
    return 1;
}
