/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * wsprd's decode library in one process.
 *
 *   test_wsprd_decode repeat <wav>
 *     Two fresh contexts decode the same audio and give identical results,
 *     at wsprd's defaults, at the GUI's deepest options and with the stack
 *     decoder (-J); the second plans through a plan_guard that sees every
 *     plan made and destroyed.
 *   test_wsprd_decode hash_roundtrip <wav> <scratch dir>
 *     A hash table loaded from hashtable.txt, decoded into, saved and
 *     reloaded writes the same file again.
 *   test_wsprd_decode run_file <wav> <scratch dir>
 *     wsprd_run_file writes wspr_wisdom.dat and wspr_timer.out only when
 *     asked, writes the other side files either way, and plans through its
 *     plan_guard.
 *   test_wsprd_decode short_name <wav> <scratch dir>
 *     A file whose name is shorter than wsprd's date and time decodes, with
 *     both blank.
 *   test_wsprd_decode unresolved_type3
 *     A type 3 message whose call the table lacks is subtracted as sent: the
 *     decode's hard errors are counted against the symbols sent, not those of
 *     its printed text, <...>, whose hash differs.
 *   test_wsprd_decode type2_grid <scratch dir>
 *     A type 2 decode into a slot that held another station's call and grid
 *     leaves that slot with no grid.
 *   test_wsprd_decode unencodable_text
 *     A decode whose printed text no WSPR encoder makes (call "K1") is not
 *     printed, and a weaker signal after it still decodes.
 *   test_wsprd_decode calibration_passes
 *     A calibrated search retains a weaker signal for a later pass.
 *   test_wsprd_decode osd_distance
 *     Check the distance and ranking of WSPR codewords after a missed pivot.
 *   test_wsprd_decode decoder_api
 *     wspr_decoder's knowledge import checks, its status codes, no reception
 *     after a failed prepare, and the 12 kHz transform planned and destroyed
 *     in each prepare.
 *   test_wsprd_decode threads <wav>
 *     Threads decode as one thread does, field for field: the sample at the
 *     GUI's three depths and a crowded period whose type 3 message resolves
 *     only through a type 1 decoded before it in the same pass, each with two,
 *     four and the CPU count's threads, twice; the sample at wsprd's defaults
 *     and with the stack decoder with four threads once; and one context
 *     decoding the sample at 8, 2 and the CPU count's threads in turn, as a
 *     codec session whose wspr_threads changes, against one thread. And the
 *     count follows ft8_threads' rule on 1 to 64 cores. With no OpenMP every
 *     count is one.
 *   test_wsprd_decode osd_threads
 *     osdwspr at the GUI's depth on eight threads at once (the process's
 *     first calls among them), each thread with pattern boxes of its own, as
 *     the decoder gives them, 96 noisy codewords 31 times over, returns what
 *     it returns called one at a time, and more than one thread ran (with
 *     OpenMP).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#define change_dir(path) _chdir(path)
#else
#include <unistd.h>
#define make_dir(path) mkdir(path, 0755)
#define change_dir(path) chdir(path)
#endif

#include <float.h>
#include <math.h>

#include "lib/wsprd/wsprd_decode.h"
#include "lib/wsprd/wspr_decoder.h"
#include "lib/wsprd/nhash.h"
#include "lib/wsprd/fano.h"
#include "lib/wsprd/wsprsim_utils.h"

extern void osdwspr_(float [], unsigned char [], int *, unsigned char [], int *, float *, int []);
extern int wspr_parallel_threads(int requested);
extern int wspr_parallel_thread_rule(int requested, int cores);
extern int wspr_parallel_cpus(void);
extern int wspr_parallel_for(int count, int threads, void (*body)(void *, int, int), void *arg);

static int failures = 0;

/* WSPRD_DECODE_THREADS_BRIEF set (ThreadSanitizer builds, which run decodes
   tens of times slower): the threads row runs four threads once, at the
   GUI's depth 2 only (OSD on), on the sample and the crowded period, and the
   reused context at 2 and 8 threads. */
static int brief_threads(void)
{
    return getenv("WSPRD_DECODE_THREADS_BRIEF") != NULL;
}

#define CHECK(cond, ...) do { \
    if( !(cond) ) { \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fputc('\n', stderr); \
        failures++; \
    } \
} while (0)

/* wsprd's readwavfile: skip the 44-byte header, then up to 114 s of samples;
   the rest of the buffer stays zero. */
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
        fprintf(stderr, "no samples in '%s'\n", path);
        free(buf);
        return NULL;
    }
    *npoints = n;
    return buf;
}

static int planner_calls = 0;

/* Runs and counts every plan and destroy call. */
static int counting_guard(void (*fn)(void *), void *arg)
{
    fn(arg);
    planner_calls++;
    return 0;
}

static int decode_once(short const *samples, size_t nsamples,
                       struct wsprd_decode_options const *opt,
                       struct wsprd_decode_hooks const *hooks,
                       struct wsprd_context *ctx,
                       struct wsprd_decode_result *results)
{
    memset(results, 0, sizeof(struct wsprd_decode_result) * WSPRD_DECODE_MAX_RESULTS);
    return wsprd_decode_pcm(ctx, opt, hooks, samples, nsamples,
                            14.0956, "150426", "0918", results);
}

/* One WSPR-2 signal: a message's text, or (text NULL) its packed call n and
   grid and power m; hz from 1500 Hz; amplitude. */
struct signal {
    char const *text;
    unsigned long n, m;
    double hz, amplitude;
};

/* A WSPR-2 period holding the signals from 1 s, continuous-phase, in
   Gaussian noise of standard deviation sigma (fixed seed). */
