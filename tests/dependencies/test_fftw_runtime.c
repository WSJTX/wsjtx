#include <fftw3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  const int size = 32;
  float *input;
  fftwf_complex *output;
  fftwf_plan plan;
  int result = 0;
  int i;

  printf("FFTW runtime: %s\n", fftwf_version);
  if (argc > 1 && argv[1][0]) {
    char expected[64];
    size_t length;
    snprintf(expected, sizeof(expected), "fftw-%s", argv[1]);
    length = strlen(expected);
    if (strncmp(fftwf_version, expected, length) != 0 ||
        (fftwf_version[length] != '\0' && fftwf_version[length] != '-')) {
      fprintf(stderr, "Expected FFTW %s\n", argv[1]);
      return 1;
    }
  }
  if (!fftwf_init_threads()) {
    fprintf(stderr, "FFTW thread initialization failed\n");
    return 1;
  }
  fftwf_plan_with_nthreads(2);
  input = fftwf_alloc_real(size);
  output = fftwf_alloc_complex(size / 2 + 1);
  if (!input || !output) {
    fprintf(stderr, "FFTW allocation failed\n");
    fftwf_free(output);
    fftwf_free(input);
    fftwf_cleanup_threads();
    return 1;
  }
  plan = fftwf_plan_dft_r2c_1d(size, input, output, FFTW_ESTIMATE);
  if (!plan) {
    fprintf(stderr, "FFTW threaded single-precision plan failed\n");
    result = 1;
  } else {
    memset(input, 0, size * sizeof(*input));
    input[0] = 1.0f;
    fftwf_execute(plan);
    for (i = 0; i <= size / 2; ++i) {
      if (!isfinite(output[i][0]) || !isfinite(output[i][1]) ||
          fabsf(output[i][0] - 1.0f) > 1.0e-5f || fabsf(output[i][1]) > 1.0e-5f) {
        fprintf(stderr, "FFTW transform failed at bin %d\n", i);
        result = 1;
        break;
      }
    }
    fftwf_destroy_plan(plan);
  }
  fftwf_free(output);
  fftwf_free(input);
  fftwf_cleanup_threads();
  return result;
}
