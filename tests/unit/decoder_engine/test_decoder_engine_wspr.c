#include "DecoderEngine.h"
#include "lib/wsprd/wsprsim_utils.h"
#include "lib/wsprd/wsprd_utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

static int sample_count(int variant)
{
  return (variant == 15 ? 912 : 114) * 12000;
}

static int16_t *make_signal(const char *text, int variant, double frequency, double start, double drift)
{
  const int count = sample_count(variant);
  const int scale = variant == 15 ? 8 : 1;
  int16_t *samples = malloc((size_t)count * sizeof *samples);
  char *calls = calloc(WSPRD_HASH_COUNT, WSPRD_CALLSIGN_SIZE);
  char *grids = calloc(WSPRD_HASH_COUNT, WSPRD_GRID4_SIZE);
  unsigned char tones[162];
  char message[WSPRD_MESSAGE_SIZE];
  CHECK(samples && calls && grids);
  uint32_t random = 1234567;
  for (int i = 0; i < count; ++i) {
    random = 1664525U * random + 1013904223U;
    samples[i] = (int16_t)((int)(random >> 24) - 128);
  }
  if (strcmp(text, "noise")) {
    CHECK(strlen(text) < sizeof message);
    strcpy(message, text);
    CHECK(get_wspr_channel_symbols(message, calls, grids, tones));
    double phase = 0.0;
    int index = (int)lround(start * 12000.0);
    for (int symbol = 0; symbol < 162; ++symbol) {
      const double step = 8.0 * atan(1.0) *
        (frequency + drift * 0.5 * (symbol - 81.0) / 81.0
         + (tones[symbol] - 1.5) * 12000.0 / (8192 * scale)) / 12000.0;
      for (int j = 0; j < 8192 * scale; ++j, ++index) {
        if (index >= 0 && index < count)
          samples[index] += (int16_t)lround(1800.0 * sin(phase));
        phase += step;
      }
    }
  }
  free(grids);
  free(calls);
  return samples;
}

static void write_le(FILE *file, uint32_t value, int bytes)
{
  for (int i = 0; i < bytes; ++i) CHECK(fputc((value >> (8 * i)) & 255, file) != EOF);
}

static void write_wav(const char *path, const int16_t *samples, int count)
{
  FILE *file = fopen(path, "wb");
  CHECK(file);
  CHECK(fwrite("RIFF", 1, 4, file) == 4);
  write_le(file, 36 + count * 2, 4);
  CHECK(fwrite("WAVEfmt ", 1, 8, file) == 8);
  write_le(file, 16, 4);
  write_le(file, 1, 2);
  write_le(file, 1, 2);
  write_le(file, 12000, 4);
  write_le(file, 24000, 4);
  write_le(file, 2, 2);
  write_le(file, 16, 2);
  CHECK(fwrite("data", 1, 4, file) == 4);
  write_le(file, count * 2, 4);
  for (int i = 0; i < count; ++i) write_le(file, (uint16_t)samples[i], 2);
  CHECK(fclose(file) == 0);
}

