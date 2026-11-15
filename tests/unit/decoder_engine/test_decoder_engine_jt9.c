#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "DecoderEngine.h"
#include <fenv.h>
#include <fftw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#ifdef __linux__
#include <link.h>
#endif

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

enum { SAMPLE_COUNT = 720000 };
extern void decoder_engine_test_jt9_signal(int submode, int start_sample, float spread_hz,
                                         float frequency_hz, int16_t *samples);
extern void decoder_engine_test_jt9_reference(int submode, int windowed, const int16_t *samples, float result[4]);

typedef struct {
  decoder_engine_handle engine;
  decoder_attempt_request request;
  decoder_audio_view audio;
  int16_t *samples, *before;
  int count, found, compare_reference, check_dt;
  float reference[4], expected_dt, expected_frequency, frequency_tolerance;
} fixture;

static void observe(const decoder_observation *record, void *opaque)
{
  fixture *f = opaque;
  CHECK(record->input_id == f->request.input_id);
  CHECK(record->analysis_id == f->request.analysis_id);
  CHECK(record->attempt_no == f->request.attempt_no);
  CHECK(record->mode == DECODER_MODE_JT9);
  CHECK(record->variant == f->request.jt9.submode);
  CHECK(record->message[0] != '\0' && record->message[0] != ' ');
  CHECK(isfinite(record->sync) && isfinite(record->dt_seconds));
  CHECK(record->ft8.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(record->ft4.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(record->superfox.kind == 0);
  ++f->count;
  if (!strcmp(record->message, "CQ K1JT FN20")) {
    ++f->found;
    if (fabsf(record->frequency_hz - f->expected_frequency) >= f->frequency_tolerance) {
      fprintf(stderr, "JT9%c input %lld attempt %d: frequency %.6f Hz, expected %.3f +/- %.3f Hz\n",
              'A' + f->request.jt9.submode, (long long)f->request.input_id,
              f->request.attempt_no, record->frequency_hz, f->expected_frequency,
              f->frequency_tolerance);
    }
    CHECK(fabsf(record->frequency_hz - f->expected_frequency) < f->frequency_tolerance);
    if (f->check_dt) CHECK(fabsf(record->dt_seconds - f->expected_dt) < 0.1f);
    CHECK(record->jt9.has_drift == (f->request.jt9.submode == 0));
    CHECK(isfinite(record->jt9.drift_hz_per_minute));
    if (record->jt9.has_drift) CHECK(fabsf(record->jt9.drift_hz_per_minute) < 2.0f);
    if (f->compare_reference) {
      /* FFT layout roundoff is amplified by wide-mode sync normalization. */
      const float sync_tolerance = 0.001f + 0.0001f * fabsf(f->reference[2]);
      if (fabsf(record->sync - f->reference[2]) >= sync_tolerance) {
        fprintf(stderr, "JT9%c input %lld attempt %d: sync %.9g, reference %.9g, difference %.9g, tolerance %.9g\n",
                'A' + f->request.jt9.submode, (long long)f->request.input_id,
                f->request.attempt_no, record->sync, f->reference[2],
                record->sync - f->reference[2], sync_tolerance);
      }
      CHECK(fabsf(record->frequency_hz - f->reference[0]) < 0.001f);
      CHECK(fabsf(record->dt_seconds - f->reference[1]) < 0.001f);
      CHECK(fabsf(record->sync - f->reference[2]) < sync_tolerance);
      CHECK(record->snr_db == (int)f->reference[3]);
    }
  }
  CHECK(decoder_engine_release_input(f->engine, record->input_id) == DECODER_BUSY);
}

static void prepare(fixture *f, int submode)
{
  static int64_t next_id = 100;
  memset(&f->request, 0, sizeof f->request);
  f->request.input_id = ++next_id;
  f->request.analysis_id = next_id;
  f->request.attempt_no = 1;
  f->request.mode = DECODER_MODE_JT9;
  f->request.phase = DECODER_PHASE_NORMAL;
  f->request.source = DECODER_SOURCE_FILE;
  f->request.jt9.utc = 1200;
  f->request.jt9.receive_frequency_hz = 1000;
  f->request.jt9.search_low_hz = 900;
  f->request.jt9.search_high_hz = 1100;
  f->request.jt9.tolerance_hz = 20;
  f->request.jt9.depth = 3;
  f->request.jt9.submode = submode;
  f->audio.sample_count = SAMPLE_COUNT;
  f->audio.sample_rate_hz = 12000;
  f->compare_reference = 0;
  f->check_dt = 0;
  f->frequency_tolerance = 3.0f;
  f->expected_frequency = 1000.0f;
  decoder_engine_test_jt9_signal(submode, 12000, 0.0f, f->expected_frequency, f->samples);
}

static int decode(fixture *f, int expect_message)
{
  decoder_attempt_outcome outcome;
  memcpy(f->before, f->samples, SAMPLE_COUNT * sizeof *f->samples);
  f->count = f->found = 0;
  CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
  CHECK(decoder_engine_decode(f->engine, &f->request, &f->audio, observe, f, &outcome) == DECODER_OK);
  if (fetestexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW)) {
    fprintf(stderr, "JT9%c input %lld attempt %d: floating-point exception at %.3f Hz\n",
            'A' + f->request.jt9.submode, (long long)f->request.input_id,
            f->request.attempt_no, f->expected_frequency);
  }
  CHECK(fetestexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW) == 0);
  CHECK(outcome.status == DECODER_OK && outcome.observation_count == f->count);
  CHECK(outcome.retained_count >= f->count && outcome.evidence_dropped == 0);
  CHECK(!memcmp(f->before, f->samples, SAMPLE_COUNT * sizeof *f->samples));
  if (expect_message > 0 && !f->found) {
    fprintf(stderr, "Missing JT9%c message (input %lld, attempt %d, observations %d)\n",
            'A' + f->request.jt9.submode, (long long)f->request.input_id,
            f->request.attempt_no, f->count);
  }
  if (expect_message > 0) CHECK(f->found > 0);
  else CHECK(f->count == 0);
  return outcome.retained_count;
}

