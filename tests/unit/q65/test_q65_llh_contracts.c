#include "q65.h"

#include <math.h>
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
  float llh = 123.0f;
  require (q65_check_llh (&llh, &symbol, 1, 64, intrinsics) == 0,
           "negative codeword symbol accepted");
  require (isinf (llh) && llh < 0.0f,
           "invalid codeword symbol left LLH undefined");

  symbol = 64;
  llh = 123.0f;
  require (q65_check_llh (&llh, &symbol, 1, 64, intrinsics) == 0,
           "out-of-range codeword symbol accepted");
  require (isinf (llh) && llh < 0.0f,
           "out-of-range codeword symbol left LLH undefined");

  return 0;
}