static void write_c2(const char *path, const char *text, int variant, double frequency, double start, double drift)
{
  char *calls = calloc(WSPRD_HASH_COUNT, WSPRD_CALLSIGN_SIZE);
  char *grids = calloc(WSPRD_HASH_COUNT, WSPRD_GRID4_SIZE);
  float *iq = calloc(90000, sizeof *iq);
  unsigned char tones[162];
  char message[WSPRD_MESSAGE_SIZE];
  CHECK(calls && grids && iq && strlen(text) < sizeof message);
  strcpy(message, text);
  CHECK(get_wspr_channel_symbols(message, calls, grids, tones));
  const double scale = variant == 15 ? 8 : 1;
  const double center = variant == 15 ? 1612.5 : 1500;
  double phase = 0;
  int index = (int)lround(start * 375 / scale);
  uint32_t random = 7654321;
  for (int i = 0; i < 90000; ++i) {
    random = 1664525U * random + 1013904223U;
    iq[i] = ((int)(random >> 24) - 128) * 0.0001f;
  }
  for (int symbol = 0; symbol < 162; ++symbol) {
    const double step = 8 * atan(1.0) *
      ((frequency - center + drift * 0.5 * (symbol - 81.0) / 81.0) * scale
       + (tones[symbol] - 1.5) * 375 / 256) / 375;
    for (int j = 0; j < 256; ++j, ++index) {
      if (index >= 0 && index < 45000) {
        iq[2 * index] += (float)cos(phase);
        iq[2 * index + 1] -= (float)sin(phase);
      }
      phase += step;
    }
  }
  FILE *file = fopen(path, "wb");
  char name[14] = "000000_0000.c2";
  const double dial = 0;
  CHECK(file);
  CHECK(fwrite(name, 1, sizeof name, file) == sizeof name);
  CHECK(fwrite(&variant, sizeof variant, 1, file) == 1);
  CHECK(fwrite(&dial, sizeof dial, 1, file) == 1);
  CHECK(fwrite(iq, sizeof *iq, 90000, file) == 90000);
  CHECK(fclose(file) == 0);
  free(iq);
  free(grids);
  free(calls);
}

static int16_t *read_wav(const char *path, int variant, int *count)
{
  FILE *file = fopen(path, "rb");
  CHECK(file);
  CHECK(fseek(file, 44, SEEK_SET) == 0);
  int16_t *samples = calloc((size_t)sample_count(variant), sizeof *samples);
  CHECK(samples);
  *count = (int)fread(samples, sizeof *samples, (size_t)sample_count(variant), file);
  CHECK(*count > 0);
  CHECK(fclose(file) == 0);
  return samples;
}

typedef struct {
  decoder_engine_handle engine;
  decoder_attempt_request request;
  decoder_audio_view audio;
  decoder_observation records[50];
  int count;
} fixture;

static decoder_attempt_request request_for(int64_t id, int variant)
{
  decoder_attempt_request request = {0};
  request.input_id = id;
  request.analysis_id = id + 1000;
  request.attempt_no = 1;
  request.mode = DECODER_MODE_WSPR;
  request.phase = DECODER_PHASE_NORMAL;
  request.source = DECODER_SOURCE_FILE;
  request.wspr.waveform = variant;
  request.wspr.effort = DECODER_WSPR_DEFAULT;
  const float center = variant == 15 ? 1612.5f : 1500.0f;
  const float width = variant == 15 ? 110.0f / 8 : 110.0f;
  request.wspr.search_low_hz = center - width;
  request.wspr.search_high_hz = center + width;
  return request;
}

static void observe(const decoder_observation *record, void *opaque)
{
  fixture *f = opaque;
  CHECK(f->count < 50);
  CHECK(record->input_id == f->request.input_id);
  CHECK(record->analysis_id == f->request.analysis_id);
  CHECK(record->attempt_no == f->request.attempt_no);
  CHECK(record->mode == DECODER_MODE_WSPR);
  CHECK(record->variant == f->request.wspr.waveform);
  CHECK(isfinite(record->frequency_hz) && isfinite(record->dt_seconds));
  CHECK(isfinite(record->wspr.snr_db) && isfinite(record->wspr.drift_hz));
  CHECK(isfinite(record->wspr.waveform_start_seconds));
  CHECK(fabsf(record->dt_seconds - (record->wspr.waveform_start_seconds - 1)) < 1e-5f);
  if (f->count) CHECK(f->records[f->count - 1].frequency_hz <= record->frequency_hz);
  f->records[f->count++] = *record;
  CHECK(decoder_engine_release_input(f->engine, record->input_id) == DECODER_BUSY);
  CHECK(decoder_engine_reset_session(f->engine) == DECODER_BUSY);
  CHECK(decoder_engine_set_wspr_calls(f->engine, NULL, 0) == DECODER_BUSY);
  CHECK(decoder_engine_destroy(f->engine) == DECODER_BUSY);
}

