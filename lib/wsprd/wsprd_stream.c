/*
 * wsprd streaming layer.
 *
 * Adds `wsprd --stream` mode: read PCM audio from stdin (per the
 * streaming framing) instead of from a .wav/.c2 file, run the
 * standard wsprd decode pipeline, and emit decodes as NDJSON on stdout
 * (matching the jt9 --stream protocol).
 *
 * Streaming protocol summary:
 *   1. WSJL session header (8 bytes): magic 'WSJL' + fmt + ch + rate_kHz
 *      LE u16. fmt=0x00 = int16 PCM (same as jt9). For wsprd, audio is
 *      always 12 kHz mono int16.
 *   2. Frames: [type:1][len:4 LE][body:len]
 *        type 0x01 = audio chunk (int16 PCM samples)
 *        type 0x02 = control JSON (configure / halt)
 *   3. Configure frame schema (subset of jt9):
 *        {"t":"configure","date":"YYMMDD","time":"HHMM",
 *         "dialfreq":14.0956,"wspr_type":2,
 *         "mycall":"K1JT","mygrid":"FN20"}
 *      The configure frame must arrive before enough audio for one period.
 *   4. Halt frame: {"t":"halt"} — drains current period if data buffered,
 *      emits decode_finished, exits clean.
 *
 * NDJSON schema (v=1, mirrors jt9):
 *   {"v":1,"t":"ready","modes":["WSPR"],"protocol":1}
 *   {"v":1,"t":"decode","mode":"WSPR","date":"YYMMDD","time":"HHMM",
 *    "snr":-12,"dt":0.6,"freq":14.097103,"drift":0,"message":"K1JT FN20 33"}
 *   {"v":1,"t":"decode_finished","date":"YYMMDD","time":"HHMM"}
 *   {"v":1,"t":"error","msg":"..."}
 *
 * Scope:
 *   - One period per session. Halt after the first period's decode.
 *     Multi-period (continuous) is a refinement.
 *   - PCM input only (fmt=0x00). C2 baseband (fmt=0x02) is a refinement.
 *   - Hand-rolled JSON parsing (strstr-based). Acceptable for the closed
 *     configure schema.
 *   - All diagnostics go to stderr (consumers MUST drain).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <fftw3.h>

#include "wsprd_stream.h"

#define WSJL_MAGIC      "WSJL"
#define WSJL_FMT_PCM    0x00
#define FRAME_AUDIO     0x01
#define FRAME_CONTROL   0x02

// Buffer the producer's audio chunks. Sized for WSPR-15 worst case
// (8 * 114 * 12000 = 10,944,000 samples). WSPR-2 needs only 1,368,000.
#define MAX_PCM_SAMPLES (8 * 114 * 12000)

// ============================================================
// JSON parsing — strstr-based, schema-restricted (no escapes,
// no nesting, ASCII only). Mirrors the streaming control-frame
// parser's scope discipline.
// ============================================================

static const char *json_find_key(const char *json, const char *key) {
    // Searches for "key" pattern. Returns pointer at the value position
    // (just after the colon following the quoted key) or NULL.
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    return *p ? p : NULL;
}

static int json_get_string(const char *json, const char *key,
                           char *out, size_t out_size) {
    const char *p = json_find_key(json, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (*p == '"') ? 0 : -1;
}

static int json_get_int(const char *json, const char *key, int *out) {
    const char *p = json_find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return -1;
    *out = (int) v;
    return 0;
}

static int json_get_double(const char *json, const char *key, double *out) {
    const char *p = json_find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return -1;
    *out = v;
    return 0;
}

// ============================================================
// Frame I/O. POSIX-only (Linux + macOS).
// ============================================================

static int wsjl_header_read(FILE *fp) {
    unsigned char hdr[8];
    if (fread(hdr, 1, 8, fp) != 8) {
        fprintf(stderr, "wsprd --stream: short read on WSJL header\n");
        return -1;
    }
    if (memcmp(hdr, WSJL_MAGIC, 4) != 0) {
        fprintf(stderr, "wsprd --stream: bad magic; expected 'WSJL'\n");
        return -1;
    }
    unsigned char fmt = hdr[4];
    unsigned char ch = hdr[5];
    uint16_t rate_khz = (uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8);
    if (fmt != WSJL_FMT_PCM) {
        fprintf(stderr,
                "wsprd --stream: unsupported fmt=0x%02x (only 0x00 PCM today)\n",
                fmt);
        return -1;
    }
    fprintf(stderr,
            "wsprd --stream: header ok (fmt=%u ch=%u rate=%u kHz)\n",
            fmt, ch, rate_khz);
    return 0;
}

// Reads one frame envelope. *body is malloc'd on success; caller frees.
// Returns 0 ok, -1 err, +1 EOF (clean producer-disconnect).
static int frame_read(FILE *fp, unsigned char *type, void **body, uint32_t *len) {
    unsigned char hdr[5];
    size_t nr = fread(hdr, 1, 5, fp);
    if (nr == 0) return 1;  // clean EOF
    if (nr != 5) {
        fprintf(stderr, "wsprd --stream: short read on frame header\n");
        return -1;
    }
    *type = hdr[0];
    *len = (uint32_t)hdr[1] | ((uint32_t)hdr[2] << 8) |
           ((uint32_t)hdr[3] << 16) | ((uint32_t)hdr[4] << 24);
    if (*len == 0) {
        *body = NULL;
        return 0;
    }
    *body = malloc(*len);
    if (!*body) {
        fprintf(stderr, "wsprd --stream: alloc %u bytes failed\n", *len);
        return -1;
    }
    if (fread(*body, 1, *len, fp) != *len) {
        fprintf(stderr, "wsprd --stream: short read on frame body (%u bytes)\n",
                *len);
        free(*body);
        *body = NULL;
        return -1;
    }
    return 0;
}

// ============================================================
// PCM → I/Q FFT/extract/IFFT (mirrors readwavfile lines 155-188).
// ============================================================

static unsigned long pcm_to_iq(int16_t *pcm, size_t npcm, int ntrmin,
                               float *idat, float *qdat) {
    int nfft1, nfft2 = 46080, nh2 = nfft2 / 2, i0;
    double df;
    size_t expected_npcm;

    if (ntrmin == 2) {
        nfft1 = nfft2 * 32;
        df = 12000.0 / nfft1;
        i0 = 1500.0 / df + 0.5;
        expected_npcm = 114 * 12000;
    } else if (ntrmin == 15) {
        nfft1 = nfft2 * 8 * 32;
        df = 12000.0 / nfft1;
        i0 = (1500.0 + 112.5) / df + 0.5;
        expected_npcm = 8 * 114 * 12000;
    } else {
        fprintf(stderr, "wsprd --stream: invalid wspr_type=%d\n", ntrmin);
        return 1;
    }

    if (npcm < expected_npcm) {
        fprintf(stderr,
                "wsprd --stream: short PCM (got %zu, expected %zu for WSPR-%d)\n",
                npcm, expected_npcm, ntrmin);
        return 1;
    }

    float *realin = fftwf_malloc(sizeof(float) * nfft1);
    fftwf_complex *fftout = fftwf_malloc(sizeof(fftwf_complex) * (nfft1 / 2 + 1));
    fftwf_plan plan_fwd = fftwf_plan_dft_r2c_1d(nfft1, realin, fftout, FFTW_ESTIMATE);

    for (size_t i = 0; i < expected_npcm; i++) realin[i] = pcm[i] / 32768.0f;
    for (size_t i = expected_npcm; i < (size_t)nfft1; i++) realin[i] = 0.0f;

    fftwf_execute(plan_fwd);
    fftwf_destroy_plan(plan_fwd);
    fftwf_free(realin);

    fftwf_complex *fftin = fftwf_malloc(sizeof(fftwf_complex) * nfft2);
    for (size_t i = 0; i < (size_t)nfft2; i++) {
        size_t j = i0 + i;
        if (i > (size_t)nh2) j = j - nfft2;
        fftin[i][0] = fftout[j][0];
        fftin[i][1] = fftout[j][1];
    }
    fftwf_free(fftout);

    fftwf_complex *fftout2 = fftwf_malloc(sizeof(fftwf_complex) * nfft2);
    fftwf_plan plan_inv = fftwf_plan_dft_1d(nfft2, fftin, fftout2, FFTW_BACKWARD,
                                            FFTW_ESTIMATE);
    fftwf_execute(plan_inv);
    fftwf_destroy_plan(plan_inv);

    for (size_t i = 0; i < (size_t)nfft2; i++) {
        idat[i] = fftout2[i][0] / 1000.0f;
        qdat[i] = fftout2[i][1] / 1000.0f;
    }
    fftwf_free(fftin);
    fftwf_free(fftout2);
    return (unsigned long) nfft2;
}

// ============================================================
// Public API (declared in wsprd_stream.h).
// ============================================================

void wsprd_stream_emit_ready(void) {
    fputs("{\"v\":1,\"t\":\"ready\",\"modes\":[\"WSPR\"],\"protocol\":1}\n",
          stdout);
    fflush(stdout);
}

// JSON-escape a string into out. Best-effort: handles ASCII printable
// and the must-escape control chars per RFC 8259.
static void json_escape_into(const char *src, char *out, size_t out_size) {
    size_t o = 0;
    for (const char *p = src; *p && o + 6 < out_size; p++) {
        unsigned char c = (unsigned char) *p;
        if (c == '"' || c == '\\') {
            if (o + 2 < out_size) { out[o++] = '\\'; out[o++] = c; }
        } else if (c < 0x20) {
            o += snprintf(out + o, out_size - o, "\\u%04x", c);
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

void wsprd_stream_emit_decode(const char *date, const char *uttime,
                              float snr, float dt, double freq,
                              int drift, const char *message) {
    char esc_msg[80];
    json_escape_into(message ? message : "", esc_msg, sizeof esc_msg);
    fprintf(stdout,
            "{\"v\":1,\"t\":\"decode\",\"mode\":\"WSPR\","
            "\"date\":\"%s\",\"time\":\"%s\","
            "\"snr\":%.0f,\"dt\":%.1f,\"freq\":%.6f,"
            "\"drift\":%d,\"message\":\"%s\"}\n",
            date ? date : "",
            uttime ? uttime : "",
            snr, dt, freq, drift, esc_msg);
    fflush(stdout);
}

void wsprd_stream_emit_decode_finished(const char *date, const char *uttime) {
    fprintf(stdout,
            "{\"v\":1,\"t\":\"decode_finished\","
            "\"date\":\"%s\",\"time\":\"%s\"}\n",
            date ? date : "",
            uttime ? uttime : "");
    fflush(stdout);
}

void wsprd_stream_emit_error(const char *msg) {
    char esc[256];
    json_escape_into(msg ? msg : "", esc, sizeof esc);
    fprintf(stdout, "{\"v\":1,\"t\":\"error\",\"msg\":\"%s\"}\n", esc);
    fflush(stdout);
}

// Read header + frames from stdin until configure + enough audio + halt
// (or audio-exhaustion + EOF) are received. Populates *cfg and the
// supplied I/Q buffers via FFT/extract/IFFT. Returns nfft2 (number of
// complex samples populated) on success; 1 on protocol error.
unsigned long wsprd_stream_read(struct wsprd_stream_config *cfg,
                                float *idat, float *qdat) {
    if (wsjl_header_read(stdin) != 0) return 1;

    int16_t *pcm = calloc(MAX_PCM_SAMPLES, sizeof(int16_t));
    if (!pcm) {
        wsprd_stream_emit_error("alloc pcm buffer failed");
        return 1;
    }

    cfg->configure_received = 0;
    int halted = 0;
    size_t pcm_n = 0;

    while (!halted) {
        unsigned char type;
        void *body = NULL;
        uint32_t len = 0;
        int rc = frame_read(stdin, &type, &body, &len);
        if (rc < 0) {
            free(pcm);
            wsprd_stream_emit_error("frame read failed");
            return 1;
        }
        if (rc > 0) break;  // EOF without halt — treat as drain-and-decode

        if (type == FRAME_CONTROL) {
            // body is a JSON line (no trailing newline). NUL-terminate
            // for strstr safety.
            char *json = malloc(len + 1);
            memcpy(json, body, len);
            json[len] = '\0';
            free(body);

            if (strstr(json, "\"halt\"")) {
                halted = 1;
                free(json);
                continue;
            }
            if (strstr(json, "\"configure\"")) {
                json_get_string(json, "date", cfg->date, sizeof cfg->date);
                json_get_string(json, "time", cfg->uttime, sizeof cfg->uttime);
                json_get_double(json, "dialfreq", &cfg->dialfreq);
                if (json_get_int(json, "wspr_type", &cfg->wspr_type) != 0) {
                    cfg->wspr_type = 2;
                }
                json_get_string(json, "mycall", cfg->mycall, sizeof cfg->mycall);
                json_get_string(json, "mygrid", cfg->mygrid, sizeof cfg->mygrid);
                cfg->configure_received = 1;
            }
            free(json);
            continue;
        }

        if (type == FRAME_AUDIO) {
            // body is raw int16 LE PCM. len must be even (whole samples).
            size_t nsamples = len / sizeof(int16_t);
            if (pcm_n + nsamples > MAX_PCM_SAMPLES) {
                nsamples = MAX_PCM_SAMPLES - pcm_n;
            }
            memcpy(pcm + pcm_n, body, nsamples * sizeof(int16_t));
            pcm_n += nsamples;
            free(body);
            continue;
        }

        // Unknown frame type — ignore (forward-compat).
        free(body);
    }

    if (!cfg->configure_received) {
        free(pcm);
        wsprd_stream_emit_error("no configure frame received before halt/EOF");
        return 1;
    }

    unsigned long npoints = pcm_to_iq(pcm, pcm_n, cfg->wspr_type, idat, qdat);
    free(pcm);
    return npoints;
}
