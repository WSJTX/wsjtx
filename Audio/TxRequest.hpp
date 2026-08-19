#ifndef TX_REQUEST_HPP_
#define TX_REQUEST_HPP_

#include <QMetaType>
#include <QString>
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
  };
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