static void decode(fixture *f)
{
  decoder_attempt_outcome outcome;
  const size_t bytes = (size_t)f->audio.sample_count * sizeof(int16_t);
  int16_t *before = malloc(bytes);
  CHECK(before);
  memcpy(before, f->audio.samples, bytes);
  f->count = 0;
  CHECK(decoder_engine_decode(f->engine, &f->request, &f->audio, observe, f, &outcome) == DECODER_OK);
  CHECK(outcome.status == DECODER_OK && outcome.observation_count == f->count);
  CHECK(outcome.retained_count >= f->count && outcome.evidence_dropped == 0);
  CHECK(!memcmp(before, f->audio.samples, bytes));
  free(before);
}

static const decoder_observation *find(const fixture *f, const char *message)
{
  for (int i = 0; i < f->count; ++i)
    if (!strcmp(f->records[i].message, message)) return &f->records[i];
  fprintf(stderr, "Missing WSPR message '%s'; received %d observations\n", message, f->count);
  for (int i = 0; i < f->count; ++i) fprintf(stderr, "  %s\n", f->records[i].message);
  exit(EXIT_FAILURE);
}

static void check_text(const char *actual, int capacity, const char *expected)
{
  CHECK((int)strlen(expected) <= capacity);
  CHECK(!memcmp(actual, expected, strlen(expected)));
  for (int i = (int)strlen(expected); i < capacity; ++i) CHECK(actual[i] == ' ');
}

static void next_input(fixture *f, const char *message, int variant, double frequency, double start, double drift)
{
  CHECK(decoder_engine_release_input(f->engine, f->request.input_id) == DECODER_OK);
  free((void *)f->audio.samples);
  f->request = request_for(f->request.input_id + 1, variant);
  f->audio = (decoder_audio_view){make_signal(message, variant, frequency, start, drift), sample_count(variant), 12000};
}