static short *synthesize_signals(struct signal const *signals, int count, double sigma)
{
    static char hashtab[32768 * 13], loctab[32768 * 5];
    size_t n = wsprd_downsample_points(2);
    short *pcm = calloc(n, sizeof *pcm);
    double *sum = calloc(n, sizeof *sum);
    double const pi = 3.14159265358979323846;
    if( !pcm || !sum ) {
        free(pcm);
        free(sum);
        return NULL;
    }
    for( int s = 0; s < count; s++ ) {
        unsigned char symbols[162];
        struct signal const *g = &signals[s];
        if( g->text ) {
            if( !get_wspr_channel_symbols((char *)g->text, hashtab, loctab, symbols) ) {
                free(pcm);
                free(sum);
                return NULL;
            }
        } else {
            unsigned char data[11] = {0};
            data[0] = 0xFF & (g->n >> 20); data[1] = 0xFF & (g->n >> 12); data[2] = 0xFF & (g->n >> 4);
            data[3] = ((g->n & 0x0F) << 4) + ((g->m >> 18) & 0x0F);
            data[4] = 0xFF & (g->m >> 10); data[5] = 0xFF & (g->m >> 2); data[6] = (g->m & 0x03) << 6;
            wspr_channel_symbols_from_data(data, symbols);
        }
        double phase = 0;
        for( long k = 0; k < 162L * 8192; k++ ) {
            double f = 1500.0 + g->hz + (symbols[k / 8192] - 1.5) * 12000.0 / 8192.0;
            phase += 2 * pi * f / 12000.0;
            sum[12000 + k] += g->amplitude * sin(phase);
        }
    }
    unsigned long long state = 12345;
    for( size_t i = 0; i < n; i++ ) {
        double v = sum[i];
        double u[2];
        for( int j = 0; j < 2; j++ ) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            u[j] = ((state >> 11) + 0.5) / 9007199254740992.0;
        }
        v += sigma * sqrt(-2 * log(u[0])) * cos(2 * pi * u[1]);
        if( v > 32767 ) v = 32767;
        if( v < -32768 ) v = -32768;
        pcm[i] = (short)lrint(v);
    }
    free(sum);
    return pcm;
}

/* A WSPR-2 period holding message at 1500 Hz, amplitude 1000, in Gaussian
   noise for snr_db in 2500 Hz. */
static short *synthesize(char const *message, double snr_db)
{
    struct signal one = {message, 0, 0, 0.0, 1000.0};
    double sigma = sqrt(1000.0 * 1000.0 / 2 / pow(10.0, snr_db / 10) * 6000.0 / 2500.0);
    return synthesize_signals(&one, 1, sigma);
}

static void compare_results(char const *label, int n1,
                            struct wsprd_decode_result const *a, int n2,
                            struct wsprd_decode_result const *b)
{
    CHECK(n1 == n2, "%s: %d decodes then %d", label, n1, n2);
    for( int i = 0; i < n1 && i < n2; i++ ) {
        CHECK(strcmp(a[i].date, b[i].date) == 0, "%s[%d]: date", label, i);
        CHECK(strcmp(a[i].time, b[i].time) == 0, "%s[%d]: time", label, i);
        CHECK(strcmp(a[i].message, b[i].message) == 0, "%s[%d]: message '%s' then '%s'",
              label, i, a[i].message, b[i].message);
        CHECK(a[i].freq == b[i].freq, "%s[%d]: freq %.9f then %.9f", label, i, a[i].freq, b[i].freq);
        CHECK(a[i].snr == b[i].snr, "%s[%d]: snr", label, i);
        CHECK(a[i].dt == b[i].dt, "%s[%d]: dt", label, i);
        CHECK(a[i].drift == b[i].drift, "%s[%d]: drift", label, i);
        CHECK(a[i].sync == b[i].sync, "%s[%d]: sync", label, i);
        CHECK(a[i].cycles == b[i].cycles, "%s[%d]: cycles", label, i);
        CHECK(a[i].jitter == b[i].jitter, "%s[%d]: jitter", label, i);
        CHECK(a[i].blocksize == b[i].blocksize, "%s[%d]: blocksize", label, i);
        CHECK(a[i].metric == b[i].metric, "%s[%d]: metric", label, i);
        CHECK(a[i].nhardmin == b[i].nhardmin, "%s[%d]: nhardmin", label, i);
        CHECK(a[i].ipass == b[i].ipass, "%s[%d]: ipass", label, i);
        CHECK(a[i].decodetype == b[i].decodetype, "%s[%d]: decodetype", label, i);
        CHECK(a[i].f1 == b[i].f1, "%s[%d]: f1", label, i);
    }
}

static int row_repeat(char const *wav)
{
    size_t nsamples = 0;
    short *samples = read_wav(wav, &nsamples);
    if( !samples ) return 1;

    struct wsprd_decode_options defaults, deepest, stack;
    wsprd_decode_options_init(&defaults);
    wsprd_decode_options_init(&deepest);
    wsprd_decode_options_init(&stack);
    deepest.maxcycles = 500;          /* -C 500 -o 4 -d */
    deepest.ndepth = 4;
    deepest.more_candidates = 1;
    stack.stackdecoder = 1;           /* -J */

    struct { char const *label; struct wsprd_decode_options const *opt; int expected; } sets[] = {
        {"defaults", &defaults, 9},   /* test/fixtures/wspr_150426_0918.expected.txt */
        {"-C 500 -o 4 -d", &deepest, 8},
        {"-J", &stack, 9},
    };
    static struct wsprd_decode_result first[WSPRD_DECODE_MAX_RESULTS];
    static struct wsprd_decode_result second[WSPRD_DECODE_MAX_RESULTS];
    for( size_t s = 0; s < sizeof sets / sizeof sets[0]; s++ ) {
        struct wsprd_context *ctx1 = wsprd_context_new(NULL);
        planner_calls = 0;
        struct wsprd_context *ctx2 = wsprd_context_new(counting_guard);
        CHECK(ctx1 && ctx2, "context allocation");
        if( !ctx1 || !ctx2 ) {
            wsprd_context_free(ctx1);
            wsprd_context_free(ctx2);
            free(samples);
            return 1;
        }
        int n1 = decode_once(samples, nsamples, sets[s].opt, NULL, ctx1, first);
        int n2 = decode_once(samples, nsamples, sets[s].opt, NULL, ctx2, second);

        CHECK(n1 == sets[s].expected, "%s: %d decodes, expected %d", sets[s].label, n1, sets[s].expected);
        compare_results(sets[s].label, n1, first, n2, second);
        wsprd_context_free(ctx1);
        wsprd_context_free(ctx2);
        /* two plans made at creation and destroyed when freed; the 12 kHz
           transform's made and destroyed in the decode */
        CHECK(planner_calls == 6, "%s: plan_guard ran %d planner calls, expected 6",
              sets[s].label, planner_calls);
    }
    free(samples);
    return 0;
}

