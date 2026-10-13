#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "DecoderEngine.h"
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

enum { SAMPLE_CAPACITY = 60 * 12000 };
extern void decoder_engine_test_jt4_signal(int, int, int, float, float, float, int16_t *);

typedef struct {
  decoder_engine_handle engine;
  decoder_attempt_request request;
  decoder_audio_view audio;
  int16_t *samples, *before;
  int observations, messages, method, average_count, kind;
  float sync;
} fixture;

static void observe(const decoder_observation *record, void *opaque)
{
  fixture *f = opaque;
  CHECK(record->input_id == f->request.input_id);
  CHECK(record->analysis_id == f->request.analysis_id);
  CHECK(record->attempt_no == f->request.attempt_no);
  CHECK(record->mode == DECODER_MODE_JT4);
  CHECK(record->variant == f->request.jt4.submode);
  CHECK(isfinite(record->sync) && isfinite(record->frequency_hz));
  CHECK(isfinite(record->dt_seconds) && isfinite(record->quality));
  CHECK(record->ap_type == 0);
  CHECK(record->ft8.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(record->ft4.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(record->jt4.kind >= DECODER_JT4_NO_SYNC && record->jt4.kind <= DECODER_JT4_MESSAGE);
  ++f->observations;
  f->kind = record->jt4.kind;
  f->sync = record->sync;
  if (record->jt4.kind == DECODER_JT4_MESSAGE) {
    CHECK(!strcmp(record->message, "K1ABC W9XYZ FN42"));
    CHECK(record->jt4.method == DECODER_JT4_METHOD_FANO ||
          record->jt4.method == DECODER_JT4_METHOD_DEEP_SEARCH);
    CHECK(record->jt4.average_count >= 0 && record->jt4.average_count <= 64);
    CHECK(record->jt4.width_profile >= 0 && record->jt4.width_profile <= f->request.jt4.submode);
    CHECK(record->jt4.sync_polarity == 1);
    CHECK(fabsf(record->frequency_hz - 1000) < 10);
    CHECK(fabsf(record->dt_seconds) < 0.4f);
    ++f->messages;
    f->method = record->jt4.method;
    f->average_count = record->jt4.average_count;
  } else {
    CHECK(!record->message[0]);
    CHECK(record->jt4.method == DECODER_JT4_METHOD_NONE);
  }
  CHECK(decoder_engine_release_input(f->engine, record->input_id) == DECODER_BUSY);
  CHECK(decoder_engine_clear_jt4_averages(f->engine) == DECODER_BUSY);
  CHECK(decoder_engine_set_jt4_calls(f->engine, NULL, 0) == DECODER_BUSY);
}

static void create(fixture *f)
{
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  decoder_engine_capabilities capabilities;
  f->samples = malloc(SAMPLE_CAPACITY * sizeof *f->samples);
  f->before = malloc(SAMPLE_CAPACITY * sizeof *f->before);
  CHECK(f->samples && f->before);
  f->audio.samples = f->samples;
  CHECK(decoder_engine_create(&options, &f->engine) == DECODER_OK);
  CHECK(decoder_engine_get_capabilities(f->engine, &capabilities) == DECODER_OK);
  CHECK(capabilities.supported_modes & DECODER_SUPPORT_JT4);
}

static void prepare(fixture *f, int rate, int submode, int utc)
{
  static int64_t next_id = 100;
  memset(&f->request, 0, sizeof f->request);
  f->request.input_id = ++next_id;
  f->request.analysis_id = next_id;
  f->request.attempt_no = 1;
  f->request.mode = DECODER_MODE_JT4;
  f->request.phase = DECODER_PHASE_NORMAL;
  f->request.source = DECODER_SOURCE_FILE;
  f->request.jt4.utc = utc;
  f->request.jt4.submode = submode;
  f->request.jt4.receive_frequency_hz = 1000;
  f->request.jt4.tolerance_hz = 50;
  f->request.jt4.depth = 1;
  memset(f->request.jt4.mycall, ' ', sizeof f->request.jt4.mycall);
  memset(f->request.jt4.hiscall, ' ', sizeof f->request.jt4.hiscall);
  memset(f->request.jt4.hisgrid, ' ', sizeof f->request.jt4.hisgrid);
  f->audio.sample_count = 60 * rate;
  f->audio.sample_rate_hz = rate;
  decoder_engine_test_jt4_signal(rate, submode, utc + 1, 300, 100, 1000, f->samples);
}

static void decode(fixture *f, int expected)
{
  decoder_attempt_outcome outcome;
  decoder_attempt_request before_request = f->request;
  size_t bytes = f->audio.sample_count * sizeof *f->samples;
  memcpy(f->before, f->samples, bytes);
  f->observations = f->messages = f->average_count = 0;
  f->method = DECODER_JT4_METHOD_NONE;
  f->kind = -1;
  f->sync = NAN;
  CHECK(decoder_engine_decode(f->engine, &f->request, &f->audio, observe, f, &outcome) == DECODER_OK);
  CHECK(outcome.status == DECODER_OK && outcome.observation_count == f->observations);
  CHECK(outcome.evidence_dropped == 0);
  CHECK(!memcmp(&before_request, &f->request, sizeof before_request));
  CHECK(!memcmp(f->before, f->samples, bytes));
  if (expected >= 0) {
    if (!!f->messages != !!expected)
      fprintf(stderr, "JT4%c %d Hz UTC %06d: got %d messages, expected %d\n",
              'A' + f->request.jt4.submode, f->audio.sample_rate_hz,
              f->request.jt4.utc, f->messages, expected);
    CHECK(!!f->messages == !!expected);
  }
}

static void release(fixture *f)
{
  CHECK(decoder_engine_release_input(f->engine, f->request.input_id) == DECODER_OK);
}

static void repeat(fixture *f)
{
  f->request.phase = DECODER_PHASE_REPEAT;
  ++f->request.analysis_id;
  ++f->request.attempt_no;
}

static void destroy(fixture *f)
{
  CHECK(decoder_engine_destroy(f->engine) == DECODER_OK);
  free(f->before);
  free(f->samples);
}

static void invalid(fixture *f)
{
  decoder_attempt_outcome outcome;
  f->observations = 0;
  CHECK(decoder_engine_decode(f->engine, &f->request, &f->audio, observe, f, &outcome) == DECODER_INVALID);
  CHECK(f->observations == 0);
}

static void lifecycle(fixture *f)
{
  prepare(f, 11025, 0, 120000);
  decode(f, 1);
  CHECK(f->method == DECODER_JT4_METHOD_FANO && !f->average_count);
  repeat(f);
  --f->audio.sample_count;
  invalid(f);
  ++f->audio.sample_count;
  f->audio.sample_rate_hz = 12000;
  invalid(f);
  f->audio.sample_rate_hz = 11025;
  f->request.jt4.receive_frequency_hz = 1500;
  decode(f, 0);
  repeat(f);
  f->request.jt4.receive_frequency_hz = 1000;
  decode(f, 1);
  release(f);

  prepare(f, 12000, 0, 120100);
  decode(f, 1);
  release(f);
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  prepare(f, 11025, 0, 120200);
  f->audio.sample_count = 1;
  f->samples[0] = 0;
  decode(f, 0);
  CHECK(f->observations == 1);
  release(f);
  prepare(f, 11025, 0, 120300);
  decode(f, 1);
  release(f);
  prepare(f, 11025, 0, 120400);
  memset(f->samples, 0, f->audio.sample_count * sizeof *f->samples);
  decode(f, 0);
  CHECK(f->observations == 1);
  release(f);
}

static void averages(fixture *f, int expected)
{
  decoder_jt4_average_entry entries[DECODER_JT4_AVERAGE_CAPACITY];
  int32_t count;
  CHECK(decoder_engine_get_jt4_averages(f->engine, NULL, 0, &count) == DECODER_OK);
  CHECK(count == expected);
  CHECK(decoder_engine_get_jt4_averages(f->engine, entries, DECODER_JT4_AVERAGE_CAPACITY, &count) == DECODER_OK);
  CHECK(count == expected);
  for (int i = 0; i < count; ++i) {
    CHECK(entries[i].utc == 120000 + i * 200);
    CHECK(entries[i].polarity == 1 && entries[i].used == 1);
    CHECK(abs(entries[i].frequency_hz - 1000) < 10);
    CHECK(isfinite(entries[i].sync) && isfinite(entries[i].dt_seconds));
  }
  if (count > 1) {
    CHECK(decoder_engine_get_jt4_averages(f->engine, entries, 1, &count) == DECODER_CAPACITY);
    CHECK(count == expected);
  }
}

static void weak_averaging(fixture *f)
{
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  for (int i = 0; i < 2; ++i) {
    prepare(f, 11025, 0, 120000 + i * 200);
    f->request.jt4.depth = 3;
    f->request.jt4.averaging = 1;
    decoder_engine_test_jt4_signal(11025, 0, 5 + i, 5.0f, 100, 1000, f->samples);
    decode(f, i == 1);
    averages(f, i + 1);
    if (i == 0) release(f);
  }
  CHECK(f->messages == 1 && f->method == DECODER_JT4_METHOD_FANO && f->average_count == 2);
  repeat(f);
  decode(f, 1);
  CHECK(f->average_count == 2);
  averages(f, 2);
  CHECK(decoder_engine_clear_jt4_averages(f->engine) == DECODER_OK);
  averages(f, 0);
  repeat(f);
  decode(f, 0);
  int32_t count;
  decoder_jt4_average_entry entry;
  CHECK(decoder_engine_get_jt4_averages(f->engine, &entry, 1, &count) == DECODER_OK);
  CHECK(count == 1 && entry.utc == 120200);
  release(f);
  prepare(f, 11025, 1, 120400);
  decode(f, 1);
  averages(f, 0);
  release(f);
}

static void forced_decode(fixture *f)
{
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  prepare(f, 11025, 0, 120000);
  f->request.jt4.tolerance_hz = 0;
  f->request.jt4.averaging = 1;
  decoder_engine_test_jt4_signal(11025, 0, 1, 0, 100, 1000, f->samples);
  decode(f, 0);
  CHECK(f->kind == DECODER_JT4_NO_SYNC && f->sync == 0);
  int32_t count;
  CHECK(decoder_engine_get_jt4_averages(f->engine, NULL, 0, &count) == DECODER_OK && count == 0);
  repeat(f);
  f->request.jt4.min_sync = -1;
  decode(f, 0);
  CHECK(f->kind == DECODER_JT4_SYNC && f->sync == 0);
  CHECK(decoder_engine_get_jt4_averages(f->engine, NULL, 0, &count) == DECODER_OK && count == 1);
  release(f);

  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  prepare(f, 11025, 0, 120200);
  f->request.jt4.min_sync = -1;
  f->request.jt4.averaging = 1;
  memset(f->samples, 0, f->audio.sample_count * sizeof *f->samples);
  decode(f, 0);
  CHECK(f->kind == DECODER_JT4_NO_SYNC && f->sync == 0);
  CHECK(decoder_engine_get_jt4_averages(f->engine, NULL, 0, &count) == DECODER_OK && count == 0);
  release(f);
}

static void deep_search(fixture *f)
{
  decoder_jt4_call call = {"W9XYZ       ", "FN42"};
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  prepare(f, 11025, 0, 120000);
  f->request.jt4.depth = 3;
  memcpy(f->request.jt4.mycall, "K1ABC       ", 12);
  decoder_engine_test_jt4_signal(11025, 0, 1, 5.0f, 100, 1000, f->samples);
  decode(f, 0);
  CHECK(decoder_engine_set_jt4_calls(f->engine, &call, 1) == DECODER_OK);
  memset(&call, 0, sizeof call);
  repeat(f);
  f->request.jt4.deep_search = 1;
  decode(f, 1);
  CHECK(f->method == DECODER_JT4_METHOD_DEEP_SEARCH && !f->average_count);
  memcpy(call.call, "N0CALL      ", 12);
  memcpy(call.grid, "EM00", 4);
  CHECK(decoder_engine_set_jt4_calls(f->engine, &call, 1) == DECODER_OK);
  repeat(f);
  decode(f, 0);
  CHECK(decoder_engine_set_jt4_calls(f->engine, NULL, 0) == DECODER_OK);
  memcpy(f->request.jt4.hiscall, "W9XYZ       ", 12);
  memcpy(f->request.jt4.hisgrid, "FN42  ", 6);
  repeat(f);
  decode(f, 1);
  CHECK(f->method == DECODER_JT4_METHOD_DEEP_SEARCH);
  memcpy(f->request.jt4.hiscall, "N0CALL      ", 12);
  memcpy(f->request.jt4.hisgrid, "EM00  ", 6);
  repeat(f);
  decode(f, 0);
  release(f);
  memcpy(call.call, "W9XYZ       ", 12);
  memcpy(call.grid, "FN42", 4);
  CHECK(decoder_engine_set_jt4_calls(f->engine, &call, 1) == DECODER_OK);
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  prepare(f, 11025, 0, 120200);
  f->request.jt4.depth = 3;
  f->request.jt4.deep_search = 1;
  memcpy(f->request.jt4.mycall, "K1ABC       ", 12);
  decoder_engine_test_jt4_signal(11025, 0, 1, 5.0f, 100, 1000, f->samples);
  decode(f, 0);
  release(f);
}

static void run_tests(int smoke)
{
  fixture f = {0};
  create(&f);
  for (int submode = 0; submode < (smoke ? 1 : 7); ++submode) {
    prepare(&f, submode == 5 ? 12000 : 11025, submode, 0);
    decode(&f, 1);
    CHECK(f.method == DECODER_JT4_METHOD_FANO && !f.average_count);
    release(&f);
  }
  if (!smoke) {
    decoder_jt4_call call = {"W9XYZ       ", "FN42"};
    CHECK(decoder_engine_set_jt4_calls(f.engine, &call, 1) == DECODER_OK);
    memset(&call, 0, sizeof call);
    CHECK(decoder_engine_set_jt4_calls(f.engine, NULL, 1) == DECODER_INVALID);
    CHECK(decoder_engine_set_jt4_calls(f.engine, NULL, DECODER_JT4_CALL_CAPACITY + 1) == DECODER_INVALID);
    CHECK(decoder_engine_set_jt4_calls(f.engine, NULL, 0) == DECODER_OK);
    int32_t count = -1;
    CHECK(decoder_engine_get_jt4_averages(f.engine, NULL, 0, &count) == DECODER_OK && count == 0);
    prepare(&f, 11025, 0, 0);
    f.request.phase = DECODER_PHASE_EARLY;
    invalid(&f);
    f.request.phase = DECODER_PHASE_NORMAL;
    f.request.jt4.minimum_width_profile = 1;
    invalid(&f);
    f.request.jt4.minimum_width_profile = 0;
    f.request.jt4.utc = 126000;
    invalid(&f);
    f.request.jt4.utc = 0;
    f.request.jt4.submode = 7;
    invalid(&f);
    f.request.jt4.submode = 0;
    f.audio.sample_rate_hz = 8000;
    decoder_attempt_outcome outcome;
    f.observations = 0;
    CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_UNSUPPORTED);
    CHECK(f.observations == 0);
    f.audio.sample_rate_hz = 11025;
    ++f.audio.sample_count;
    invalid(&f);
    --f.audio.sample_count;
    f.request.jt4.averaging = 2;
    invalid(&f);
  }
  lifecycle(&f);
  if (!smoke) {
    weak_averaging(&f);
    forced_decode(&f);
    deep_search(&f);
  }
  CHECK(decoder_engine_destroy(f.engine) == DECODER_OK);
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  CHECK(decoder_engine_create(&options, &f.engine) == DECODER_OK);
  prepare(&f, 11025, 0, 130000);
  decode(&f, 1);
  destroy(&f);
}

static void write_le(FILE *file, uint32_t value, unsigned bytes)
{
  for (unsigned i = 0; i < bytes; ++i) CHECK(fputc((value >> (8 * i)) & 255, file) != EOF);
}

static void write_wav(const char *path)
{
  int16_t *samples = malloc(SAMPLE_CAPACITY * sizeof *samples);
  CHECK(samples);
  decoder_engine_test_jt4_signal(12000, 0, 1, 300, 100, 1000, samples);
  FILE *file = fopen(path, "wb");
  CHECK(file);
  CHECK(fwrite("RIFF", 1, 4, file) == 4);
  write_le(file, 36 + SAMPLE_CAPACITY * 2, 4);
  CHECK(fwrite("WAVEfmt ", 1, 8, file) == 8);
  write_le(file, 16, 4);
  write_le(file, 1, 2);
  write_le(file, 1, 2);
  write_le(file, 12000, 4);
  write_le(file, 24000, 4);
  write_le(file, 2, 2);
  write_le(file, 16, 2);
  CHECK(fwrite("data", 1, 4, file) == 4);
  write_le(file, SAMPLE_CAPACITY * 2, 4);
  for (int i = 0; i < SAMPLE_CAPACITY; ++i) write_le(file, (uint16_t)samples[i], 2);
  CHECK(fclose(file) == 0);
  free(samples);
}

#ifdef _WIN32
static DWORD WINAPI run_on_small_stack(LPVOID unused)
#else
static void *run_on_small_stack(void *unused)
#endif
{
  (void)unused;
  run_tests(0);
  return 0;
}

#ifdef __linux__
static int add_tls_size(struct dl_phdr_info *info, size_t size, void *opaque)
{
  (void)size;
  size_t *stack_size = opaque;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr) *header = &info->dlpi_phdr[i];
    if (header->p_type == PT_TLS) *stack_size += header->p_memsz + header->p_align;
  }
  return 0;
}
#endif

int main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "--write-wav")) write_wav(argv[2]);
  else if (argc == 2 && !strcmp(argv[1], "--smoke")) run_tests(1);
  else if (argc == 1) run_tests(0);
  else {
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