static void create_fixture(fixture *f)
{
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  f->samples = malloc(SAMPLE_COUNT * sizeof *f->samples);
  f->before = malloc(SAMPLE_COUNT * sizeof *f->before);
  CHECK(f->samples && f->before);
  f->audio.samples = f->samples;
  CHECK(decoder_engine_create(&options, &f->engine) == DECODER_OK);
}

static void check_lifecycle_and_destroy(fixture *f)
{
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  decode(f, 1);
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  decode(f, 1);
  CHECK(decoder_engine_destroy(f->engine) == DECODER_OK);
  CHECK(decoder_engine_create(&options, &f->engine) == DECODER_OK);
  prepare(f, 7);
  decode(f, 1);
  CHECK(decoder_engine_destroy(f->engine) == DECODER_OK);
  free(f->before);
  free(f->samples);
}

static void run_smoke(void)
{
  fixture f = {0};
  create_fixture(&f);
  prepare(&f, 0);
  check_lifecycle_and_destroy(&f);
}

static void run_tests(void)
{
  decoder_attempt_outcome outcome;
  fixture f = {0};
  create_fixture(&f);

  for (int submode = 0; submode <= 7; ++submode) {
    float windowed[4] = {0};
    prepare(&f, submode);
    if (submode == 0 || submode == 7) {
      decoder_engine_test_jt9_reference(submode, 1, f.samples, windowed);
      decoder_engine_test_jt9_reference(submode, 0, f.samples, f.reference);
      f.compare_reference = 1;
    }
    decode(&f, 1);
    f.compare_reference = 0;
    if (submode == 0 || submode == 7) {
      /* The engine's own spectra follow the Low sidelobes request. */
      float unwindowed[4];
      memcpy(unwindowed, f.reference, sizeof unwindowed);
      memcpy(f.reference, windowed, sizeof windowed);
      f.request.jt9.low_sidelobes = 1;
      ++f.request.analysis_id;
      ++f.request.attempt_no;
      f.compare_reference = 1;
      decode(&f, 1);
      f.compare_reference = 0;
      f.request.jt9.low_sidelobes = 0;
      memcpy(f.reference, unwindowed, sizeof unwindowed);
      f.request.phase = DECODER_PHASE_REPEAT;
      ++f.request.analysis_id;
      ++f.request.attempt_no;
      f.request.jt9.receive_frequency_hz = 1005;
      f.request.jt9.search_low_hz = 950;
      f.request.jt9.search_high_hz = 1150;
      int retained = decode(&f, 1);

      /* Decodable storage beyond the borrowed prefix must not reach the decoder. */
      f.audio.sample_count = 1;
      f.samples[0] = 0;
      ++f.request.attempt_no;
      CHECK(decode(&f, 0) == retained);
      f.audio.sample_count = SAMPLE_COUNT;
      memset(f.samples, 0, SAMPLE_COUNT * sizeof *f.samples);
      ++f.request.attempt_no;
      CHECK(decode(&f, 0) == retained);
    }
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  }

  prepare(&f, 7);
  decoder_engine_test_jt9_signal(7, 18000, 3.0f, f.expected_frequency, f.samples);
  f.check_dt = 1;
  f.expected_dt = 0.45f;
  decode(&f, 1);
  CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);

  const int spectrum_rows[] = {173, 179, 181};
  for (size_t i = 0; i < sizeof spectrum_rows / sizeof *spectrum_rows; ++i) {
    prepare(&f, 7);
    decoder_engine_test_jt9_signal(7, 18000, 3.0f, f.expected_frequency, f.samples);
    f.audio.sample_count = spectrum_rows[i] * 3456 + 2048;
    f.check_dt = 1;
    f.expected_dt = 0.45f;
    decode(&f, 1);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  }

  const int tolerances[] = {1, 2, 5};
  for (size_t i = 0; i < sizeof tolerances / sizeof *tolerances; ++i) {
    prepare(&f, 7);
    f.request.jt9.tolerance_hz = tolerances[i];
    f.frequency_tolerance = 0.5f;
    f.check_dt = 1;
    f.expected_dt = -0.05f;
    decode(&f, 1);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  }

  const float boundary_frequencies[] = {999.2f, 1000.8f, 998.8f, 1001.2f};
  for (size_t i = 0; i < sizeof boundary_frequencies / sizeof *boundary_frequencies; ++i) {
    prepare(&f, 7);
    f.expected_frequency = boundary_frequencies[i];
    decoder_engine_test_jt9_signal(7, 12000, 0.0f, f.expected_frequency, f.samples);
    f.request.jt9.tolerance_hz = 1;
    f.frequency_tolerance = 0.2f;
    f.check_dt = 1;
    f.expected_dt = -0.05f;
    decode(&f, fabsf(f.expected_frequency - f.request.jt9.receive_frequency_hz) < 1.0f);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  }

  const int excluded_frequencies[] = {0, 1010, 4999};
  for (size_t i = 0; i < sizeof excluded_frequencies / sizeof *excluded_frequencies; ++i) {
    prepare(&f, 7);
    f.request.jt9.receive_frequency_hz = excluded_frequencies[i];
    f.request.jt9.tolerance_hz = 5;
    decode(&f, 0);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  }

  prepare(&f, 0);
  f.request.jt9.depth = 1;
  memset(f.samples, 0, 16 * 12000 * sizeof *f.samples);
  decode(&f, 1);
  CHECK(f.count == 1 && f.found == 1);
  CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);

  for (int submode = 0; submode <= 7; submode += 7) {
    prepare(&f, submode);
    f.audio.sample_count = 45 * 12000;
    decode(&f, 1);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);

    prepare(&f, submode);
    memset(f.samples + 20 * 12000, 0, 1200 * sizeof *f.samples);
    decode(&f, 1);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);

    prepare(&f, submode);
    for (int i = 0; i < SAMPLE_COUNT; ++i) f.samples[i] = 2;
    decode(&f, 0);
    CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  }
  prepare(&f, 7);
  f.request.jt9.receive_frequency_hz = 3500;
  f.request.jt9.search_low_hz = 3400;
  f.request.jt9.search_high_hz = 3600;
  decode(&f, 0);
  CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);

  prepare(&f, 0);
  f.request.phase = DECODER_PHASE_EARLY;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
  f.request.phase = DECODER_PHASE_NORMAL;
  f.audio.sample_count = SAMPLE_COUNT + 1;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
  f.audio.sample_count = SAMPLE_COUNT;
  check_lifecycle_and_destroy(&f);
}