static char *slurp(char const *path)
{
    FILE *fp = fopen(path, "rb");
    if( !fp ) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *s = malloc((size_t)n + 1);
    if( s && fread(s, 1, (size_t)n, fp) != (size_t)n ) {
        free(s);
        s = NULL;
    }
    if( s ) {
        /* Text the program wrote: CR LF on Windows. */
        long k = 0;
        for( long j = 0; j < n; j++ ) if( !(s[j] == '\r' && j + 1 < n && s[j + 1] == '\n') ) s[k++] = s[j];
        s[k] = '\0';
    }
    fclose(fp);
    return s;
}

static int row_hash_roundtrip(char const *wav, char const *dir)
{
    char seed[1024], saved[1024], resaved[1024], cleared[1024], missing[1024];
    snprintf(seed, sizeof seed, "%s/seed-hashtable.txt", dir);
    snprintf(saved, sizeof saved, "%s/saved-hashtable.txt", dir);
    snprintf(resaved, sizeof resaved, "%s/resaved-hashtable.txt", dir);
    snprintf(cleared, sizeof cleared, "%s/cleared-hashtable.txt", dir);
    snprintf(missing, sizeof missing, "%s/no-such-hashtable.txt", dir);

    /* Two calls the sample does not hold; the second line has no grid. */
    FILE *fp = fopen(seed, "w");
    CHECK(fp != NULL, "cannot write '%s'", seed);
    if( !fp ) return 1;
    fputs("  123 K1ABC FN42\n31000 PJ4/K1ABC\n", fp);
    fclose(fp);

    size_t nsamples = 0;
    short *samples = read_wav(wav, &nsamples);
    if( !samples ) return 1;

    struct wsprd_context *a = wsprd_context_new(NULL);
    struct wsprd_context *b = wsprd_context_new(NULL);
    CHECK(a && b, "context allocation");
    if( !a || !b ) {
        wsprd_context_free(a);
        wsprd_context_free(b);
        free(samples);
        return 1;
    }
    CHECK(wsprd_hash_load(a, missing) == -1, "loading a missing file");
    CHECK(wsprd_hash_load(a, seed) == 0, "loading '%s'", seed);

    struct wsprd_decode_options opt;
    wsprd_decode_options_init(&opt);
    static struct wsprd_decode_result results[WSPRD_DECODE_MAX_RESULTS];
    int n = decode_once(samples, nsamples, &opt, NULL, a, results);
    CHECK(n == 9, "%d decodes, expected 9", n);

    CHECK(wsprd_hash_save(a, saved) == 0, "saving '%s'", saved);
    /* The seed's lines and the sample's nine calls, in hashtable.txt's
       format, as develop's wsprd writes them for this sample. */
    char const *expected =
        "  123 K1ABC FN42\n"
        " 2711 ND6P DM04\n"
        " 4023 W3BI FN20\n"
        " 4642 W5BIT EL09\n"
        " 5785 NM7J DM26\n"
        " 6857 KI7CI DM09\n"
        " 8822 W3HH EL89\n"
        "12775 G8VDQ IO91\n"
        "15534 WD4LHT EL89\n"
        "23865 DJ6OL JO52\n"
        "31000 PJ4/K1ABC \n";
    char *text = slurp(saved);
    CHECK(text && strcmp(text, expected) == 0, "saved table:\n%s\nexpected:\n%s",
          text ? text : "(unreadable)", expected);
    free(text);

    CHECK(wsprd_hash_load(b, saved) == 0, "reloading '%s'", saved);
    CHECK(wsprd_hash_save(b, resaved) == 0, "saving '%s'", resaved);
    char *first = slurp(saved), *again = slurp(resaved);
    CHECK(first && again && strcmp(first, again) == 0,
          "reloaded table saves differently:\n%s\nthen:\n%s",
          first ? first : "(unreadable)", again ? again : "(unreadable)");
    free(first);
    free(again);

    wsprd_hash_clear(b);
    CHECK(wsprd_hash_save(b, cleared) == 0, "saving '%s'", cleared);
    text = slurp(cleared);
    CHECK(text && text[0] == '\0', "a cleared table saves '%s'", text ? text : "(unreadable)");
    free(text);

    wsprd_context_free(a);
    wsprd_context_free(b);
    free(samples);
    return 0;
}

static int count_lines(char const *path)
{
    char *text = slurp(path);
    int n = -1;
    if( text ) {
        n = 0;
        for( char *p = text; *p; p++ ) n += *p == '\n';
        free(text);
    }
    return n;
}

