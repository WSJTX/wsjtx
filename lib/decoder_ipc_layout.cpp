#include "decoder_ipc_layout.h"

#include "../commons.h"
#include "decoder_ipc_control.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

static_assert (sizeof (int) == 4, "decoder IPC layout words require 32-bit integers");
static_assert (sizeof (decoder_ipc_layout_t) == (14 + 2 * DECODER_IPC_LAYOUT_FIELDS) * 4,
               "decoder IPC layout descriptor must not contain padding");
static_assert (sizeof (shared_dec_data_t) <= std::numeric_limits<int>::max (),
               "decoder IPC sizes must fit in layout words");

extern "C" void decoder_ipc_expected_layout (decoder_ipc_layout_t * layout)
{
  if (!layout) return;
  *layout = {};
  layout->magic = DECODER_IPC_LAYOUT_MAGIC;
  layout->protocol_version = DECODER_IPC_VERSION;
  layout->header_bytes = offsetof (shared_dec_data_t, payload);
  layout->payload_bytes = sizeof (dec_data_t);
  layout->logical_bytes = sizeof (shared_dec_data_t);
  layout->payload_offset = offsetof (shared_dec_data_t, payload);
  layout->capabilities = DECODER_IPC_CAPABILITIES;
  layout->int_bytes = sizeof (int);
  layout->short_bytes = sizeof (short);
  layout->float_bytes = sizeof (float);
  layout->bool_bytes = sizeof (bool);
  layout->char_bytes = sizeof (char);
  layout->endian_marker = DECODER_IPC_ENDIAN_MARKER;
  layout->field_count = DECODER_IPC_LAYOUT_FIELDS;

  std::size_t index {0};
#define FIELD(member) \
  layout->fields[index++] = {offsetof (shared_dec_data_t, member), \
                            sizeof (((shared_dec_data_t *) nullptr)->member)}
  FIELD (control.generation);
  FIELD (control.state);
  FIELD (control.version);
  FIELD (control.progress);
  FIELD (payload.ss);
  FIELD (payload.savg);
  FIELD (payload.sred);
  FIELD (payload.d2);
  FIELD (payload.params);
  FIELD (payload.params.nutc);
  FIELD (payload.params.ndiskdat);
  FIELD (payload.params.ntrperiod);
  FIELD (payload.params.nQSOProgress);
  FIELD (payload.params.nfqso);
  FIELD (payload.params.nftx);
  FIELD (payload.params.newdat);
  FIELD (payload.params.nfa);
  FIELD (payload.params.nfb);
  FIELD (payload.params.ntol);
  FIELD (payload.params.kin);
  FIELD (payload.params.nzhsym);
  FIELD (payload.params.nsubmode);
  FIELD (payload.params.nagain);
  FIELD (payload.params.ndepth);
  FIELD (payload.params.lft8apon);
  FIELD (payload.params.lapcqonly);
  FIELD (payload.params.ljt65apon);
  FIELD (payload.params.napwid);
  FIELD (payload.params.nmode);
  FIELD (payload.params.minw);
  FIELD (payload.params.nclearave);
  FIELD (payload.params.minSync);
  FIELD (payload.params.emedelay);
  FIELD (payload.params.dttol);
  FIELD (payload.params.nlist);
  FIELD (payload.params.listutc);
  FIELD (payload.params.n2pass);
  FIELD (payload.params.nranera);
  FIELD (payload.params.naggressive);
  FIELD (payload.params.nexp_decode);
  FIELD (payload.params.max_drift);
  FIELD (payload.params.datetime);
  FIELD (payload.params.mycall);
  FIELD (payload.params.mygrid);
  FIELD (payload.params.hiscall);
  FIELD (payload.params.hisgrid);
  FIELD (payload.params.b_even_seq);
  FIELD (payload.params.b_superfox);
  FIELD (payload.params.yymmdd);
  FIELD (payload.params.mybcall);
  FIELD (payload.params.hisbcall);
  FIELD (payload.params.ncandthin);
  FIELD (payload.params.ndtcenter);
  FIELD (payload.params.nft8cycles);
  FIELD (payload.params.ntrials10);
  FIELD (payload.params.ntrialsrxf10);
  FIELD (payload.params.nharmonicsdepth);
  FIELD (payload.params.ntopfreq65);
  FIELD (payload.params.nprepass);
  FIELD (payload.params.nsdecatt);
  FIELD (payload.params.nlasttx);
  FIELD (payload.params.ndelay);
  FIELD (payload.params.nmt);
  FIELD (payload.params.nft8rxfsens);
  FIELD (payload.params.nft4depth);
  FIELD (payload.params.nsecbandchanged);
  FIELD (payload.params.nagainfil);
  FIELD (payload.params.nstophint);
  FIELD (payload.params.nhint);
  FIELD (payload.params.fmaskact);
  FIELD (payload.params.lmultift8);
  FIELD (payload.params.lft8lowth);
  FIELD (payload.params.lft8subpass);
  FIELD (payload.params.ltxing);
  FIELD (payload.params.lhideft8dupes);
  FIELD (payload.params.lhound);
  FIELD (payload.params.lcommonft8b);
  FIELD (payload.params.lmycallstd);
  FIELD (payload.params.lhiscallstd);
  FIELD (payload.params.lapmyc);
  FIELD (payload.params.lmodechanged);
  FIELD (payload.params.lbandchanged);
  FIELD (payload.params.lenabledxcsearch);
  FIELD (payload.params.lwidedxcsearch);
  FIELD (payload.params.lmultinst);
  FIELD (payload.params.lskiptx1);
  FIELD (payload.params.ndecoderstart);
  FIELD (metadata);
  FIELD (metadata.input_id);
  FIELD (metadata.analysis_id);
  FIELD (metadata.attempt_no);
  FIELD (metadata.valid_samples);
#undef FIELD
}