static void check_lifecycle(void)
{
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  fixture f = {0};
  f.request = request_for(1, DECODER_WSPR_2);
  f.audio = (decoder_audio_view){make_signal("K1ABC FN42 33", 2, 1520, 1.2, 0), sample_count(2), 12000};
  CHECK(decoder_engine_create(&options, &f.engine) == DECODER_OK);
  decode(&f);
  decoder_observation standard = *find(&f, "K1ABC FN42 33");
  CHECK(standard.wspr.message_type == DECODER_WSPR_STANDARD);
  CHECK(standard.wspr.has_call && standard.wspr.has_locator && standard.wspr.has_power);
  CHECK(!standard.wspr.has_hash15 && !standard.wspr.hash_resolved);
  CHECK(standard.wspr.power_dbm == 33);
  check_text(standard.wspr.call, 12, "K1ABC");
  check_text(standard.wspr.locator, 6, "FN42");
  CHECK(fabsf(standard.frequency_hz - 1520) < 0.15f);
  CHECK(fabsf(standard.wspr.waveform_start_seconds - 1.2f) < 0.1f);
  CHECK(fabsf(standard.wspr.drift_hz) < 0.6f);

  int32_t count;
  CHECK(decoder_engine_get_wspr_calls(f.engine, NULL, 0, &count) == DECODER_OK && count == 1);
  decoder_wspr_call calls[2];
  CHECK(decoder_engine_get_wspr_calls(f.engine, calls, 2, &count) == DECODER_OK && count == 1);
  check_text(calls[0].call, 12, "K1ABC");
  check_text(calls[0].grid, 4, "FN42");

  /* A fresh residual must make repeat decoding independent of prior subtraction. */
  f.request.phase = DECODER_PHASE_REPEAT;
  ++f.request.attempt_no;
  ++f.request.analysis_id;
  decode(&f);
  const decoder_observation *repeat = find(&f, "K1ABC FN42 33");
  CHECK(repeat->frequency_hz == standard.frequency_hz);
  CHECK(repeat->dt_seconds == standard.dt_seconds);
  CHECK(repeat->wspr.snr_db == standard.wspr.snr_db);
  for (int effort = DECODER_WSPR_FAST; effort <= DECODER_WSPR_DEEP; ++effort) {
    ++f.request.attempt_no;
    f.request.wspr.effort = effort;
    decode(&f);
    find(&f, "K1ABC FN42 33");
  }

  ++f.request.attempt_no;
  f.request.wspr.calibration_error_hz = 3.25f;
  decode(&f);
  CHECK(fabsf(find(&f, "K1ABC FN42 33")->frequency_hz - (standard.frequency_hz - 3.25f)) < 0.01f);
  ++f.request.attempt_no;
  f.request.wspr.calibration_error_hz = -2.5f;
  decode(&f);
  CHECK(fabsf(find(&f, "K1ABC FN42 33")->frequency_hz - (standard.frequency_hz + 2.5f)) < 0.01f);
  ++f.request.attempt_no;
  f.request.wspr.calibration_error_hz = 0;
  f.request.wspr.search_low_hz = 1450;
  f.request.wspr.search_high_hz = 1460;
  decode(&f);
  CHECK(f.count == 0);

  decoder_attempt_outcome outcome;
  ++f.request.attempt_no;
  --f.audio.sample_count;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
  ++f.audio.sample_count;
  f.request.wspr.waveform = 15;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, observe, &f, &outcome) == DECODER_INVALID);
  f.request.wspr.waveform = 2;

  next_input(&f, "<K1ABC> FN42AA 33", 2, 1520, 1.2, 0);
  decode(&f);
  const decoder_observation *hashed = find(&f, "<K1ABC> FN42AA 33");
  CHECK(hashed->wspr.message_type == DECODER_WSPR_HASHED);
  CHECK(hashed->wspr.has_hash15 && hashed->wspr.hash_resolved && hashed->wspr.has_call);
  CHECK(hashed->wspr.hash15 == (int)nhash("K1ABC", 5, 146));
  check_text(hashed->wspr.call, 12, "K1ABC");
  check_text(hashed->wspr.locator, 6, "FN42AA");
  CHECK(decoder_engine_reset_session(f.engine) == DECODER_OK);
  CHECK(decoder_engine_get_wspr_calls(f.engine, NULL, 0, &count) == DECODER_OK && count == 0);
  decode(&f);
  hashed = find(&f, "<...> FN42AA 33");
  CHECK(hashed->wspr.has_hash15 && !hashed->wspr.hash_resolved && !hashed->wspr.has_call);

  memset(calls, ' ', sizeof calls);
  memcpy(calls[0].call, "K1ABC", 5);
  memcpy(calls[0].grid, "FN42", 4);
  memcpy(calls[1].call, "W9XYZ", 5);
  memcpy(calls[1].grid, "EN50", 4);
  CHECK(decoder_engine_set_wspr_calls(f.engine, calls, 2) == DECODER_OK);
  CHECK(decoder_engine_get_wspr_calls(f.engine, calls, 1, &count) == DECODER_CAPACITY && count == 2);
  ++f.request.attempt_no;
  decode(&f);
  CHECK(find(&f, "<K1ABC> FN42AA 33")->wspr.hash_resolved);
  CHECK(decoder_engine_set_wspr_calls(f.engine, NULL, 0) == DECODER_OK);
  CHECK(decoder_engine_get_wspr_calls(f.engine, NULL, 0, &count) == DECODER_OK && count == 0);

  memset(calls, ' ', sizeof calls);
  memcpy(calls[0].call, "K1ABQ", 5);
  memcpy(calls[0].grid, "FN42", 4);
  memcpy(calls[1].call, "K1AHC", 5);
  CHECK(nhash("K1ABQ", 5, 146) == nhash("K1AHC", 5, 146));
  CHECK(decoder_engine_set_wspr_calls(f.engine, calls, 2) == DECODER_OK);
  CHECK(decoder_engine_get_wspr_calls(f.engine, calls, 2, &count) == DECODER_OK && count == 1);
  check_text(calls[0].call, 12, "K1AHC");
  check_text(calls[0].grid, 4, "");
  CHECK(decoder_engine_set_wspr_calls(f.engine, NULL, 0) == DECODER_OK);

  memset(calls, ' ', sizeof calls);
  memcpy(calls[0].call, "PJ4/K1ABCD", 10);
  memcpy(calls[0].grid, "FN42", 4);
  CHECK(decoder_engine_set_wspr_calls(f.engine, calls, 1) == DECODER_OK);
  const char *unsupported_calls[] = {"W1/AB1CDE/P", "EA8/AB1CDE/P"};
  for (size_t i = 0; i < sizeof unsupported_calls / sizeof *unsupported_calls; ++i) {
    memset(&calls[1], ' ', sizeof calls[1]);
    memcpy(calls[1].call, unsupported_calls[i], strlen(unsupported_calls[i]));
    CHECK(decoder_engine_set_wspr_calls(f.engine, calls, 2) == DECODER_INVALID);
    CHECK(decoder_engine_get_wspr_calls(f.engine, calls, 1, &count) == DECODER_OK && count == 1);
    check_text(calls[0].call, 12, "PJ4/K1ABCD");
    check_text(calls[0].grid, 4, "FN42");
  }
  next_input(&f, "<PJ4/K1ABCD> FN42AA 33", 2, 1520, 1.2, 0);
  decode(&f);
  const decoder_observation *longest_call = find(&f, "<PJ4/K1ABCD> FN42AA 33");
  CHECK(longest_call->wspr.message_type == DECODER_WSPR_HASHED);
  CHECK(longest_call->wspr.has_hash15 && longest_call->wspr.hash_resolved && longest_call->wspr.has_call);
  check_text(longest_call->wspr.call, 12, "PJ4/K1ABCD");
  CHECK(decoder_engine_set_wspr_calls(f.engine, NULL, 0) == DECODER_OK);

  next_input(&f, "PJ4/K1ABC 37", 2, 1480, 0.8, 0);
  decode(&f);
  const decoder_observation *compound = find(&f, "PJ4/K1ABC 37");
  CHECK(compound->wspr.message_type == DECODER_WSPR_COMPOUND);
  CHECK(compound->wspr.has_call && !compound->wspr.has_locator && compound->wspr.power_dbm == 37);
  check_text(compound->wspr.call, 12, "PJ4/K1ABC");

  next_input(&f, "K1ABC FN42 33", 2, 1520, 1.2, 2.0);
  decode(&f);
  CHECK(fabsf(find(&f, "K1ABC FN42 33")->wspr.drift_hz - 2.0f) < 0.6f);

  next_input(&f, "noise", 2, 1500, 1, 0);
  decode(&f);
  CHECK(f.count == 0);
  CHECK(decoder_engine_release_input(f.engine, f.request.input_id) == DECODER_OK);
  ++f.request.input_id;
  f.audio.sample_count = 1;
  decode(&f);
  CHECK(f.count == 0);

  next_input(&f, "K1ABC FN42 33", 15, 1615, 1.2, 0.25);
  f.request.wspr.effort = DECODER_WSPR_FAST;
  decode(&f);
  const decoder_observation *long_period = find(&f, "K1ABC FN42 33");
  CHECK(fabsf(long_period->frequency_hz - 1615) < 0.03f);
  CHECK(fabsf(long_period->wspr.waveform_start_seconds - 1.2f) < 0.5f);
  /* The internal half-Hz refinement step is 0.0625 physical Hz in WSPR-15. */
  CHECK(fabsf(long_period->wspr.drift_hz - 0.25f) < 0.08f);

  CHECK(decoder_engine_reset_session(f.engine) == DECODER_OK);
  f.request = request_for(200, 2);
  f.audio.sample_count = 1;
  f.audio.sample_rate_hz = 48000;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_UNSUPPORTED);
  f.audio.sample_rate_hz = 12000;
  f.request.wspr.waveform = 3;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_INVALID);
  f.request.wspr.waveform = 2;
  f.request.wspr.effort = -1;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_INVALID);
  f.request.wspr.effort = 0;
  f.request.wspr.search_low_hz = NAN;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_INVALID);
  f.request.wspr.search_low_hz = 1600;
  f.request.wspr.search_high_hz = 1400;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_INVALID);
  f.request.wspr.search_low_hz = 1400;
  f.request.wspr.search_high_hz = 1600;
  f.request.wspr.calibration_error_hz = INFINITY;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_INVALID);
  f.request.wspr.calibration_error_hz = 0;
  f.audio.sample_count = sample_count(2) + 1;
  CHECK(decoder_engine_decode(f.engine, &f.request, &f.audio, NULL, NULL, &outcome) == DECODER_INVALID);
  f.audio.sample_count = 1;
  CHECK(decoder_engine_get_wspr_calls(f.engine, NULL, 0, &count) == DECODER_OK && count == 0);
  decode(&f);
  CHECK(f.count == 0);
  CHECK(decoder_engine_get_wspr_calls(f.engine, NULL, 0, &count) == DECODER_OK && count == 0);
  CHECK(decoder_engine_destroy(f.engine) == DECODER_OK);
  free((void *)f.audio.samples);
}

