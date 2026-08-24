#include "q65.h"

#include <stdio.h>
#include <stdlib.h>

static void require (int condition, char const * message)
{
  if (!condition)
    {
      fprintf (stderr, "FAIL: %s\n", message);
      exit (1);
    }
}

int main (void)
{
  float intrinsics[64] = {0};
  int symbol = 0;

  for (int i = 0; i < 64; ++i)
    intrinsics[i] = 1.0f;

  require (q65_check_llh (NULL, &symbol, 1, 64, intrinsics) == 1,
           "valid codeword symbol rejected");

  symbol = -1;
  require (q65_check_llh (NULL, &symbol, 1, 64, intrinsics) == 0,
           "negative codeword symbol accepted");

  symbol = 64;
  require (q65_check_llh (NULL, &symbol, 1, 64, intrinsics) == 0,
           "out-of-range codeword symbol accepted");

  return 0;
}
