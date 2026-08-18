#include "DecoderIpc.hpp"

#include "lib/decoder_ipc_control.h"

#include <cstring>
#include <limits>

namespace
{
  bool hasCurrentProtocol (shared_dec_data_t const& shared)
  {
    return DECODER_IPC_VERSION == DecoderIpc::protocolVersion (shared);
  }

  bool parseField (QByteArray const& field, qint32& value)
  {
    bool ok {false};
    auto const parsed = field.toInt (&ok);
    if (!ok) return false;
    value = parsed;
    return true;
  }

  bool parseGeneration (QByteArray const& text, qint32& generation)
  {
    if (text.isEmpty ()) return false;
    for (auto const digit: text)
      {
        if (digit < '0' || digit > '9') return false;
      }
    return parseField (text, generation) && generation > 0;
  }
}

qint32 DecoderIpc::nextGeneration (qint32 current)
{
  return current <= 0 || current == std::numeric_limits<qint32>::max ()
    ? 1 : current + 1;
}

bool DecoderIpc::hasUsableSize (qint64 size)
{
  return size >= static_cast<qint64> (sizeof (shared_dec_data_t));
}

bool DecoderIpc::hasShutdownControlSize (qint64 size)
{
  return size >= static_cast<qint64> (offsetof (decoder_ipc_control_t, progress));
}

qint32 DecoderIpc::state (shared_dec_data_t const& shared)
{
  return decoder_ipc_atomic_load (&shared.control.state);
}

qint32 DecoderIpc::generation (shared_dec_data_t const& shared)
{
  return decoder_ipc_atomic_load (&shared.control.generation);
}

qint32 DecoderIpc::progress (shared_dec_data_t const& shared)
{
  return decoder_ipc_atomic_load (&shared.control.progress);
}

qint32 DecoderIpc::protocolVersion (shared_dec_data_t const& shared)
{
  return decoder_ipc_atomic_load (&shared.control.version);
}

void DecoderIpc::initialize (shared_dec_data_t& shared)
{
  std::memset (&shared, 0, sizeof (shared));
  decoder_ipc_control_initialize (&shared.control.generation, &shared.control.state,
                                  &shared.control.version, &shared.control.progress);
}

void DecoderIpc::shutdown (shared_dec_data_t& shared)
{
  shutdown (shared.control);
}

void DecoderIpc::shutdown (decoder_ipc_control_t& control)
{
  shutdownControl (&control);
}

void DecoderIpc::shutdownControl (void * control)
{
  auto * words = static_cast<int *> (control);
  decoder_ipc_control_shutdown (words + 1, words + 2);
}

bool DecoderIpc::publish (shared_dec_data_t& shared, dec_data_t const& payload,
                          bool copySamples, qint32 generation)
{
  if (!hasCurrentProtocol (shared)
      || DECODER_IPC_IDLE != state (shared)
      || generation <= 0)
    {
      return false;
    }

  if (copySamples)
    {
      shared.payload = payload;
    }
  else
    {
      shared.payload.params = payload.params;
    }
  return decoder_ipc_control_publish (&shared.control.generation,
                                      &shared.control.state,
                                      &shared.control.version,
                                      &shared.control.progress, generation);
}

bool DecoderIpc::publishFt8Mtd (shared_dec_data_t& shared,
                                Ft8MtdPayload const& payload,
                                qint32 generation)
{
  if (!hasCurrentProtocol (shared)
      || DECODER_IPC_IDLE != state (shared)
      || generation <= 0
      || 8 != payload.params.nmode
      || !payload.params.lmultift8)
    {
      return false;
    }

  shared.payload.params = payload.params;
  std::memcpy (shared.payload.d2, payload.samples.data (),
               sizeof payload.samples);
  return decoder_ipc_control_publish (&shared.control.generation,
                                      &shared.control.state,
                                      &shared.control.version,
                                      &shared.control.progress, generation);
}

bool DecoderIpc::claim (shared_dec_data_t& shared, qint32& generation)
{
  return DECODER_IPC_CLAIMED == decoder_ipc_control_try_claim (
      &shared.control.generation, &shared.control.state, &shared.control.version,
      &generation);
}

bool DecoderIpc::finish (shared_dec_data_t& shared, qint32 generation)
{
  return decoder_ipc_control_finish (&shared.control.generation,
                                     &shared.control.state,
                                     &shared.control.version, generation);
}

bool DecoderIpc::consume (shared_dec_data_t& shared, qint32 generation)
{
  return decoder_ipc_control_consume (&shared.control.generation,
                                      &shared.control.state,
                                      &shared.control.version, generation);
}

bool DecoderIpc::parseStart (QByteArray line, qint32 * generation)
{
  if (!generation) return false;
  if (line.endsWith ('\n')) line.chop (1);
  if (line.endsWith ('\r')) line.chop (1);

  QByteArray const prefix {"<DecodeStarted> gen="};
  if (!line.startsWith (prefix)) return false;

  qint32 parsed {0};
  if (!parseGeneration (line.mid (prefix.size ()), parsed)) return false;
  *generation = parsed;
  return true;
}

bool DecoderIpc::parseCompletion (QByteArray line, Completion * completion)
{
  if (!completion) return false;
  if (line.endsWith ('\n')) line.chop (1);
  if (line.endsWith ('\r')) line.chop (1);

  QByteArray const prefix {"<DecodeFinished>"};
  QByteArray const generationPrefix {" gen="};
  constexpr int synchronizedWidth {4};
  constexpr int decodedWidth {4};
  constexpr int averageWidth {9};
  auto const synchronizedOffset = prefix.size ();
  auto const decodedOffset = synchronizedOffset + synchronizedWidth;
  auto const averageOffset = decodedOffset + decodedWidth;
  auto const generationOffset = averageOffset + averageWidth;
  if (!line.startsWith (prefix)
      || line.mid (generationOffset, generationPrefix.size ()) != generationPrefix)
    {
      return false;
    }

  Completion parsed {};
  if (!parseField (line.mid (synchronizedOffset, synchronizedWidth),
                   parsed.synchronized)
      || !parseField (line.mid (decodedOffset, decodedWidth), parsed.decoded)
      || !parseField (line.mid (averageOffset, averageWidth), parsed.average))
    {
      return false;
    }

  auto const generationText = line.mid (generationOffset + generationPrefix.size ());
  if (!parseGeneration (generationText, parsed.generation))
    {
      return false;
    }
  *completion = parsed;
  return true;
}
