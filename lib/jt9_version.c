#include <stdio.h>

#include "scs_version.h"
#include "wsjtx_config.h"

#ifndef JT9_PROGRAM_NAME
#define JT9_PROGRAM_NAME "jt9"
#endif

void jt9_print_version(void)
{
  printf("%s %d.%d.%d%s", JT9_PROGRAM_NAME,
         PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH,
         BUILD_TYPE_REVISION);
  if (SCS_VERSION_STR[0] != '\0') {
    printf(" (revision %s)\n", SCS_VERSION_STR);
  } else {
    puts(" (revision unavailable)");
  }
}
