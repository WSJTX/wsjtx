/* stream_setmode.c — stdin helpers for wsjt-codec's stream.
 *
 * wsjt-codec reads a framed binary byte stream (PCM audio + control frames)
 * from stdin. On Windows, stdin defaults to text mode, which translates
 * CR/LF and stops at a 0x1A (^Z) byte — both corrupt binary data.
 * stream_set_stdin_binary forces binary mode once, before the stream is
 * opened. On POSIX (Linux, macOS) there is no text/binary distinction for
 * stdin, so it compiles to a no-op.
 *
 * stream_stdin_is_terminal tells whether stdin is a terminal rather than a
 * stream (lib/jt9.f90).
 *
 * Called via bind(C) interfaces from lib/streaming_io.f90 and lib/jt9.f90.
 * C linkage gives the exact symbol name (no Fortran trailing-underscore
 * mangling), matched by bind(C, name="...").
 */
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

void stream_set_stdin_binary(void)
{
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
}

int stream_stdin_is_terminal(void)
{
#ifdef _WIN32
  /* _isatty is true for every character device, NUL included. */
  DWORD mode;
  return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
#else
  return isatty(STDIN_FILENO);
#endif
}
