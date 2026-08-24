#include <stdio.h>

#include "scs_version.h"
#include "wsjtx_config.h"

void print_version(const char *prog_name)
{
  printf(" Program:  %s %d.%d.%d%s", prog_name,
         PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH,
         BUILD_TYPE_REVISION);
  if (SCS_VERSION_STR[0] != '\0') {
    printf(" (revision %s)\n", SCS_VERSION_STR);
  } else {
    puts(" (revision unavailable)");
  }
}