#ifdef _WIN32
static DWORD WINAPI run_on_small_stack(LPVOID unused)
#else
static void *run_on_small_stack(void *unused)
#endif
{
  (void)unused;
  run_tests();
  return 0;
}

#ifdef __linux__
static int add_tls_size(struct dl_phdr_info *info, size_t size, void *opaque)
{
  (void)size;
  size_t *stack_size = opaque;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr) *header = &info->dlpi_phdr[i];
    if (header->p_type == PT_TLS)
      *stack_size += header->p_memsz + header->p_align;
  }
  return 0;
}
#endif

extern void decoder_engine_test_jt9_signal_level(int submode, int start_sample, float spread_hz,
                                               float frequency_hz, float amplitude, int16_t *samples);

static void write_le(FILE *file, uint32_t value, unsigned bytes)
{
  for (unsigned i = 0; i < bytes; ++i) CHECK(fputc((value >> (8 * i)) & 255, file) != EOF);
}

static void write_wav(const char *path, float frequency, float amplitude)
{
  int16_t *samples = malloc(SAMPLE_COUNT * sizeof *samples);
  CHECK(samples);
  decoder_engine_test_jt9_signal_level(0, 12000, 0.0f, frequency, amplitude, samples);
  FILE *file = fopen(path, "wb");
  CHECK(file);
  CHECK(fwrite("RIFF", 1, 4, file) == 4);
  write_le(file, 36 + SAMPLE_COUNT * 2, 4);
  CHECK(fwrite("WAVEfmt ", 1, 8, file) == 8);
  write_le(file, 16, 4);
  write_le(file, 1, 2);
  write_le(file, 1, 2);
  write_le(file, 12000, 4);
  write_le(file, 24000, 4);
  write_le(file, 2, 2);
  write_le(file, 16, 2);
  CHECK(fwrite("data", 1, 4, file) == 4);
  write_le(file, SAMPLE_COUNT * 2, 4);
  for (int i = 0; i < SAMPLE_COUNT; ++i) write_le(file, (uint16_t)samples[i], 2);
  CHECK(fclose(file) == 0);
  free(samples);
}

