#include "DecoderEngine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); \
  } \
} while (0)

extern void decoder_engine_test_signal(int16_t samples[180000], int8_t payload[77],
                                       int8_t tones[79], int32_t start_sample);
extern void decoder_engine_test_ft8_message(const char text[37], float carrier,
                                            int16_t samples[180000], int8_t payload[77],
                                            int8_t tones[79], int32_t start_sample);
extern void decoder_engine_test_qso_snapshot(int32_t *valid, int32_t *suppressed,
                                             char message[38], float *dt, char partner[13]);
extern void decoder_engine_test_records(decoder_observation records[2], size_t *record_size);
extern void decoder_engine_test_residual(float samples[180000]);
extern void decoder_engine_test_host_windows(void);

static void check_evidence_layout(void)
{
  decoder_observation records[2];
  size_t record_size = 0;
  decoder_engine_test_records(records, &record_size);
  CHECK(record_size == sizeof records[0]);
  CHECK(records[0].mode == DECODER_MODE_FT4);
  CHECK(records[0].dt_seconds == 0.25f);
  CHECK(records[0].ft4.payload77[76] == 1 && records[0].ft4.tones[102] == 3);
  CHECK(records[0].ft4.payload_origin == DECODER_PAYLOAD_DECODED);
  CHECK(records[0].ft4.has_tones == 1 && records[0].ft4.has_waveform_start == 1);
  CHECK(records[0].ft4.waveform_start_seconds == -0.125f);
  CHECK(records[0].ft8.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(records[1].mode == DECODER_MODE_FT8);
  CHECK(records[1].variant == DECODER_FT8_SUPERFOX);
  CHECK(records[1].superfox.symbols[49] == 127);
  CHECK(records[1].superfox.kind == DECODER_SUPERFOX_VERIFICATION);
  CHECK(records[1].superfox.child_index == 4);
  CHECK(records[1].ft8.payload_origin == DECODER_PAYLOAD_NONE);
  CHECK(records[1].ft4.payload_origin == DECODER_PAYLOAD_NONE);
}

typedef struct {
  decoder_engine_handle engine;
  const decoder_attempt_request *request;
  const decoder_audio_view *audio;
  int count;
  int found_expected;
  const char *expected_message;
  int8_t expected_payload[77];
  int8_t expected_tones[79];
  float expected_start_seconds;
  decoder_observation retained;
  decoder_observation expected_record;
} callback_context;

static void observe(const decoder_observation *record, void *opaque)
{
  callback_context *context = opaque;
  decoder_attempt_outcome outcome;
  CHECK(record->input_id == context->request->input_id);
  CHECK(record->analysis_id == context->request->analysis_id);
  CHECK(record->attempt_no == context->request->attempt_no);
  CHECK(record->mode == DECODER_MODE_FT8);
  ++context->count;
  context->retained = *record;

  if (strcmp(record->message, context->expected_message) == 0) {
    CHECK(record->ft8.payload_origin == DECODER_PAYLOAD_DECODED);
    CHECK(memcmp(record->ft8.payload77, context->expected_payload, 77) == 0);
    CHECK(memcmp(record->ft8.tones, context->expected_tones, 79) == 0);
    CHECK(record->ft8.has_tones != 0);
    CHECK(record->ft8.has_waveform_start != 0);
    CHECK(fabsf(record->ft8.waveform_start_seconds - context->expected_start_seconds) < 0.035f);
    CHECK(fabsf(record->dt_seconds - (context->expected_start_seconds - 0.5f)) < 0.035f);
    CHECK(record->ft4.payload_origin == DECODER_PAYLOAD_NONE);
    CHECK(record->ft4.has_tones == 0 && record->ft4.has_waveform_start == 0);
    CHECK(record->superfox.kind == 0 && record->superfox.child_index == 0);
    context->expected_record = *record;
    ++context->found_expected;
  }

  CHECK(decoder_engine_release_input(context->engine, record->input_id) == DECODER_BUSY);
  CHECK(decoder_engine_reset_session(context->engine) == DECODER_BUSY);
  CHECK(decoder_engine_destroy(context->engine) == DECODER_BUSY);
  CHECK(decoder_engine_decode(context->engine, context->request, context->audio,
                             observe, context, &outcome) == DECODER_BUSY);
}

static decoder_attempt_request request_for(int64_t input_id, int64_t analysis_id)
{
  decoder_attempt_request request = {0};
  request.input_id = input_id;
  request.analysis_id = analysis_id;
  request.attempt_no = 1;
  request.mode = DECODER_MODE_FT8;
  request.phase = DECODER_PHASE_NORMAL;
  request.source = DECODER_SOURCE_FILE;
  request.ft8.half_symbol_stage = 50;
  request.ft8.utc = 120000;
  request.ft8.receive_frequency_hz = 1500;
  request.ft8.transmit_frequency_hz = 1500;
  request.ft8.search_low_hz = 200;
  request.ft8.search_high_hz = 4000;
  request.ft8.tolerance_hz = 20;
  request.ft8.depth = 1;
  request.ft8.ap_width_hz = 75;
  request.ft8.cycles = 1;
  request.ft8.threads = 1;
  request.ft8.discard_leading_seconds = 0;
  request.ft8.date = 260928;
  memset(request.ft8.mycall, ' ', sizeof request.ft8.mycall);
  memset(request.ft8.my_base_call, ' ', sizeof request.ft8.my_base_call);
  memset(request.ft8.hiscall, ' ', sizeof request.ft8.hiscall);
  memset(request.ft8.his_base_call, ' ', sizeof request.ft8.his_base_call);
  memset(request.ft8.mygrid, ' ', sizeof request.ft8.mygrid);
  memset(request.ft8.hisgrid, ' ', sizeof request.ft8.hisgrid);
  return request;
}

static decoder_attempt_request mtd_request_for(int64_t input_id, int64_t analysis_id)
{
  decoder_attempt_request request = request_for(input_id, analysis_id);
  request.ft8.mtd = 1;
  request.ft8.depth = 3;
  request.ft8.threads = 2;
  request.ft8.candidate_thinning = 100;
  request.ft8.receive_sensitivity = 3;
  request.ft8.low_threshold = 1;
  request.ft8.subtract_pass = 1;
  request.ft8.wide_dx_search = 1;
  return request;
}

static void decode_signal(callback_context *context, const int16_t *original,
                          decoder_attempt_outcome *outcome)
{
  context->count = 0;
  context->found_expected = 0;
  CHECK(decoder_engine_decode(context->engine, context->request, context->audio,
                             observe, context, outcome) == DECODER_OK);
  CHECK(outcome->status == DECODER_OK);
  CHECK(context->found_expected > 0);
  CHECK(context->count == outcome->observation_count);
  CHECK(outcome->retained_count > 0 && outcome->evidence_dropped == 0);
  CHECK(memcmp(context->audio->samples, original, 180000 * sizeof *original) == 0);
}

static void check_same_result(const callback_context *context,
                              const decoder_observation *reference, int count)
{
  CHECK(context->count == count);
  CHECK(strcmp(context->expected_record.message, reference->message) == 0);
  CHECK(context->expected_record.variant == reference->variant);
  CHECK(context->expected_record.ft8.payload_origin == reference->ft8.payload_origin);
  CHECK(memcmp(context->expected_record.ft8.payload77, reference->ft8.payload77, 77) == 0);
  CHECK(memcmp(context->expected_record.ft8.tones, reference->ft8.tones, 79) == 0);
}

static void check_reset_and_recreate(callback_context *context,
                                    decoder_attempt_request *request,
                                    const int16_t *original,
                                    const decoder_engine_options *options,
                                    decoder_attempt_outcome *outcome)
{
  decoder_observation reference = context->expected_record;
  int reference_count = context->count;
  CHECK(decoder_engine_reset_session(context->engine) == DECODER_OK);
  ++request->input_id;
  ++request->analysis_id;
  decode_signal(context, original, outcome);
  check_same_result(context, &reference, reference_count);
  CHECK(decoder_engine_destroy(context->engine) == DECODER_OK);
  CHECK(decoder_engine_create(options, &context->engine) == DECODER_OK);
  ++request->input_id;
  ++request->analysis_id;
  decode_signal(context, original, outcome);
  check_same_result(context, &reference, reference_count);
}

static void check_repeat_stages_and_eme(callback_context *context, const int16_t *original)
{
  decoder_attempt_request request = request_for(701, 801);
  decoder_attempt_outcome outcome;
  context->request = &request;
  request.phase = DECODER_PHASE_REPEAT;
  request.ft8.eme_delay_seconds = 2.0f;
  CHECK(decoder_engine_reset_session(context->engine) == DECODER_OK);
  for (int stage = 41; stage < 50; ++stage) {
    request.ft8.half_symbol_stage = stage;
    ++request.attempt_no;
    CHECK(decoder_engine_decode(context->engine, &request, context->audio,
                               NULL, NULL, &outcome) == DECODER_OK);
    CHECK(outcome.observation_count == 0);
  }
  request.ft8.half_symbol_stage = 50;
  ++request.attempt_no;
  decode_signal(context, original, &outcome);
}

static void probe_mtd_residual(callback_context *context, decoder_attempt_request *request,
                               int source, float *residual)
{
  decoder_attempt_request probe = *request;
  decoder_attempt_outcome outcome;
  /* Stop after input preparation so decoding cannot modify the residual. */
  probe.ft8.filtered_retry = 1;
  probe.ft8.receive_frequency_hz = 3000;
  probe.ft8.search_low_hz = 1000;
  probe.ft8.search_high_hz = 2000;
  probe.phase = DECODER_PHASE_REPEAT;
  probe.source = source;
  probe.attempt_no = ++request->attempt_no;
  CHECK(decoder_engine_decode(context->engine, &probe, context->audio,
                             NULL, NULL, &outcome) == DECODER_OK);
  decoder_engine_test_residual(residual);
}

static void check_mtd_lifecycle(callback_context *context, decoder_attempt_request *request,
                                const int16_t *original, const decoder_engine_options *options)
{
  float *saved = malloc(180000 * sizeof *saved);
  float *current = malloc(180000 * sizeof *current);
  decoder_attempt_outcome outcome;
  decoder_observation reference = context->expected_record;
  int reference_count = context->count;
  int changed = 0;
  CHECK(saved != NULL && current != NULL);
  decoder_engine_test_residual(saved);
  for (int i = 0; i < 180000; ++i)
    changed |= saved[i] != context->audio->samples[i];
  CHECK(changed);

  probe_mtd_residual(context, request, DECODER_SOURCE_FILE, current);
  CHECK(memcmp(saved, current, 180000 * sizeof *saved) == 0);

  for (int recreate = 0; recreate <= 1; ++recreate) {
    if (recreate) {
      CHECK(decoder_engine_destroy(context->engine) == DECODER_OK);
      CHECK(decoder_engine_create(options, &context->engine) == DECODER_OK);
    } else {
      CHECK(decoder_engine_reset_session(context->engine) == DECODER_OK);
    }
    ++request->input_id;
    ++request->analysis_id;
    request->attempt_no = 1;
    probe_mtd_residual(context, request, DECODER_SOURCE_FILE, current);
    for (int i = 0; i < 180000; ++i)
      CHECK(current[i] == context->audio->samples[i]);

    ++request->attempt_no;
    decode_signal(context, original, &outcome);
    check_same_result(context, &reference, reference_count);
    decoder_engine_test_residual(current);
    changed = 0;
    for (int i = 0; i < 180000; ++i)
      changed |= current[i] != context->audio->samples[i];
    CHECK(changed);
    if (recreate) {
      CHECK(decoder_engine_release_input(context->engine, request->input_id) == DECODER_OK);
      ++request->input_id;
      ++request->analysis_id;
      request->attempt_no = 1;
    }
    probe_mtd_residual(context, request,
                       recreate ? DECODER_SOURCE_FILE : DECODER_SOURCE_LIVE, current);
    for (int i = 0; i < 180000; ++i)
      CHECK(current[i] == context->audio->samples[i]);
  }
  free(current);
  free(saved);
}

static void check_mtd_qso_boundary(callback_context *context, int16_t *samples,
                                   int16_t *original)
{
  const char expected[] = "K1JT HA0DU -10";
  char text[37], message[38], partner[13];
  int32_t valid, suppressed;
  float dt;
  decoder_attempt_outcome outcome;
  memset(text, ' ', sizeof text);
  memcpy(text, expected, strlen(expected));
  context->expected_message = expected;
  context->expected_start_seconds = 0.5f;
  decoder_engine_test_ft8_message(text, 1600.0f, samples, context->expected_payload,
                                  context->expected_tones, 6000);
  memcpy(original, samples, 180000 * sizeof *samples);

  for (int boundary = 0; boundary < 2; ++boundary) {
    decoder_attempt_request request = mtd_request_for(901 + boundary, 1001 + boundary);
    request.ft8.receive_frequency_hz = 1600;
    request.ft8.transmit_frequency_hz = 1600;
    request.ft8.search_low_hz = boundary ? 200 : 1601;
    request.ft8.search_high_hz = 3000;
    request.ft8.filtered_retry = 0;
    request.ft8.qso_progress = 1;
    request.ft8.standard_mycall = 1;
    request.ft8.standard_hiscall = 1;
    memcpy(request.ft8.mycall, "K1JT", 4);
    memcpy(request.ft8.my_base_call, "K1JT", 4);
    memcpy(request.ft8.hiscall, "HA0DU", 5);
    memcpy(request.ft8.his_base_call, "HA0DU", 5);
    CHECK(decoder_engine_reset_session(context->engine) == DECODER_OK);
    context->request = &request;
    /* The 1601-Hz lower bound excludes the QSO from every integer worker range,
       while the first FFT bin still contains the 1600-Hz signal. */
    decode_signal(context, original, &outcome);
    CHECK(context->expected_record.variant == DECODER_FT8_MTD);
    decoder_engine_test_qso_snapshot(&valid, &suppressed, message, &dt, partner);
    CHECK(valid && suppressed);
    CHECK(strcmp(message, expected) == 0);
    CHECK(strcmp(partner, "HA0DU") == 0);
    CHECK(fabsf(dt - context->expected_record.dt_seconds) < 0.001f);
  }
  context->expected_message = "CQ K1JT FN20";
}

static void write_le(FILE *file, uint32_t value, unsigned bytes)
{
  for (unsigned i = 0; i < bytes; ++i) CHECK(fputc((value >> (8 * i)) & 255, file) != EOF);
}

static void write_wav(const char *path)
{
  const int count = 180000;
  int16_t *samples = malloc(count * sizeof *samples);
  int8_t payload[77], tones[79];
  CHECK(samples);
  decoder_engine_test_signal(samples, payload, tones, 6000);
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
  free(samples);
}

int main(int argc, char **argv)
{
  if (argc == 3 && !strcmp(argv[1], "--write-wav")) {
    write_wav(argv[2]);
    return 0;
  }
  const int qso_boundary_only = argc == 2 && !strcmp(argv[1], "--qso-boundary");
  CHECK(argc == 1 || qso_boundary_only);
  decoder_engine_options options = {DECODER_ENGINE_ABI};
  decoder_engine_handle engine = NULL, second = NULL;
  decoder_engine_capabilities capabilities;
  decoder_attempt_request request = request_for(101, 201);
  decoder_attempt_outcome outcome;
  int16_t *samples = calloc(180000, sizeof *samples);
  int16_t *before = calloc(180000, sizeof *before);
  decoder_audio_view audio = {samples, 180000, 12000};
  callback_context context = {0};
  CHECK(samples != NULL && before != NULL);
  check_evidence_layout();

  options.abi_version = DECODER_ENGINE_ABI - 1;
  CHECK(decoder_engine_create(&options, &engine) == DECODER_UNSUPPORTED);
  CHECK(engine == NULL);
  options.abi_version = DECODER_ENGINE_ABI;
  CHECK(decoder_engine_create(&options, &engine) == DECODER_OK);
  CHECK(engine != NULL);
  CHECK(decoder_engine_create(&options, &second) == DECODER_BUSY);
  CHECK(decoder_engine_get_capabilities(engine, &capabilities) == DECODER_OK);
  CHECK(capabilities.abi_version == DECODER_ENGINE_ABI);
  CHECK(capabilities.supported_modes == (DECODER_SUPPORT_FT8 | DECODER_SUPPORT_FT4 | DECODER_SUPPORT_JT9 | DECODER_SUPPORT_JT65 | DECODER_SUPPORT_Q65 | DECODER_SUPPORT_FST4 | DECODER_SUPPORT_FST4W | DECODER_SUPPORT_JT4));
  CHECK(capabilities.cancellation == 0);
  CHECK(capabilities.concurrent_sessions == 0);
  CHECK(capabilities.evidence_capacity == 1024);

  context.expected_message = "CQ K1JT FN20";
  context.engine = engine;
  context.request = &request;
  context.audio = &audio;
  if (qso_boundary_only) {
    check_mtd_qso_boundary(&context, samples, before);
    CHECK(decoder_engine_destroy(context.engine) == DECODER_OK);
    free(before);
    free(samples);
    return EXIT_SUCCESS;
  }
  request.mode = 123;
  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_UNSUPPORTED);
  request.mode = DECODER_MODE_FT4;
  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_INVALID);
  request.mode = DECODER_MODE_FT8;
  audio.sample_rate_hz = 48000;
  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_UNSUPPORTED);
  audio.sample_rate_hz = 12000;
  audio.sample_count = 180001;
  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_INVALID);
  audio.sample_count = 0;
  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_INVALID);
  audio.sample_count = 180000;
  for (int stage = 40; stage <= 51; stage += 11) {
    request.ft8.half_symbol_stage = stage;
    CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_INVALID);
  }
  request.ft8.half_symbol_stage = 50;
  int32_t const progress = request.ft8.qso_progress;
  for (int value = -1; value <= 6; value += 7) {
    request.ft8.qso_progress = value;
    CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_INVALID);
  }
  request.ft8.qso_progress = progress;
  CHECK(context.count == 0);

  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_OK);
  CHECK(outcome.status == DECODER_OK);
  CHECK(outcome.observation_count == 0 && context.count == 0);
  CHECK(outcome.retained_count == 0 && outcome.evidence_dropped == 0);
  CHECK(memcmp(samples, before, 180000 * sizeof *samples) == 0);
  CHECK(decoder_engine_release_input(engine, request.input_id) == DECODER_OK);

  context.expected_start_seconds = 0.5f;
  decoder_engine_test_signal(samples, context.expected_payload, context.expected_tones, 6000);
  memcpy(before, samples, 180000 * sizeof *samples);
  request = request_for(102, 202);
  decode_signal(&context, before, &outcome);

  /* A different reception cannot silently replace retained input state. */
  request.input_id = 103;
  CHECK(decoder_engine_decode(engine, &request, &audio, observe, &context, &outcome) == DECODER_INVALID);
  request.input_id = 102;
  check_reset_and_recreate(&context, &request, before, &options, &outcome);
  CHECK(decoder_engine_release_input(context.engine, request.input_id) == DECODER_OK);
  CHECK(context.retained.input_id == request.input_id);
  CHECK(context.retained.analysis_id == request.analysis_id);

  memset(samples, 0, 180000 * sizeof *samples);
  context.count = 0;
  request = request_for(105, 205);
  CHECK(decoder_engine_decode(context.engine, &request, &audio, observe, &context, &outcome) == DECODER_OK);
  CHECK(outcome.observation_count == 0 && context.count == 0);
  CHECK(outcome.retained_count == 0);
  CHECK(decoder_engine_reset_session(context.engine) == DECODER_OK);
  memcpy(samples, before, 180000 * sizeof *samples);
  request = mtd_request_for(301, 401);
  decode_signal(&context, before, &outcome);
  CHECK(context.expected_record.variant == DECODER_FT8_MTD);
  check_mtd_lifecycle(&context, &request, before, &options);
  /* Classic crops EME input; MTD analyzes the original sample coordinates. */
  context.expected_start_seconds = 2.15f;
  decoder_engine_test_signal(samples, context.expected_payload, context.expected_tones, 25800);
  memcpy(before, samples, 180000 * sizeof *samples);
  check_repeat_stages_and_eme(&context, before);
  context.request = &request;
  CHECK(decoder_engine_reset_session(context.engine) == DECODER_OK);
  request = mtd_request_for(502, 602);
  request.ft8.eme_delay_seconds = 2.0f;
  decode_signal(&context, before, &outcome);
  check_mtd_qso_boundary(&context, samples, before);
  CHECK(decoder_engine_destroy(context.engine) == DECODER_OK);
  decoder_engine_test_host_windows();
  free(before);
  free(samples);
  return EXIT_SUCCESS;
}
