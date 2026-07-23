/* stream_stdin.c — portable read-fully helper for the framed stdin
 * protocol (lib/streaming_io.f90).
 *
 * Fortran unit I/O cannot read a redirected stdin portably: native
 * Windows has no /dev/stdin, and re-OPENing the preconnected unit 5 with
 * ACCESS='STREAM' is non-conforming (F2018 12.5.6.1 — ACCESS is not a
 * changeable mode of a connected unit), so libgfortran rejects it on
 * every platform. This helper reads file descriptor 0 directly instead,
 * and is used on ALL platforms — one code path, exercised by the POSIX
 * fixtures suites, no #ifdef branch that CI never runs.
 *
 * The loop retries until the full request is delivered or the stream
 * truly ends (read() == 0, or a hard error). A pipe short-read — which
 * a small fast producer can trigger, and which Fortran stream input
 * surfaces as an I/O error — therefore can never be mistaken for EOF:
 * a short return here means the producer closed the pipe.
 *
 * Windows: binary mode must be forced on fd 0 before the first read
 * (stream_set_stdin_binary in lib/stream_setmode.c, called from
 * jt9_stream's setup) or the CRT stops at 0x1A and rewrites CRLF;
 * _read() honors that mode. Called via bind(C) from Fortran.
 */
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

int64_t stream_stdin_read_fully(void *buf, int64_t n)
{
  char *p = (char *) buf;
  int64_t total = 0;
  while (total < n) {
    int64_t want = n - total;
    if (want > ((int64_t) 1 << 20)) want = (int64_t) 1 << 20;
#ifdef _WIN32
    int r = _read(0, p + total, (unsigned int) want);
#else
    ssize_t r = read(0, p + total, (size_t) want);
    if (r < 0 && errno == EINTR) continue;
#endif
    if (r <= 0) break;
    total += (int64_t) r;
  }
  return total;
}
