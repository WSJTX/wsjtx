#ifndef TX_PLAYBACK_EVIDENCE_HPP_
#define TX_PLAYBACK_EVIDENCE_HPP_

#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include "Audio/TxIdentity.hpp"

namespace TxEvidence
{
  inline qint64 boundedCommittedEndSample (qint64 leadingSilentFrames,
                                            qint64 firstSourceSample,
                                            qint64 lastSourceSample) noexcept
  {
    if (leadingSilentFrames < 0 || firstSourceSample < 0 ||
        lastSourceSample < firstSourceSample)
      {
        return -1;
      }
    return leadingSilentFrames + lastSourceSample - firstSourceSample;
  }

  inline qint64 interleavedFrameCount (qint64 scalarCount,
                                       int channelCount) noexcept
  {
    if (scalarCount < 0 || channelCount <= 0 || scalarCount % channelCount)
      {
        return -1;
      }
    return scalarCount / channelCount;
  }

  enum class TxPlayoutTier
  {
    Unavailable,
    DeadReckoning,
    DeviceClock
  };

  enum class TxBackendState
  {
    Unavailable,
    Active,
    Idle,
    Suspended,
    Stopped,
    Interrupted,
    Unknown
  };

  enum class TxBackendError
  {
    None,
    Open,
    Io,
    Underrun,
    Fatal,
    Unknown
  };

  enum class TxStopReason
  {
    NormalEnd,
    UserHalt,
    Watchdog,
    Error,
    ModeChange
  };

  struct TxStartSnapshot
  {
    TxSessionId session_id;
    TxGeneration generation;
    QString mode;
    int sample_rate_hz {0};
    qint64 committed_end_sample {-1};
    bool target_known {false};
    QString diagnostic;
  };

  struct TxRawPlayoutSnapshot
  {
    TxPlayoutTier tier {TxPlayoutTier::Unavailable};
    qint64 backend_start_sequence {0};
    bool start_event {false};
    bool available {false};
    TxBackendState state {TxBackendState::Unavailable};
    TxBackendError error {TxBackendError::None};
    qint64 available_bytes {-1};
    qint64 buffered_bytes {-1};
    qint64 capacity_bytes {-1};
    qint64 processed_usecs {-1};
    qint64 elapsed_usecs {-1};
    qint64 successfully_written_frames {-1};
    qint64 source_served_frames {-1};
    qint64 source_total_frames {-1};
    int channel_count {0};
    int bytes_per_frame {0};
    int sample_rate_hz {0};
    int report_interval_ms {0};
    QString diagnostic;
  };

  struct TxPlayoutSnapshot
  {
    TxSessionId session_id;
    TxGeneration generation;
    TxRawPlayoutSnapshot raw;
    qint64 consumed_samples {-1};
    qint64 uncertainty_samples {-1};
  };

  struct TxStopDecision
  {
    int tail_ms {0};
    TxStopReason reason {TxStopReason::NormalEnd};
  };

  struct TxTerminalSession
  {
    TxSessionId session_id;
    TxGeneration generation;
    QString mode;
    TxPlayoutTier tier {TxPlayoutTier::Unavailable};
    bool target_known {false};
    qint64 committed_end_sample {-1};
    qint64 source_served_samples {-1};
    qint64 consumed_samples {-1};
    qint64 uncertainty_samples {-1};
    bool underrun {false};
    bool truncated_known {false};
    bool truncated {false};
    bool terminal_playout_observed {false};
    qint64 backend_start_sequence {0};
    TxStopDecision stop;
    QString diagnostic;
  };
}

Q_DECLARE_METATYPE (TxEvidence::TxStartSnapshot)
Q_DECLARE_METATYPE (TxEvidence::TxRawPlayoutSnapshot)

#endif
