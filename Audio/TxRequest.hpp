#ifndef TX_REQUEST_HPP_
#define TX_REQUEST_HPP_

#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "AudioDevice.hpp"
#include "TxAudioQueue.hpp"
#include "TxIdentity.hpp"

namespace TxEvidence
{
  struct TxRequest
  {
    QString mode {"FT8"};
    unsigned symbols_length {79};
    double frames_per_symbol {1920.0};
    double frequency_hz {1500.0};
    double tone_spacing {-3.0};
    AudioDevice::Channel channel {AudioDevice::Mono};
    bool synchronize {true};
    bool fast_mode {false};
    double snr_db {99.0};
    double tr_period_s {60.0};
    TxSessionId session_id {};
    TxGeneration generation {};
    TxAudioQueueEpoch queue_epoch {};
    bool tuning {false};
    QVector<int> cw_id;
    qint64 start_window_open_ms {-1};
    qint64 start_window_close_ms {-1};
  };

  inline bool operator == (TxRequest const& lhs, TxRequest const& rhs)
  {
    return lhs.mode == rhs.mode
      && lhs.symbols_length == rhs.symbols_length
      && lhs.frames_per_symbol == rhs.frames_per_symbol
      && lhs.frequency_hz == rhs.frequency_hz
      && lhs.tone_spacing == rhs.tone_spacing
      && lhs.channel == rhs.channel
      && lhs.synchronize == rhs.synchronize
      && lhs.fast_mode == rhs.fast_mode
      && lhs.snr_db == rhs.snr_db
      && lhs.tr_period_s == rhs.tr_period_s
      && lhs.session_id == rhs.session_id
      && lhs.generation == rhs.generation
      && lhs.queue_epoch == rhs.queue_epoch
      && lhs.tuning == rhs.tuning
      && lhs.cw_id == rhs.cw_id
      && lhs.start_window_open_ms == rhs.start_window_open_ms
      && lhs.start_window_close_ms == rhs.start_window_close_ms;
  }

  inline bool operator != (TxRequest const& lhs, TxRequest const& rhs)
  {
    return !(lhs == rhs);
  }
}

Q_DECLARE_METATYPE (TxEvidence::TxRequest)

namespace TxEvidence
{
  inline int register_tx_request_type ()
  {
    return qRegisterMetaType<TxRequest> ("TxEvidence::TxRequest");
  }
}

#endif