extern "C" int decoder_ipc_validate_layout (
    void const * storage, std::size_t available_bytes,
    decoder_ipc_layout_t const * peer_layout)
{
  if (!storage) return DECODER_IPC_LAYOUT_NULL;
  if (available_bytes < sizeof (decoder_ipc_control_t))
    return DECODER_IPC_LAYOUT_TRUNCATED;
  if (reinterpret_cast<std::uintptr_t> (storage) % alignof (shared_dec_data_t))
    return DECODER_IPC_LAYOUT_ALIGNMENT;

  auto const * control = static_cast<decoder_ipc_control_t const *> (storage);
  if (decoder_ipc_atomic_load (&control->version) != DECODER_IPC_VERSION)
    return DECODER_IPC_LAYOUT_VERSION;
  if (available_bytes < offsetof (shared_dec_data_t, payload))
    return DECODER_IPC_LAYOUT_TRUNCATED;

  decoder_ipc_layout_t expected;
  decoder_ipc_expected_layout (&expected);
  if (peer_layout && std::memcmp (peer_layout, &expected, sizeof expected))
    return DECODER_IPC_LAYOUT_MISMATCH;

  decoder_ipc_layout_t actual;
  std::memcpy (&actual, static_cast<unsigned char const *> (storage)
                        + offsetof (shared_dec_data_t, layout), sizeof actual);
  if (actual.protocol_version != expected.protocol_version)
    return DECODER_IPC_LAYOUT_VERSION;
  if (actual.header_bytes != expected.header_bytes)
    return DECODER_IPC_LAYOUT_HEADER_SIZE;
  if (actual.payload_bytes != expected.payload_bytes)
    return DECODER_IPC_LAYOUT_PAYLOAD_SIZE;
  if (actual.logical_bytes != expected.logical_bytes)
    return DECODER_IPC_LAYOUT_LOGICAL_SIZE;
  if (actual.capabilities != expected.capabilities)
    return DECODER_IPC_LAYOUT_CAPABILITIES;
  if (std::memcmp (&actual, &expected, sizeof expected))
    return DECODER_IPC_LAYOUT_MISMATCH;
  if (available_bytes < static_cast<std::size_t> (expected.logical_bytes))
    return DECODER_IPC_LAYOUT_TRUNCATED;

  auto const state = decoder_ipc_atomic_load (&control->state);
  if (state != DECODER_IPC_IDLE && state != DECODER_IPC_READY
      && state != DECODER_IPC_DECODING && state != DECODER_IPC_COMPLETE
      && state != DECODER_IPC_SHUTDOWN)
    return DECODER_IPC_LAYOUT_STATE;
  return DECODER_IPC_LAYOUT_OK;
}

extern "C" char const * decoder_ipc_layout_status_name (int status)
{
  switch (status)
    {
    case DECODER_IPC_LAYOUT_OK: return "ok";
    case DECODER_IPC_LAYOUT_NULL: return "null-segment";
    case DECODER_IPC_LAYOUT_TRUNCATED: return "truncated-segment";
    case DECODER_IPC_LAYOUT_ALIGNMENT: return "misaligned-segment";
    case DECODER_IPC_LAYOUT_VERSION: return "unsupported-version";
    case DECODER_IPC_LAYOUT_HEADER_SIZE: return "wrong-header-size";
    case DECODER_IPC_LAYOUT_PAYLOAD_SIZE: return "wrong-payload-size";
    case DECODER_IPC_LAYOUT_MISMATCH: return "incompatible-layout";
    case DECODER_IPC_LAYOUT_CAPABILITIES: return "unsupported-capabilities";
    case DECODER_IPC_LAYOUT_STATE: return "invalid-state";
    case DECODER_IPC_LAYOUT_LOGICAL_SIZE: return "wrong-logical-size";
    case DECODER_IPC_LAYOUT_ATTACH: return "attach-failed";
    default: return "unknown-status";
    }
}

extern "C" void decoder_ipc_report_layout_error (
    int status, void const * storage, std::size_t available_bytes)
{
  decoder_ipc_layout_t expected;
  decoder_ipc_expected_layout (&expected);
  int version {-1};
  int header {-1};
  int payload {-1};
  auto const * bytes = static_cast<unsigned char const *> (storage);
  if (storage && available_bytes >= offsetof (decoder_ipc_control_t, version) + sizeof version)
    std::memcpy (&version, bytes + offsetof (decoder_ipc_control_t, version), sizeof version);
  if (storage && available_bytes >= offsetof (shared_dec_data_t, layout) + sizeof expected)
    {
      decoder_ipc_layout_t actual;
      std::memcpy (&actual, bytes + offsetof (shared_dec_data_t, layout), sizeof actual);
      header = actual.header_bytes;
      payload = actual.payload_bytes;
    }
  std::fprintf (stdout, "<DecoderError> status=%s\n", decoder_ipc_layout_status_name (status));
  std::fprintf (stdout, "Decoder IPC expected version=%d header=%d payload=%d; "
                        "actual version=%d header=%d payload=%d available=%zu\n",
                DECODER_IPC_VERSION, expected.header_bytes, expected.payload_bytes,
                version, header, payload, available_bytes);
  std::fflush (stdout);
}