static void decode_wav(int argc, char **argv)
{
  CHECK(argc >= 4);
  const int variant = argc > 4 ? atoi(argv[4]) : 2;
  fixture f = {0};
  f.request = request_for(1, variant);
  int count;
  f.audio.samples = read_wav(argv[2], variant, &count);
  f.audio.sample_count = count;
  f.audio.sample_rate_hz = 12000;
  if (argc > 5) f.request.wspr.effort = atoi(argv[5]);
  if (argc > 6) f.request.wspr.calibration_error_hz = strtof(argv[6], NULL);
  if (argc > 7) f.request.wspr.search_low_hz = strtof(argv[7], NULL);
  if (argc > 8) f.request.wspr.search_high_hz = strtof(argv[8], NULL);
  const double dial = argc > 9 ? strtod(argv[9], NULL) : 0;
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  CHECK(decoder_engine_create(&options, &f.engine) == DECODER_OK);
  decode(&f);
  for (int i = 0; i < f.count; ++i) {
    const decoder_observation *record = &f.records[i];
    printf("%4s %3.0f %4.1f %10.6f %2d  %-s \n", argv[3], record->wspr.snr_db,
           record->dt_seconds, dial + record->frequency_hz / 1e6,
           (int)(record->wspr.drift_hz * (variant == 15 ? 8 : 1)), record->message);
  }
  puts("<DecodeFinished>");
  CHECK(decoder_engine_destroy(f.engine) == DECODER_OK);
  free((void *)f.audio.samples);
}

int main(int argc, char **argv)
{
  if (argc >= 4 && !strcmp(argv[1], "--decode-wav")) {
    decode_wav(argc, argv);
    return EXIT_SUCCESS;
  }
  if ((argc == 7 || argc == 8) && !strcmp(argv[1], "--write-c2")) {
    write_c2(argv[2], argv[3], atoi(argv[4]), strtod(argv[5], NULL), strtod(argv[6], NULL),
             argc > 7 ? strtod(argv[7], NULL) : 0);
    return EXIT_SUCCESS;
  }
  if ((argc == 7 || argc == 8) && !strcmp(argv[1], "--write-wav")) {
    int variant = atoi(argv[4]);
    int16_t *samples = make_signal(argv[3], variant, strtod(argv[5], NULL), strtod(argv[6], NULL),
                                   argc > 7 ? strtod(argv[7], NULL) : 0);
    write_wav(argv[2], samples, sample_count(variant));
    free(samples);
    return EXIT_SUCCESS;
  }
  CHECK(argc == 1);
  check_lifecycle();
  return EXIT_SUCCESS;
}
