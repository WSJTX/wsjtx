#ifndef DECODER_IPC_HPP
#define DECODER_IPC_HPP

#include <QByteArray>
#include <QtGlobal>

#include <array>

#include "commons.h"

namespace DecoderIpc
{
  // FT8 reads the complete 15-second buffer even when decoding starts earlier.
  constexpr std::size_t Ft8SampleCount {15 * RX_SAMPLE_RATE};

  struct Ft8MtdPayload
  {
    std::array<short, Ft8SampleCount> samples {};
    decoder_params_t params {};
  };

  struct Completion
  {
    qint32 synchronized;
    qint32 decoded;
    qint32 average;
    qint32 generation;
  };

  qint32 nextGeneration (qint32 current);
  bool hasUsableSize (qint64 size);
  bool hasShutdownControlSize (qint64 size);
  qint32 state (shared_dec_data_t const& shared);
  qint32 generation (shared_dec_data_t const& shared);
  qint32 progress (shared_dec_data_t const& shared);
  qint32 protocolVersion (shared_dec_data_t const& shared);
  void initialize (shared_dec_data_t& shared);
  void shutdown (shared_dec_data_t& shared);
  void shutdown (decoder_ipc_control_t& control);
  void shutdownControl (void * control);
  bool publish (shared_dec_data_t& shared, dec_data_t const& payload,
                bool copySamples, qint32 generation);
  bool publishFt8Mtd (shared_dec_data_t& shared,
                      Ft8MtdPayload const& payload, qint32 generation);
  bool claim (shared_dec_data_t& shared, qint32& generation);
  bool finish (shared_dec_data_t& shared, qint32 generation);
  bool consume (shared_dec_data_t& shared, qint32 generation);
  bool parseStart (QByteArray line, qint32 * generation);
  bool parseCompletion (QByteArray line, Completion * completion);
}

#endif
