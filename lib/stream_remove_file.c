/* stream_remove_file.c — delete a file the codec stream owns.
 *
 * A missing file counts as deleted. The stream must not ask gfortran whether
 * the file exists: on Windows an INQUIRE(FILE=) of an existing file ends
 * wsjt-codec's stdin (lib/stream_stdin.c), its next read returning end of
 * input.
 *
 * Called via bind(C) from lib/streaming_receive.f90, which is in the shared
 * Fortran library and linked into jt9 as well as wsjt-codec, so this file
 * belongs to the shared C sources, not wsjt-codec's own.
 */

#include <errno.h>
#include <stdio.h>

/* 0 once path is gone, -1 if it is still there. */
int stream_remove_file(char const *path)
{
  return 0 == remove(path) || ENOENT == errno ? 0 : -1;
}
