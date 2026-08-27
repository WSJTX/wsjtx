#ifndef TX_AUDIO_QUEUE_HPP_
#define TX_AUDIO_QUEUE_HPP_

#include <memory>

#include <QMetaType>
#include <QVector>
#include <QtGlobal>

struct TxAudioQueueEpoch
{
  constexpr TxAudioQueueEpoch () noexcept : value_ {0} {}
  explicit constexpr TxAudioQueueEpoch (qint64 value) noexcept : value_ {value} {}

  static constexpr TxAudioQueueEpoch invalid () noexcept {return TxAudioQueueEpoch {};}
  constexpr bool isValid () const noexcept {return value_ > 0;}
  constexpr qint64 value () const noexcept {return value_;}

private:
  qint64 value_;
};

inline constexpr bool operator == (TxAudioQueueEpoch lhs,
                                   TxAudioQueueEpoch rhs) noexcept
{
  return lhs.value () == rhs.value ();
}

inline constexpr bool operator != (TxAudioQueueEpoch lhs,
                                   TxAudioQueueEpoch rhs) noexcept
{
  return !(lhs == rhs);
}

struct TxAudioQueueProgress
{
  TxAudioQueueEpoch epoch {};
  qint64 queued_samples {0};
  qint64 served_samples {0};
  qint64 total_samples {0};
};

struct TxAudioQueueDrainState
{
  bool ready {false};
  TxAudioQueueEpoch epoch {};
  qint64 total_at_drain {0};
};

struct TxAudioQueueEnqueueResult
{
  bool accepted {false};
  TxAudioQueueProgress progress {};
};

class TxAudioQueue
{
public:
  // One serialized producer owns clear/enqueue; one consumer owns reset/pull/drain.
  static constexpr qint64 defaultCapacity () noexcept {return 60 * 48000;}

  TxAudioQueue ();
  explicit TxAudioQueue (qint64 capacity_samples);
  ~TxAudioQueue ();

  TxAudioQueue (TxAudioQueue const&) = delete;
  TxAudioQueue& operator = (TxAudioQueue const&) = delete;

  TxAudioQueueEnqueueResult enqueue (QVector<qint16> const& samples,
                                     TxAudioQueueEpoch epoch);
  TxAudioQueueEnqueueResult enqueue (qint16 const * samples, qint64 count,
                                     TxAudioQueueEpoch epoch);
  void clear (TxAudioQueueEpoch epoch);
  void applyPendingReset () noexcept;

  qint16 pullSample (qint64 drain_guard) noexcept;
  TxAudioQueueDrainState takeDrainReady () noexcept;

  // A concurrent clear may produce progress with an invalid epoch.
  TxAudioQueueProgress progress () const noexcept;
  qint64 capacity () const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

Q_DECLARE_METATYPE (TxAudioQueueEpoch)
Q_DECLARE_METATYPE (TxAudioQueueProgress)
Q_DECLARE_METATYPE (TxAudioQueueDrainState)
Q_DECLARE_METATYPE (TxAudioQueueEnqueueResult)

#endif
