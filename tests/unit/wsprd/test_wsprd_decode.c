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
 *   test_wsprd_decode decoder_api
 *     wspr_decoder's knowledge import checks, its status codes, no reception
 *     after a failed prepare, and the 12 kHz transform planned and destroyed
 *     in each prepare.
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

#include <math.h>

#include "lib/wsprd/wsprd_decode.h"
#include "lib/wsprd/wspr_decoder.h"
#include "lib/wsprd/nhash.h"
#include "lib/wsprd/wsprsim_utils.h"

static int failures = 0;

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
    } else if( argc == 2 && strcmp(argv[1], "decoder_api") == 0 ) {
        rc = row_decoder_api();
    } else {
        fprintf(stderr, "usage: %s repeat <wav> | hash_roundtrip <wav> <dir> | run_file <wav> <dir>"
                " | short_name <wav> <dir> | unresolved_type3 | type2_grid <dir>"
                " | unencodable_text | decoder_api\n", argv[0]);
        return 2;
    }
    if( rc == 0 && failures == 0 ) {
        printf("%s: passed\n", argv[1]);
        return 0;
    }
    fprintf(stderr, "%s: %d failure(s)\n", argv[1], failures);
    return 1;
}