static int exists(char const *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int row_run_file(char const *wav, char const *dir)
{
    static char const *const names[] = {
        "ALL_WSPR.TXT", "wspr_spots.txt", "hashtable.txt",
        "wspr_wisdom.dat", "wspr_timer.out"
    };
    for( int flags = 0; flags <= 1; flags++ ) {
        char sub[1024], path[1100];
        snprintf(sub, sizeof sub, "%s/run_file_%d", dir, flags);
        make_dir(sub);
        for( size_t i = 0; i < sizeof names / sizeof names[0]; i++ ) {
            snprintf(path, sizeof path, "%s/%s", sub, names[i]);
            remove(path);
        }

        struct wsprd_run_args args;
        wsprd_run_args_init(&args);
        CHECK(args.wisdom == 1 && args.timer == 1 && args.usehashtable == 1,
              "run args defaults");
        args.data_dir = sub;
        args.infile = wav;
        args.dialfreq_cmdline = 14.0956;
        args.wisdom = flags;
        args.timer = flags;
        args.plan_guard = counting_guard;
        planner_calls = 0;
        fflush(stdout);
        int rc = wsprd_run_file(&args);
        CHECK(rc == 0, "flags %d: wsprd_run_file returned %d", flags, rc);
        CHECK(planner_calls == 6, "flags %d: plan_guard ran %d planner calls, expected 6",
              flags, planner_calls);
        for( int i = 0; i < 3; i++ ) {
            snprintf(path, sizeof path, "%s/%s", sub, names[i]);
            CHECK(count_lines(path) == 9, "flags %d: %s has %d lines, expected 9",
                  flags, names[i], count_lines(path));
        }
        for( int i = 3; i < 5; i++ ) {
            snprintf(path, sizeof path, "%s/%s", sub, names[i]);
            CHECK(exists(path) == flags, "flags %d: %s %s", flags, names[i],
                  exists(path) ? "written" : "missing");
        }
    }

    struct wsprd_run_args args;
    wsprd_run_args_init(&args);
    args.data_dir = dir;
    args.infile = "no-such-dir/150426_0918.wav";
    CHECK(wsprd_run_file(&args) == 1, "a missing file returns 1");
    return 0;
}

static int row_short_name(char const *wav, char const *dir)
{
    char sub[1024], copy[1100];
    snprintf(sub, sizeof sub, "%s/short_name", dir);
    make_dir(sub);
    snprintf(copy, sizeof copy, "%s/a.wav", sub);
    char *bytes = NULL;
    FILE *in = fopen(wav, "rb"), *out = fopen(copy, "wb");
    CHECK(in && out, "cannot copy '%s' to '%s'", wav, copy);
    if( in && out ) {
        char buf[65536];
        size_t n;
        while( (n = fread(buf, 1, sizeof buf, in)) > 0 ) fwrite(buf, 1, n, out);
    }
    if( in ) fclose(in);
    if( out ) fclose(out);
    if( change_dir(sub) != 0 ) {
        CHECK(0, "cannot enter '%s'", sub);
        return 1;
    }
    remove("ALL_WSPR.TXT");
    struct wsprd_run_args args;
    wsprd_run_args_init(&args);
    args.data_dir = ".";
    args.infile = "a.wav";
    args.dialfreq_cmdline = 14.0956;
    args.wisdom = 0;
    args.timer = 0;
    fflush(stdout);
    int rc = wsprd_run_file(&args);
    CHECK(rc == 0, "a.wav: wsprd_run_file returned %d", rc);
    CHECK(count_lines("ALL_WSPR.TXT") == 9, "a.wav: ALL_WSPR.TXT has %d lines, expected 9",
          count_lines("ALL_WSPR.TXT"));
    bytes = slurp("ALL_WSPR.TXT");
    /* "%6s %4s ": the blank date, a space, the blank time, a space. */
    CHECK(bytes && strncmp(bytes, "            ", 12) == 0,
          "a.wav: the date and time are not blank: '%.24s'", bytes ? bytes : "(unreadable)");
    free(bytes);
    return 0;
}

static int row_unresolved_type3(void)
{
    short *pcm = synthesize("<K1ABC> FN42AA 33", -10);
    CHECK(pcm != NULL, "cannot synthesize the message");
    if( !pcm ) return 1;
    struct wsprd_context *ctx = wsprd_context_new(NULL);
    struct wsprd_decode_options opt;
    wsprd_decode_options_init(&opt);
    static struct wsprd_decode_result results[WSPRD_DECODE_MAX_RESULTS];
    int n = ctx ? decode_once(pcm, wsprd_downsample_points(2), &opt, NULL, ctx, results) : -1;
    CHECK(n == 1, "%d decodes, expected 1", n);
    if( n == 1 ) {
        CHECK(strcmp(results[0].message, "<...> FN42AA 33") == 0, "decoded '%s'", results[0].message);
        CHECK(results[0].decodetype == 0, "decoded by OSD, not Fano");
        /* The symbols sent, at -10 dB: a few hard errors at most. The printed
           text's symbols differ from them in 48 of 162. */
        CHECK(results[0].nhardmin <= 5, "%d hard errors against the symbols subtracted",
              results[0].nhardmin);
    }
    wsprd_context_free(ctx);
    free(pcm);
    return 0;
}

static int row_type2_grid(char const *dir)
{
    char const *call = "PJ4/K1ABC";
    char line[64], path[1100], expected[64];
    int slot = (int)nhash(call, strlen(call), 146);
    short *pcm = synthesize("PJ4/K1ABC 37", -10);
    struct wsprd_context *ctx = wsprd_context_new(NULL);
    CHECK(pcm && ctx, "cannot synthesize the message or make a context");
    if( !pcm || !ctx ) {
        free(pcm);
        wsprd_context_free(ctx);
        return 1;
    }
    /* Another station's call and grid in the compound call's slot. */
    snprintf(line, sizeof line, "%5d W9XYZ EN50\n", slot);
    CHECK(wsprd_hash_load_line(ctx, line) == 0, "loading '%s'", line);
    CHECK(wsprd_hash_load_line(ctx, "not an entry\n") == -1, "a line that is not an entry");
    struct wsprd_decode_options opt;
    wsprd_decode_options_init(&opt);
    static struct wsprd_decode_result results[WSPRD_DECODE_MAX_RESULTS];
    int n = decode_once(pcm, wsprd_downsample_points(2), &opt, NULL, ctx, results);
    CHECK(n == 1 && strcmp(results[0].message, "PJ4/K1ABC 37") == 0, "%d decodes, first '%s'",
          n, n > 0 ? results[0].message : "");
    snprintf(path, sizeof path, "%s/type2-hashtable.txt", dir);
    CHECK(wsprd_hash_save(ctx, path) == 0, "saving '%s'", path);
    snprintf(expected, sizeof expected, "%5d PJ4/K1ABC \n", slot);
    char *text = slurp(path);
    CHECK(text && strcmp(text, expected) == 0, "saved '%s', expected '%s'",
          text ? text : "(unreadable)", expected);
    free(text);
    wsprd_context_free(ctx);
    free(pcm);
    return 0;
}

static int row_unencodable_text(void)
{
    /* Call " K1 BC", which unpacks to "K1", grid FN42, 37 dBm: no WSPR encoder
       makes it. Then a standard message 50 Hz above, 10 dB weaker. */
    struct signal signals[] = {
        {NULL, 259066946UL, 2896997UL, 0.0, 1000.0},
        {"K9AN EN50 33", 0, 0, 50.0, 300.0},
    };
    short *pcm = synthesize_signals(signals, 2, 3000.0);
    struct wsprd_context *ctx = wsprd_context_new(NULL);
    CHECK(pcm && ctx, "cannot synthesize the period or make a context");
    if( pcm && ctx ) {
        struct wsprd_decode_options opt;
        wsprd_decode_options_init(&opt);
        static struct wsprd_decode_result results[WSPRD_DECODE_MAX_RESULTS];
        int n = decode_once(pcm, wsprd_downsample_points(2), &opt, NULL, ctx, results);
        CHECK(n == 1, "%d decodes, expected 1 (K9AN EN50 33 only)", n);
        for( int i = 0; i < n; i++ )
            CHECK(strcmp(results[i].message, "K9AN EN50 33") == 0, "decoded '%s'", results[i].message);
    }
    wsprd_context_free(ctx);
    free(pcm);
    return 0;
}

static int row_calibration_passes(void)
{
    struct signal signals[] = {
        {"K1ABC FN42 33", 0, 0, 20.0, 2000.0},
        {"K9AN EN50 33", 0, 0, 21.0, 500.0},
    };
    short *pcm = synthesize_signals(signals, 2, 3000.0);
    struct wsprd_context *ctx = wsprd_context_new(NULL);
    CHECK(pcm && ctx, "cannot synthesize the period or make a context");
    if( pcm && ctx ) {
        struct wsprd_decode_options opt;
        wsprd_decode_options_init(&opt);
        opt.fmin = -30;
        opt.fmax = 100;
        opt.dialfreq_error = 45;
        static struct wsprd_decode_result results[WSPRD_DECODE_MAX_RESULTS];
        int n = decode_once(pcm, wsprd_downsample_points(2), &opt, NULL, ctx, results);
        int found = 0;
        for( int i = 0; i < n; i++ ) {
            if( strcmp(results[i].message, "K9AN EN50 33") == 0 ) {
                found = 1;
                CHECK(results[i].ipass > 0, "weak signal decoded on pass %d", results[i].ipass);
            }
        }
        CHECK(found, "weak signal was not decoded on a later pass (%d decodes)", n);
        opt.fmin = -20;
        n = decode_once(pcm, wsprd_downsample_points(2), &opt, NULL, ctx, results);
        CHECK(n == 0, "%d decodes below the corrected lower bound", n);
        opt.fmin = -30;
        opt.fmax = -26;
        n = decode_once(pcm, wsprd_downsample_points(2), &opt, NULL, ctx, results);
        CHECK(n == 0, "%d decodes above the corrected upper bound", n);
    }
    wsprd_context_free(ctx);
    free(pcm);
    return 0;
}

static decoder_wspr_call wspr_call(char const *call, char const *grid)
{
    decoder_wspr_call c;
    memset(c.call, ' ', sizeof c.call);
    memset(c.grid, ' ', sizeof c.grid);
    memcpy(c.call, call, strlen(call) > sizeof c.call ? sizeof c.call : strlen(call));
    memcpy(c.grid, grid, strlen(grid) > sizeof c.grid ? sizeof c.grid : strlen(grid));
    return c;
}

static int row_decoder_api(void)
{
    wspr_decoder *d = NULL;
    planner_calls = 0;
    CHECK(wspr_decoder_create_guarded(&d, counting_guard) == DECODER_OK && d, "create");
    if( !d ) return 1;
    CHECK(planner_calls == 2, "creation made %d plans, expected 2", planner_calls);

    /* Knowledge: valid entries, then each invalid one leaves them. */
    decoder_wspr_call good[] = {wspr_call("K1ABC", "FN42"), wspr_call("PJ4/K1ABC", ""),
                                wspr_call("W1AW", "RR99"), wspr_call("G4ABC/P", "AA00")};
    CHECK(wspr_decoder_set_calls(d, good, 4) == DECODER_OK, "four valid entries");
    int32_t count = -1;
    CHECK(wspr_decoder_get_calls(d, NULL, 0, &count) == DECODER_OK && count == 4, "%d entries, expected 4", count);
    decoder_wspr_call bad[13];
    int nbad = 0;
    bad[nbad] = wspr_call("K1ABC", "FN42"); memcpy(bad[nbad].call, "K1ABC\0XYZ", 9); nbad++;
    bad[nbad++] = wspr_call("K1 ABC", "FN42");
    bad[nbad++] = wspr_call("k1abc", "FN42");
    bad[nbad++] = wspr_call("", "FN42");
    bad[nbad++] = wspr_call("ABCDEFGHIJK", "FN42");
    bad[nbad++] = wspr_call("K1ABC", "F N4");
    bad[nbad] = wspr_call("K1ABC", "FN42"); bad[nbad].grid[1] = 0; nbad++;
    bad[nbad++] = wspr_call("K1ABC", "FN4");
    bad[nbad++] = wspr_call("K1ABC", "SS00");
    bad[nbad++] = wspr_call("K1ABC", "fn42");
    bad[nbad++] = wspr_call("K1ABC", "42FN");
    for( int i = 0; i < nbad; i++ ) {
        CHECK(wspr_decoder_set_calls(d, &bad[i], 1) == DECODER_INVALID, "bad entry %d accepted", i);
        count = -1;
        CHECK(wspr_decoder_get_calls(d, NULL, 0, &count) == DECODER_OK && count == 4,
              "bad entry %d changed the knowledge (%d entries)", i, count);
    }
    CHECK(wspr_decoder_import_hash_line(d, "  123 K1ABC FN42\n") == DECODER_OK, "a hashtable.txt line");
    CHECK(wspr_decoder_import_hash_line(d, "not an entry\n") == DECODER_INVALID, "a line that is not an entry");

    /* The 12 kHz transform is planned and destroyed in each prepare. */
    short silence[1] = {0};
    CHECK(wspr_decoder_prepare_pcm(d, silence, 1, 12000, 2) == DECODER_OK, "prepare");
    CHECK(planner_calls == 4, "after one prepare, %d planner calls, expected 4", planner_calls);
    CHECK(wspr_decoder_prepare_pcm(d, silence, 1, 12000, 2) == DECODER_OK, "prepare again");
    CHECK(planner_calls == 6, "after two prepares, %d planner calls, expected 6", planner_calls);

    /* A failed prepare leaves no reception to decode. */
    decoder_wspr_options options = {2, 0, 1390.0f, 1610.0f, 0.0f};
    int32_t found = -1;
    CHECK(wspr_decoder_decode(d, &options, NULL, NULL, &found) == DECODER_OK, "decode the prepared silence");
    CHECK(wspr_decoder_prepare_pcm(d, silence, 1, 11025, 2) == DECODER_INVALID, "a rate of 11025 Hz");
    CHECK(wspr_decoder_decode(d, &options, NULL, NULL, &found) == DECODER_INVALID,
          "decoded the previous reception after a failed prepare");
    float iq[45000] = {0};
    CHECK(wspr_decoder_prepare_iq(d, iq, iq, 45000, 2) == DECODER_OK, "prepare I/Q");
    CHECK(wspr_decoder_prepare_iq(d, iq, iq, 1000, 2) == DECODER_INVALID, "1000 I/Q points");
    CHECK(wspr_decoder_decode(d, &options, NULL, NULL, &found) == DECODER_INVALID,
          "decoded the previous I/Q after a failed prepare");

    wspr_decoder_destroy(d);
    CHECK(planner_calls == 8, "after destroy, %d planner calls, expected 8", planner_calls);
    return 0;
}

static int row_osd_distance(void)
{
    enum { symbols = 162, pivot_free_prefix = 98 };
    unsigned char message[11] = {0};
    // The encoder writes two symbols for every bit, including byte padding.
    unsigned char challenger[sizeof message * 16];
    unsigned char apmask[symbols] = {0}, cw[symbols];
    int depth = 1;

    /* The last message bit first affects symbols 99 and 100. Ranking the
       first 98 highest leaves it without a pivot in the first 70 columns.
       A single input flip still generates its convolutional codeword. */
    message[6] = 0x40;
    encode(challenger, message, sizeof message);
    CHECK(memcmp(challenger, (unsigned char[pivot_free_prefix]){0}, pivot_free_prefix) == 0,
          "challenger has a nonzero symbol in the reliable prefix");
    int const prefix_weights[] = {2, 127};
    for( int profile = 0; profile < 2; profile++ ) {
        int weight = prefix_weights[profile], nhard;
        float received[symbols], reported;
        for( int i = 0; i < symbols; i++ )
            received[i] = challenger[i] ? 1 : -(i < pivot_free_prefix ? weight : 1);
        // Make the hard decisions differ from the challenger by one symbol.
        received[symbols-1] *= -1;

        /* Below WSPR_OSD_BOX_DEPTH osdwspr reads no boxes, but its dummy is
           an array: pass one rather than NULL. */
        int no_boxes[1] = {0};
        osdwspr_(received, apmask, &depth, cw, &nhard, &reported, no_boxes);
        int actual = 0, hard = 0;
        for( int i = 0; i < symbols; i++ ) {
            if( cw[i] != (received[i] >= 0) ) {
                actual += (int)fabsf(received[i]);
                hard++;
            }
        }
        /* Allow for float normalization by 127 and one addition per hard
           disagreement. */
        double expected = actual / 127.0;
        double tolerance = (hard + 1) * FLT_EPSILON * expected;
        CHECK(memcmp(cw, challenger, symbols) == 0,
              "prefix weight %d: returned the wrong codeword at distance %d", weight, actual);
        CHECK(fabs(reported - expected) <= tolerance && nhard == hard,
              "prefix weight %d: reported distance %g and %d errors, expected %d/127 and %d",
              weight, reported, nhard, actual, hard);
    }
    return 0;
}

/* Decodes at each option set from first to last - 1 (wsprd's defaults, the
   GUI's three depths, the stack decoder) with every thread count, against
   one thread. */
static void compare_thread_counts(char const *what, short const *samples, size_t nsamples,
                                  int first, int last, char const *must)
{
    struct wsprd_decode_options sets[5];
    char const *labels[5] = {"defaults", "depth 1 (-qB)", "depth 2 (-C 500 -o 4)",
                             "depth 3 (-C 500 -o 4 -d)", "-J"};
    for( int i = 0; i < 5; i++ ) wsprd_decode_options_init(&sets[i]);
    sets[1].quickmode = 1;
    sets[1].npasses = 2;
    sets[2].maxcycles = 500;
    sets[2].ndepth = 4;
    sets[3] = sets[2];
    sets[3].more_candidates = 1;
    sets[4].stackdecoder = 1;
    int const counts[] = {4, 2, 0};
    int const gui_depth[] = {0, 1, 1, 1, 0};
    static struct wsprd_decode_result serial[WSPRD_DECODE_MAX_RESULTS];
    static struct wsprd_decode_result threaded[WSPRD_DECODE_MAX_RESULTS];
    for( int s = first; s < last; s++ ) {
        struct wsprd_decode_options opt = sets[s];
        opt.threads = 1;
        struct wsprd_context *ctx = wsprd_context_new(NULL);
        CHECK(ctx != NULL, "context allocation");
        if( !ctx ) return;
        int n1 = decode_once(samples, nsamples, &opt, NULL, ctx, serial);
        wsprd_context_free(ctx);
        CHECK(n1 > 0, "%s, %s: one thread decoded %d", what, labels[s], n1);
        int found = must == NULL;
        for( int i = 0; i < n1; i++ ) found |= must != NULL && strcmp(serial[i].message, must) == 0;
        CHECK(found, "%s, %s: one thread did not decode %s", what, labels[s], must ? must : "");
        /* wsprd's 10,000 cycles a bit (defaults, -J) are slow: four threads
           there, once; every count at the GUI's depths, twice. */
        size_t ncounts = gui_depth[s] && !brief_threads() ? sizeof counts / sizeof counts[0] : 1;
        int repeats = gui_depth[s] && !brief_threads() ? 2 : 1;
        for( size_t c = 0; c < ncounts; c++ ) {
            for( int repeat = 0; repeat < repeats; repeat++ ) {
                char label[160];
                snprintf(label, sizeof label, "%s, %s, threads %d (%d), run %d", what, labels[s],
                         counts[c], wspr_parallel_threads(counts[c]), repeat + 1);
                opt.threads = counts[c];
                ctx = wsprd_context_new(NULL);
                CHECK(ctx != NULL, "context allocation");
                if( !ctx ) return;
                int n2 = decode_once(samples, nsamples, &opt, NULL, ctx, threaded);
                wsprd_context_free(ctx);
                compare_results(label, n1, serial, n2, threaded);
            }
        }
    }
}

/* One context decoding at 2, 8, the CPU count's and 2 threads in turn, the
   hash table carrying from decode to decode as in a codec session, against
   one thread in a context reused the same way. */
static void compare_reused_context(short const *samples, size_t nsamples)
{
    int const counts[] = {2, 8, 0, 2};    /* the workers grow, then fewer of them run */
    static struct wsprd_decode_result serial[4][WSPRD_DECODE_MAX_RESULTS];
    static struct wsprd_decode_result threaded[WSPRD_DECODE_MAX_RESULTS];
    for( int depth = 2; depth <= (brief_threads() ? 2 : 3); depth++ ) {
        struct wsprd_decode_options opt;
        wsprd_decode_options_init(&opt);
        opt.maxcycles = 500;
        opt.ndepth = 4;
        opt.more_candidates = depth == 3;
        int n1[4];
        opt.threads = 1;
        struct wsprd_context *one = wsprd_context_new(NULL), *many = wsprd_context_new(NULL);
        CHECK(one && many, "context allocation");
        if( !one || !many ) {
            wsprd_context_free(one);
            wsprd_context_free(many);
            return;
        }
        int decodes = brief_threads() ? 2 : 4;     /* 2, 8: the workers grow */
        for( int k = 0; k < decodes; k++ ) n1[k] = decode_once(samples, nsamples, &opt, NULL, one, serial[k]);
        for( int k = 0; k < decodes; k++ ) {
            char label[96];
            opt.threads = counts[k];
            int n2 = decode_once(samples, nsamples, &opt, NULL, many, threaded);
            snprintf(label, sizeof label, "one context, depth %d, decode %d at threads %d", depth, k + 1,
                     counts[k]);
            compare_results(label, n1[k], serial[k], n2, threaded);
        }
        wsprd_context_free(one);
        wsprd_context_free(many);
    }
}

static int row_threads(char const *wav)
{
    /* ft8_threads' rule (lib/ft8_engine_kernel.f90) on 1 to 64 cores: 0 from
       the CPU count, 1 to 12 at most the CPU count, anything else one. */
    for( int cores = 1; cores <= 64; cores++ ) {
        int automatic = cores == 1 ? 1 : cores < 5 ? cores - 1 : cores < 9 ? cores - 2
                      : cores < 16 ? cores - 3 : 12;
        CHECK(wspr_parallel_thread_rule(0, cores) == automatic, "0 on %d cores: %d threads",
              cores, wspr_parallel_thread_rule(0, cores));
        for( int n = 1; n <= 12; n++ )
            CHECK(wspr_parallel_thread_rule(n, cores) == (n < cores ? n : cores),
                  "%d on %d cores: %d threads", n, cores, wspr_parallel_thread_rule(n, cores));
        int const one[] = {-1, 13, 24, 25, 1000};
        for( size_t i = 0; i < sizeof one / sizeof one[0]; i++ )
            CHECK(wspr_parallel_thread_rule(one[i], cores) == 1, "%d on %d cores: %d threads",
                  one[i], cores, wspr_parallel_thread_rule(one[i], cores));
    }
    /* This host: the rule with OpenMP's CPU count (known when below 12), one
       without OpenMP. */
    int cores = wspr_parallel_threads(12);
    CHECK(cores >= 1 && cores <= 12, "12 requested: %d threads", cores);
    for( int n = 1; n <= 12; n++ )
        CHECK(wspr_parallel_threads(n) == (n < cores ? n : cores),
              "%d requested: %d threads, %d cores", n, wspr_parallel_threads(n), cores);
    int const one[] = {-1, 13, 24, 25, 1000};
    for( size_t i = 0; i < sizeof one / sizeof one[0]; i++ )
        CHECK(wspr_parallel_threads(one[i]) == 1, "%d requested: %d threads",
              one[i], wspr_parallel_threads(one[i]));
    int automatic = wspr_parallel_threads(0);
    if( cores < 12 ) {
        int expected = cores <= 1 ? 1 : cores < 5 ? cores - 1 : cores < 9 ? cores - 2 : cores - 3;
        CHECK(automatic == expected, "0 requested: %d threads, %d cores", automatic, cores);
    } else {
        CHECK(automatic >= 6 && automatic <= 12, "0 requested: %d threads", automatic);
    }
    printf("threads: up to %d here, %d chosen from the CPU count\n", cores, automatic);

    size_t nsamples = 0;
    short *samples = read_wav(wav, &nsamples);
    if( !samples ) return 1;
    compare_thread_counts("sample", samples, nsamples, brief_threads() ? 2 : 0,
                          brief_threads() ? 3 : 5, NULL);
    compare_reused_context(samples, nsamples);
    free(samples);

    /* Eight stations, the type 3 among them strongest after its type 1, so
       that it is tried in the same pass as the type 1 that resolves it. */
    struct signal crowd[] = {
        {"K1ABC FN42 37", 0, 0, -95.0, 260.0},
        {"<K1ABC> FN42AX 37", 0, 0, -60.0, 240.0},
        {"PJ4/W9XYZ 23", 0, 0, -30.0, 180.0},
        {"G4ABC/P 10", 0, 0, 5.0, 150.0},
        {"W5XYZ EM10 33", 0, 0, 30.0, 120.0},
        {"VK2XYZ QF56 20", 0, 0, 55.0, 100.0},
        {"DL1ABC JO62 30", 0, 0, 80.0, 90.0},
        {"JA1ABC PM95 40", 0, 0, -10.0, 70.0},
    };
    double sigma = sqrt(1000.0 * 1000.0 / 2 / pow(10.0, -14.0 / 10) * 6000.0 / 2500.0);
    samples = synthesize_signals(crowd, (int)(sizeof crowd / sizeof crowd[0]), sigma);
    CHECK(samples != NULL, "synthesis");
    if( !samples ) return 1;
    /* The GUI's depths: at 10,000 cycles a bit (wsprd's defaults, -J) its
       weak candidates take minutes. */
    compare_thread_counts("crowded period", samples, wsprd_downsample_points(2),
                          brief_threads() ? 2 : 1, brief_threads() ? 3 : 4,
                          "<K1ABC> FN42AX 37");
    free(samples);
    return 0;
}

/* osd_threads: noisy codewords of random messages, OSD at the GUI's depth. */
enum { OSD_WORDS = 96, OSD_SYMBOLS = 162 };
struct osd_job {
    int used[64];           /* the thread numbers that ran a word */
    int *boxes[8];          /* each thread's pattern boxes, WSPR_OSD_BOX_INTS */
    float received[OSD_WORDS][OSD_SYMBOLS];
    unsigned char cw[OSD_WORDS][OSD_SYMBOLS];
    int nhard[OSD_WORDS];
    float dmin[OSD_WORDS];
};

static void osd_word(void *arg, int k, int thread)
{
    struct osd_job *job = arg;
    unsigned char apmask[OSD_SYMBOLS] = {0};
    float received[OSD_SYMBOLS];
    int depth = 4;          /* -o 4, the GUI's depths 2 and 3 */
    if( thread >= 0 && thread < 64 ) job->used[thread] = 1;
    memcpy(received, job->received[k], sizeof received);
    osdwspr_(received, apmask, &depth, job->cw[k], &job->nhard[k], &job->dmin[k],
             job->boxes[thread]);
}

static int row_osd_threads(void)
{
    static struct osd_job serial, threaded;
    unsigned long long state = 2718281828ULL;
    double const pi = 3.14159265358979323846;
    for( int k = 0; k < OSD_WORDS; k++ ) {
        unsigned char message[11] = {0}, symbols[sizeof message * 16];
        for( int i = 0; i < 7; i++ ) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            message[i] = (unsigned char)(state >> 56);
        }
        message[6] &= 0xC0;     /* 50 bits, then the tail */
        encode(symbols, message, sizeof message);
        for( int i = 0; i < OSD_SYMBOLS; i++ ) {
            double u[2];
            for( int j = 0; j < 2; j++ ) {
                state = state * 6364136223846793005ULL + 1442695040888963407ULL;
                u[j] = ((state >> 11) + 0.5) / 9007199254740992.0;
            }
            double v = (symbols[i] ? 40.0 : -40.0) + 45.0 * sqrt(-2 * log(u[0])) * cos(2 * pi * u[1]);
            serial.received[k][i] = (float)(v > 127 ? 127 : v < -128 ? -128 : v);
        }
    }
    memcpy(threaded.received, serial.received, sizeof serial.received);
    /* Each thread's pattern boxes, as the decoder gives each of its threads. */
    for( int t = 0; t < 8; t++ ) {
        serial.boxes[t] = malloc(WSPR_OSD_BOX_INTS * sizeof *serial.boxes[t]);
        CHECK(serial.boxes[t] != NULL, "no memory for pattern boxes");
        if( !serial.boxes[t] ) return 1;
    }
    memcpy(threaded.boxes, serial.boxes, sizeof serial.boxes);
    /* The threads first: the process's first OSD calls fill osdwspr's
       generator under its lock, at once. */
    static struct osd_job first;
    memcpy(first.received, serial.received, sizeof serial.received);
    memcpy(first.boxes, serial.boxes, sizeof serial.boxes);
    memset(first.used, 0, sizeof first.used);
    wspr_parallel_for(OSD_WORDS, 8, osd_word, &first);
    for( int k = 0; k < OSD_WORDS; k++ ) osd_word(&serial, k, 0);
    for( int k = 0; k < OSD_WORDS; k++ )
        CHECK(memcmp(serial.cw[k], first.cw[k], OSD_SYMBOLS) == 0 &&
              serial.nhard[k] == first.nhard[k] && serial.dmin[k] == first.dmin[k],
              "first run, word %d: the threads' result is not the one-at-a-time one", k);
    memcpy(threaded.used, first.used, sizeof threaded.used);
    for( int run = 0; run < 30; run++ ) {
        memset(threaded.cw, 0, sizeof threaded.cw);
        wspr_parallel_for(OSD_WORDS, 8, osd_word, &threaded);
        for( int k = 0; k < OSD_WORDS; k++ )
            CHECK(memcmp(serial.cw[k], threaded.cw[k], OSD_SYMBOLS) == 0 &&
                  serial.nhard[k] == threaded.nhard[k] && serial.dmin[k] == threaded.dmin[k],
                  "run %d, word %d: %d hard errors at distance %g one at a time, %d at %g on threads"
                  " (or another codeword)", run + 1, k, serial.nhard[k], serial.dmin[k],
                  threaded.nhard[k], threaded.dmin[k]);
    }
    /* Eight threads ran, not one: the row is registered with dynamic teams
       on and one thread by default (OMP_DYNAMIC, OMP_NUM_THREADS), which the
       shim overrides. Without OpenMP one runs, and the shim cannot override a
       thread limit (OMP_THREAD_LIMIT, OMP_MAX_ACTIVE_LEVELS). */
    int ran = 0;
    for( int t = 0; t < 64; t++ ) ran += threaded.used[t];
    if( wspr_parallel_cpus() > 1 && !getenv("OMP_THREAD_LIMIT") && !getenv("OMP_MAX_ACTIVE_LEVELS") )
        CHECK(ran > 1, "one thread ran the 96 words");
    printf("osd_threads: %d threads ran\n", ran);
    for( int t = 0; t < 8; t++ ) free(serial.boxes[t]);
    return 0;
}

