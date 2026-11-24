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

enum { SAMPLE_COUNT = 720000 };
extern void decoder_engine_test_jt65_signal(int submode, int kind, int seed, int16_t *samples);

typedef struct {
  decoder_engine_handle engine;
  decoder_attempt_request request;
  decoder_audio_view audio;
  int16_t *samples, *before;
  int count, messages, sync_only, max_average, polarity, method, ap_type;
  int expected_kind, seen;
  decoder_observation last_message;
} fixture;

static void observe(const decoder_observation *record, void *opaque)
{
  fixture *f = opaque;
  CHECK(record->input_id == f->request.input_id);
  CHECK(record->analysis_id == f->request.analysis_id);
  CHECK(record->attempt_no == f->request.attempt_no);
  CHECK(record->mode == DECODER_MODE_JT65);
  CHECK(record->variant == f->request.jt65.submode);
  CHECK(isfinite(record->sync) && isfinite(record->dt_seconds));
  CHECK(record->ft8.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(record->ft4.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(record->superfox.kind == 0);
  CHECK(record->jt65.has_width == f->request.jt65.vhf);
  CHECK(isfinite(record->jt65.width_hz));
  CHECK(record->jt65.has_drift == (record->jt65.sync_polarity != 0));
  CHECK(isfinite(record->jt65.drift_hz));
  if (!record->jt65.has_drift) CHECK(record->jt65.drift_hz == 0.0f);
  if (!record->jt65.has_width) CHECK(record->jt65.width_hz == 0.0f);
  ++f->count;
  if (record->jt65.kind == DECODER_JT65_MESSAGE) {
    const char *expected = f->expected_kind == 1 ? "K1ABC W9XYZ FN42 OOO" : "K1ABC W9XYZ FN42";
    CHECK(record->jt65.ooo == (f->expected_kind == 1));
    int bit = 1;
    float frequency = 1500.0f;
    if (f->expected_kind >= 3 && f->expected_kind <= 5) {
      const char *shorthand[] = {"RO", "RRR", "73"};
      expected = shorthand[f->expected_kind - 3];
      CHECK(record->jt65.method == DECODER_JT65_METHOD_NONE);
      CHECK(!record->jt65.has_drift);
    } else {
      CHECK(record->jt65.method > 0);
      if (f->expected_kind == 6 && !strcmp(record->message, "K2DEF N0XYZ EM00")) {
        expected = "K2DEF N0XYZ EM00";
        frequency = 2100.0f;
        bit = 2;
      }
    }
    if (strcmp(record->message, expected))
      fprintf(stderr, "input %lld attempt %d samples %d kind %d frequency %.1f message [%s] expected [%s]\n",
              (long long)f->request.input_id, f->request.attempt_no, f->audio.sample_count,
              f->expected_kind, record->frequency_hz, record->message, expected);
    CHECK(!strcmp(record->message, expected));
    CHECK(!(f->seen & bit));
    f->seen |= bit;
    f->last_message = *record;
    CHECK(fabsf(record->frequency_hz - frequency) <= 4.0f);
    if (f->expected_kind < 3 || f->expected_kind > 5)
      CHECK(fabsf(record->dt_seconds) <= 1.0f);
    ++f->messages;
    if (record->jt65.average_count > f->max_average) f->max_average = record->jt65.average_count;
    f->polarity = record->jt65.sync_polarity;
    f->method = record->jt65.method;
    f->ap_type = record->ap_type;
  } else {
    CHECK(record->jt65.kind == DECODER_JT65_SYNC);
    CHECK(record->message[0] == '\0');
    CHECK(record->jt65.method == 0);
    ++f->sync_only;
  }
  CHECK(decoder_engine_release_input(f->engine, record->input_id) == DECODER_BUSY);
  CHECK(decoder_engine_clear_jt65_averages(f->engine) == DECODER_BUSY);
  CHECK(decoder_engine_set_jt65_calls(f->engine, NULL, 0) == DECODER_BUSY);
}

static void initialize_request(fixture *f, int submode, int kind, int utc)
{
  static int64_t next_id = 100;
  memset(&f->request, 0, sizeof f->request);
  f->request.input_id = ++next_id;
  f->request.analysis_id = next_id;
  f->request.attempt_no = 1;
  f->request.mode = DECODER_MODE_JT65;
  f->request.phase = DECODER_PHASE_NORMAL;
  f->request.source = DECODER_SOURCE_FILE;
  f->request.jt65.utc = utc;
  f->request.jt65.qso_progress = 5;
  f->request.jt65.receive_frequency_hz = 1500;
  f->request.jt65.search_low_hz = 1300;
  f->request.jt65.search_high_hz = 1700;
  f->request.jt65.tolerance_hz = 100;
  f->request.jt65.depth = 1;
  f->request.jt65.submode = submode;
  f->request.jt65.passes = 1;
  f->request.jt65.trials = 1000;
  f->request.jt65.aggressiveness = 5;
  f->request.jt65.single_decode = 1;
  memcpy(f->request.jt65.mycall, "K1ABC       ", 12);
  memcpy(f->request.jt65.hiscall, "W9XYZ       ", 12);
  memcpy(f->request.jt65.hisgrid, "FN42  ", 6);
  f->expected_kind = kind;
  f->audio.sample_count = SAMPLE_COUNT;
  f->audio.sample_rate_hz = 12000;
}

static void prepare(fixture *f, int submode, int kind, int utc)
{
  initialize_request(f, submode, kind, utc);
  decoder_engine_test_jt65_signal(submode, kind, utc, f->samples);
}

static void decode(fixture *f, int expect_message)
{
  decoder_attempt_outcome outcome;
  memcpy(f->before, f->samples, SAMPLE_COUNT * sizeof *f->samples);
  f->count = f->messages = f->sync_only = f->max_average = f->polarity = f->method = 0;
  f->ap_type = f->seen = 0;
  CHECK(decoder_engine_decode(f->engine, &f->request, &f->audio, observe, f, &outcome) == DECODER_OK);
  CHECK(outcome.status == DECODER_OK && outcome.observation_count == f->count);
  CHECK(outcome.evidence_dropped == 0);
  CHECK(!memcmp(f->before, f->samples, SAMPLE_COUNT * sizeof *f->samples));
  if (!!f->messages != !!expect_message) {
    fprintf(stderr, "JT65%c input %lld attempt %d: %d messages, %d sync observations, expected message %d\n",
            'A' + f->request.jt65.submode, (long long)f->request.input_id,
            f->request.attempt_no, f->messages, f->sync_only, expect_message);
  }
  CHECK(!!f->messages == !!expect_message);
}

static int average_count(fixture *f)
{
  decoder_jt65_average_entry entries[64];
  int32_t count, queried;
  CHECK(decoder_engine_get_jt65_averages(f->engine, NULL, 0, &queried) == DECODER_OK);
  CHECK(decoder_engine_get_jt65_averages(f->engine, entries, 64, &count) == DECODER_OK);
  CHECK(count == queried && count >= 0 && count <= 64);
  for (int i = 0; i < count; ++i) {
    CHECK(isfinite(entries[i].sync) && isfinite(entries[i].dt_seconds));
    CHECK(abs(entries[i].frequency_hz - 1500) <= 5);
  }
  if (count > 1) {
    CHECK(decoder_engine_get_jt65_averages(f->engine, entries, 1, &queried) == DECODER_CAPACITY);
    CHECK(queried == count);
  }
  return count;
}

static void release(fixture *f)
{
  CHECK(decoder_engine_release_input(f->engine, f->request.input_id) == DECODER_OK);
}

static void weak_reception(fixture *f, int utc)
{
  prepare(f, 0, 2, utc);
  f->request.jt65.vhf = 1;
  f->request.jt65.averaging = 1;
}

static void create_fixture(fixture *f)
{
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  decoder_engine_capabilities capabilities;
  f->samples = malloc(SAMPLE_COUNT * sizeof *f->samples);
  f->before = malloc(SAMPLE_COUNT * sizeof *f->before);
  CHECK(f->samples && f->before);
  f->audio.samples = f->samples;
  CHECK(decoder_engine_create(&options, &f->engine) == DECODER_OK);
  CHECK(decoder_engine_get_capabilities(f->engine, &capabilities) == DECODER_OK);
  CHECK(capabilities.supported_modes & DECODER_SUPPORT_JT65);
}

static void check_lifecycle_and_destroy(fixture *f)
{
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_OK);
  CHECK(average_count(f) == 0);
  prepare(f, 0, 0, 9);
  decode(f, 1);
  CHECK(decoder_engine_destroy(f->engine) == DECODER_OK);
  CHECK(decoder_engine_create(&options, &f->engine) == DECODER_OK);
  prepare(f, 0, 0, 11);
  decode(f, 1);
  CHECK(decoder_engine_destroy(f->engine) == DECODER_OK);
  free(f->before);
  free(f->samples);
}

static void run_smoke(void)
{
  fixture f = {0};
  create_fixture(&f);
  prepare(&f, 0, 0, 1);
  decode(&f, 1);
  release(&f);
  check_lifecycle_and_destroy(&f);
}

static void run_tests(void)
{
  decoder_attempt_outcome outcome;
  fixture f = {0};
  create_fixture(&f);

  prepare(&f, 0, 0, 1);
  f.request.jt65.single_decode = 0;
  f.request.jt65.aggressiveness = 0;
  f.request.jt65.ap_enabled = 1;
  f.request.jt65.qso_progress = 0;
  memset(f.request.jt65.mycall, ' ', sizeof f.request.jt65.mycall);
  memset(f.request.jt65.hiscall, ' ', sizeof f.request.jt65.hiscall);
  memset(f.request.jt65.hisgrid, ' ', sizeof f.request.jt65.hisgrid);
  decode(&f, 1);
  release(&f);

  for (int gap = 0; gap < 3; ++gap) {
    prepare(&f, 0, 0, gap + 1);
    f.request.jt65.single_decode = 0;
    f.request.jt65.search_low_hz = 200;
    f.request.jt65.search_high_hz = 2600;
    f.request.jt65.tolerance_hz = 1000;
    f.request.jt65.ap_enabled = 1;
    f.request.jt65.qso_progress = 0;
    if (gap == 0) f.audio.sample_count = 40 * 12000;
    if (gap == 1) memset(f.samples + 40 * 12000, 0, (SAMPLE_COUNT - 40 * 12000) * sizeof *f.samples);
    if (gap == 2) memset(f.samples + 20 * 12000, 0, 5 * 12000 * sizeof *f.samples);
    decode(&f, 0);
    CHECK(f.count == 0);
    release(&f);
  }

  for (int submode = 0; submode <= 2; ++submode) {
    prepare(&f, submode, 0, 1);
    decode(&f, 1);
    if (submode == 0) {
      f.request.phase = DECODER_PHASE_REPEAT;
      ++f.request.analysis_id;
      ++f.request.attempt_no;
      decode(&f, 1);
      /* The unused caller storage remains decodable; only its prefix is borrowed. */
      f.audio.sample_count = 1;
      f.samples[0] = 0;
      ++f.request.attempt_no;
      decode(&f, 0);
      CHECK(f.count == 0);
      f.samples[0] = 1000;
      ++f.request.attempt_no;
      decode(&f, 0);
      CHECK(f.count == 0);
      f.audio.sample_count = 9600;
      ++f.request.attempt_no;
      decode(&f, 0);
      CHECK(f.count == 0);
    }
    release(&f);
  }

  prepare(&f, 0, 0, 1);
  f.request.jt65.vhf = 1;
  f.request.jt65.trials = 100;
  f.request.jt65.receive_frequency_hz = 2300;
  f.request.jt65.search_low_hz = 2200;
  f.request.jt65.search_high_hz = 2400;
  decode(&f, 0);
  f.request.phase = DECODER_PHASE_REPEAT;
  ++f.request.analysis_id;
  ++f.request.attempt_no;
  f.request.jt65.receive_frequency_hz = 1500;
  f.request.jt65.search_low_hz = 1300;
  f.request.jt65.search_high_hz = 1700;
  decode(&f, 1);
  decoder_observation retained = f.last_message;
  release(&f);
  CHECK(decoder_engine_reset_session(f.engine) == DECODER_OK);
  prepare(&f, 0, 0, 1);
  f.request.jt65.vhf = 1;
  f.request.jt65.trials = 100;
  decode(&f, 1);
  CHECK(!strcmp(retained.message, f.last_message.message));
  CHECK(fabsf(retained.frequency_hz - f.last_message.frequency_hz) <= 0.5f);
  CHECK(fabsf(retained.dt_seconds - f.last_message.dt_seconds) <= 0.05f);
  CHECK(retained.jt65.method == f.last_message.jt65.method);
  release(&f);

  for (int kind = 3; kind <= 5; ++kind) {
    prepare(&f, 0, kind, 1);
    f.request.jt65.vhf = 1;
    decode(&f, 1);
    CHECK(f.messages == 1);
    release(&f);
  }

  prepare(&f, 0, 6, 1);
  f.request.jt65.aggressiveness = 0;
  f.request.jt65.vhf = 1;
  f.request.jt65.tolerance_hz = 1000;
  f.request.jt65.passes = 2;
  f.request.jt65.trials = 100;
  f.request.jt65.single_decode = 0;
  f.request.jt65.search_high_hz = 2200;
  decode(&f, 1);
  CHECK(f.messages == 2 && f.seen == 3);
  /* VHF repeats search at the selected receive frequency. */
  f.request.phase = DECODER_PHASE_REPEAT;
  ++f.request.analysis_id;
  ++f.request.attempt_no;
  decode(&f, 1);
  CHECK(f.messages == 1 && f.seen == 1);
  release(&f);
  prepare(&f, 0, 6, 3);
  f.request.jt65.aggressiveness = 0;
  f.request.jt65.vhf = 1;
  f.request.jt65.tolerance_hz = 1000;
  f.request.jt65.passes = 2;
  f.request.jt65.trials = 100;
  f.request.jt65.single_decode = 0;
  f.request.jt65.search_high_hz = 2200;
  decode(&f, 1);
  CHECK(f.messages == 2 && f.seen == 3);
  release(&f);

  prepare(&f, 0, 1, 1);
  f.request.jt65.vhf = 1;
  decode(&f, 1);
  CHECK(f.polarity < 0);
  release(&f);
  prepare(&f, 0, 0, 1);
  decode(&f, 1);
  release(&f);

  prepare(&f, 0, 0, 1);
  f.audio.sample_count = 45 * 12000;
  decode(&f, 0);
  CHECK(f.count == 0);
  release(&f);
  prepare(&f, 0, 0, 1);
  memset(f.samples + 20 * 12000, 0, 1200 * sizeof *f.samples);
  decode(&f, 0);
  CHECK(f.count == 0);
  release(&f);

  CHECK(decoder_engine_clear_jt65_averages(f.engine) == DECODER_OK);
  weak_reception(&f, 1);
  int64_t reused_input_id = f.request.input_id;
  decode(&f, 0);
  CHECK(f.sync_only > 0 && average_count(&f) == 1);
  f.request.phase = DECODER_PHASE_REPEAT;
  ++f.request.analysis_id;
  ++f.request.attempt_no;
  decode(&f, 0);
  CHECK(average_count(&f) == 1);
  CHECK(decoder_engine_clear_jt65_averages(f.engine) == DECODER_OK);
  CHECK(average_count(&f) == 0);
  ++f.request.attempt_no;
  decode(&f, 0);
  CHECK(average_count(&f) == 1);
  release(&f);
  CHECK(average_count(&f) == 1);
  weak_reception(&f, 2);
  decode(&f, 0);
  CHECK(average_count(&f) == 2);
  release(&f);
  weak_reception(&f, 3);
  f.request.input_id = reused_input_id;
  decode(&f, 1);
  CHECK(f.max_average == 2 && average_count(&f) == 3);
  decoder_jt65_average_entry selected[64];
  int32_t selected_count;
  CHECK(decoder_engine_get_jt65_averages(f.engine, selected, 64, &selected_count) == DECODER_OK);
  CHECK(selected_count == 3);
  CHECK(selected[0].utc == 1 && selected[0].used);
  CHECK(selected[1].utc == 2 && !selected[1].used);
  CHECK(selected[2].utc == 3 && selected[2].used);
  f.request.phase = DECODER_PHASE_REPEAT;
  ++f.request.analysis_id;
  ++f.request.attempt_no;
  decode(&f, 0);
  CHECK(average_count(&f) == 3);
  release(&f);

  prepare(&f, 1, 0, 5);
  f.request.jt65.vhf = 1;
  decode(&f, 1);
  CHECK(average_count(&f) == 0);
  release(&f);
  weak_reception(&f, 1);
  decode(&f, 0);
  CHECK(average_count(&f) == 1);
  release(&f);
  prepare(&f, 0, 0, 5);
  decode(&f, 1);
  CHECK(average_count(&f) == 0);
  release(&f);

  weak_reception(&f, 1);
  f.request.jt65.auto_clear = 1;
  decode(&f, 0);
  release(&f);
  weak_reception(&f, 3);
  f.request.jt65.auto_clear = 1;
  decode(&f, 1);
  CHECK(f.max_average >= 2);
  release(&f);
  weak_reception(&f, 5);
  f.request.jt65.auto_clear = 1;
  decode(&f, 0);
  CHECK(average_count(&f) == 1);
  release(&f);

  decoder_jt65_call calls[1];
  memcpy(calls[0].call, "W9XYZ       ", 12);
  memcpy(calls[0].grid, "FN42", 4);
  CHECK(decoder_engine_set_jt65_calls(f.engine, calls, 1) == DECODER_OK);
  memset(calls, 0, sizeof calls);
  CHECK(decoder_engine_set_jt65_calls(f.engine, NULL, 1) == DECODER_INVALID);
  weak_reception(&f, 1);
  f.request.jt65.averaging = 0;
  f.request.jt65.deep_search = 1;
  memset(f.request.jt65.hiscall, ' ', sizeof f.request.jt65.hiscall);
  memset(f.request.jt65.hisgrid, ' ', sizeof f.request.jt65.hisgrid);
  decode(&f, 1);
  CHECK(f.method == 2);
  memcpy(calls[0].call, "N0CALL      ", 12);
  memcpy(calls[0].grid, "EM00", 4);
  CHECK(decoder_engine_set_jt65_calls(f.engine, calls, 1) == DECODER_OK);
  f.request.phase = DECODER_PHASE_REPEAT;
  ++f.request.analysis_id;
  ++f.request.attempt_no;
  decode(&f, 0);
  CHECK(decoder_engine_set_jt65_calls(f.engine, NULL, 0) == DECODER_OK);
  release(&f);

  weak_reception(&f, 3);
  f.request.jt65.averaging = 0;
  f.request.jt65.ap_enabled = 1;
  f.request.jt65.qso_progress = 1;
  decode(&f, 1);
  CHECK(f.method == 1 && f.ap_type > 0);
  release(&f);

  initialize_request(&f, 0, 0, 7);
  memset(f.samples, 0, SAMPLE_COUNT * sizeof *f.samples);
  f.request.phase = DECODER_PHASE_EARLY;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
  f.request.phase = DECODER_PHASE_NORMAL;
  f.audio.sample_count = SAMPLE_COUNT + 1;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
  f.audio.sample_count = SAMPLE_COUNT;
  const int32_t trial_limits[] = {-1, 0, 1000000, 1000001, INT32_MAX};
  for (unsigned i = 0; i < sizeof trial_limits / sizeof *trial_limits; ++i) {
    f.request.jt65.trials = trial_limits[i];
    if (trial_limits[i] >= 0 && trial_limits[i] <= 1000000) {
      decode(&f, 0);
      CHECK(f.count == 0);
      release(&f);
    } else {
      CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
      CHECK(outcome.status == DECODER_INVALID && outcome.observation_count == 0);
    }
  }
  check_lifecycle_and_destroy(&f);
}

static void write_le(FILE *file, uint32_t value, unsigned bytes)
{
  for (unsigned i = 0; i < bytes; ++i) CHECK(fputc((value >> (8 * i)) & 255, file) != EOF);
}

static void write_wav(const char *path)
{
  int16_t *samples = malloc(SAMPLE_COUNT * sizeof *samples);
  CHECK(samples);
  decoder_engine_test_jt65_signal(0, 0, 1, samples);
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
    if (header->p_type == PT_TLS) *stack_size += header->p_memsz + header->p_align;
  }
  return 0;
}
#endif

int main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "--write-wav")) {
    write_wav(argv[2]);
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
