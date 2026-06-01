/* stream_setmode.c — put stdin into binary mode on Windows.
 *
 * jt9 --stream / jt9stream read a framed binary byte stream (PCM audio +
 * control frames) from stdin. On Windows, stdin defaults to text mode, which
 * translates CR/LF and stops at a 0x1A (^Z) byte — both corrupt binary data.
 * This helper forces binary mode once, before the stream is opened.
 *
 * On POSIX (Linux, macOS) there is no text/binary distinction for stdin, so
 * this compiles to a no-op. Called via a bind(C) interface from
 * lib/streaming_io.f90. C linkage gives the exact symbol name (no Fortran
 * trailing-underscore mangling), matched by bind(C, name="...").
 */
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#endif

void stream_set_stdin_binary(void)
{
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
}