int main(int argc, char **argv)
{
    int rc = 2;
    if( argc == 3 && strcmp(argv[1], "repeat") == 0 ) {
        rc = row_repeat(argv[2]);
    } else if( argc == 4 && strcmp(argv[1], "hash_roundtrip") == 0 ) {
        rc = row_hash_roundtrip(argv[2], argv[3]);
    } else if( argc == 4 && strcmp(argv[1], "run_file") == 0 ) {
        rc = row_run_file(argv[2], argv[3]);
    } else if( argc == 4 && strcmp(argv[1], "short_name") == 0 ) {
        rc = row_short_name(argv[2], argv[3]);
    } else if( argc == 2 && strcmp(argv[1], "unresolved_type3") == 0 ) {
        rc = row_unresolved_type3();
    } else if( argc == 3 && strcmp(argv[1], "type2_grid") == 0 ) {
        rc = row_type2_grid(argv[2]);
    } else if( argc == 2 && strcmp(argv[1], "unencodable_text") == 0 ) {
        rc = row_unencodable_text();
    } else if( argc == 2 && strcmp(argv[1], "calibration_passes") == 0 ) {
        rc = row_calibration_passes();
    } else if( argc == 2 && strcmp(argv[1], "osd_distance") == 0 ) {
        rc = row_osd_distance();
    } else if( argc == 2 && strcmp(argv[1], "decoder_api") == 0 ) {
        rc = row_decoder_api();
    } else if( argc == 3 && strcmp(argv[1], "threads") == 0 ) {
        rc = row_threads(argv[2]);
    } else if( argc == 2 && strcmp(argv[1], "osd_threads") == 0 ) {
        rc = row_osd_threads();
    } else {
        fprintf(stderr, "usage: %s repeat <wav> | hash_roundtrip <wav> <dir> | run_file <wav> <dir>"
                " | short_name <wav> <dir> | unresolved_type3 | type2_grid <dir>"
                " | unencodable_text | calibration_passes | osd_distance | decoder_api"
                " | threads <wav> | osd_threads\n", argv[0]);
        return 2;
    }
    if( rc == 0 && failures == 0 ) {
        printf("%s: passed\n", argv[1]);
        return 0;
    }
    fprintf(stderr, "%s: %d failure(s)\n", argv[1], failures);
    return 1;
}
