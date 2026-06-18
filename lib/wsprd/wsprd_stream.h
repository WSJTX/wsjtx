/*
 * wsprd streaming layer — public API consumed by wsprd.c.
 * See wsprd_stream.c for protocol + scope notes.
 */
#ifndef WSPRD_STREAM_H
#define WSPRD_STREAM_H

struct wsprd_stream_config {
    char date[7];        // YYMMDD\0
    char uttime[5];      // HHMM\0
    double dialfreq;     // MHz
    int wspr_type;       // 2 or 15
    char mycall[13];     // up to 12-char callsign + NUL
    char mygrid[7];      // up to 6-char grid + NUL
    int configure_received;
};

void wsprd_stream_emit_ready(void);
void wsprd_stream_emit_decode(const char *date, const char *uttime,
                              float snr, float dt, double freq,
                              int drift, const char *message);
void wsprd_stream_emit_decode_finished(const char *date, const char *uttime);
void wsprd_stream_emit_error(const char *msg);

// Reads WSJT header + frames from stdin until halt or EOF. On success,
// populates *cfg and the supplied I/Q buffers (idat/qdat must hold
// at least 46080 floats each). Returns the number of complex samples
// populated (nfft2 = 46080), or 1 on protocol error.
unsigned long wsprd_stream_read(struct wsprd_stream_config *cfg,
                                float *idat, float *qdat);

#endif
