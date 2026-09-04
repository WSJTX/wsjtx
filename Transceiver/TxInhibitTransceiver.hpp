#ifndef TX_INHIBIT_TRANSCEIVER_HPP__
#define TX_INHIBIT_TRANSCEIVER_HPP__

#include <memory>

#include <QElapsedTimer>
#include <QHash>
#include <QString>

#include "Transceiver.hpp"

class QTimer;

class TxInhibitTransceiver final
  : public Transceiver
{
  Q_OBJECT

public:
  static constexpr int maximum_tracked_holds {64};

  explicit TxInhibitTransceiver (logger_type *, std::unique_ptr<Transceiver>);

  void set (TransceiverState const&, unsigned sequence_number) noexcept override;
  void start (unsigned sequence_number) noexcept override;
  void stop () noexcept override;
  void enqueue_jtty_pcm (QByteArray const&, TxAudioQueueEpoch, qint64) noexcept override;
  void clear_jtty_pcm (TxAudioQueueEpoch) noexcept override;

  Q_SLOT void tx_inhibit_command (QString controller, quint32 ttl_ms, QString station);
  Q_SLOT void tx_inhibit_invalid (quint64 count);

  Q_SIGNAL void statusChanged (bool supported, bool inhibited,
                               QString const& holder,
                               quint32 hold_rx, quint32 release_rx,
                               quint32 expiries, quint32 invalid);

private:
  struct Hold
  {
    qint64 expires_at;
    QString holder;
  };

  void apply_requested_state () noexcept;
  void emit_status ();
  void expire_holds ();
  QString holder_summary () const;
  void schedule_expiry ();

  std::unique_ptr<Transceiver> wrapped_;
  QTimer * expiry_timer_;
  QElapsedTimer monotonic_clock_;
  QHash<QString, Hold> holds_;
  TransceiverState requested_;
  TransceiverState last_effective_;
  unsigned sequence_number_ {0};
  quint32 hold_rx_ {0};
  quint32 release_rx_ {0};
  quint32 expiries_ {0};
  quint32 invalid_ {0};
  bool have_requested_state_ {false};
  bool have_effective_state_ {false};
  bool started_ {false};
};

#endif
