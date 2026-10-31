#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

void export_wisdom_(char fname[], int len)
{
  int fftwf_export_wisdom_to_filename(const char *);
  fname[len-1]=0;
  fftwf_export_wisdom_to_filename(fname);
}

void import_wisdom_(char fname[], int *success, int len)
{
  int fftwf_import_wisdom_from_filename(const char *);
  fname[len-1]=0;
  *success = fftwf_import_wisdom_from_filename(fname);
}

/* FFTW's wisdom exported to a file beside path, named for this process, which
 * then replaces path: 1, or 0 with path as it was. The caller holds what
 * serializes FFTW's planner (lib/fftw_wisdom.f90). */
int wsjt_save_wisdom(const char *path)
{
  int fftwf_export_wisdom_to_filename(const char *);
  size_t size = strlen(path) + 32;
  char *temporary = malloc(size);
  int saved = 0;
  if (!temporary) return 0;
#ifdef _WIN32
  snprintf(temporary, size, "%s.%d.tmp", path, _getpid());
  if (fftwf_export_wisdom_to_filename(temporary))
    saved = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
  snprintf(temporary, size, "%s.%ld.tmp", path, (long) getpid());
  if (fftwf_export_wisdom_to_filename(temporary))
    saved = rename(temporary, path) == 0;
#endif
  if (!saved) remove(temporary);
  free(temporary);
  return saved;
}