int main(int argc, char **argv)
{
  /* FFTW threading must be initialized before creating any plans. */
  CHECK(fftwf_init_threads() != 0);
  if (argc == 5 && !strcmp(argv[1], "--write-wav")) {
    write_wav(argv[2], (float)atof(argv[3]), (float)atof(argv[4]));
  } else if (argc == 2 && !strcmp(argv[1], "--smoke")) {
    run_smoke();
  } else if (argc == 1) {
    run_tests();
  } else {
    CHECK(argc == 2 && !strcmp(argv[1], "--small-stack"));
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 1024 * 1024, run_on_small_stack, NULL,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    CHECK(thread != NULL);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    DWORD status;
    CHECK(GetExitCodeThread(thread, &status) && status == 0);
    CHECK(CloseHandle(thread));
#else
    pthread_attr_t attributes;
    pthread_t thread;
    size_t stack_size = 1024 * 1024;
#ifdef __linux__
    /* Linux puts static TLS in the stack mapping; reserve 1 MiB for calls. */
    dl_iterate_phdr(add_tls_size, &stack_size);
#endif
    CHECK(pthread_attr_init(&attributes) == 0);
    CHECK(pthread_attr_setstacksize(&attributes, stack_size) == 0);
    int error = pthread_create(&thread, &attributes, run_on_small_stack, NULL);
    if (error) fprintf(stderr, "pthread_create with %zu bytes: %s\n", stack_size, strerror(error));
    CHECK(error == 0);
    CHECK(pthread_attr_destroy(&attributes) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
#endif
  }
  return EXIT_SUCCESS;
}
