#ifndef JTTY_TX_STREAM_HPP__
#define JTTY_TX_STREAM_HPP__

#include <atomic>
#include <memory>
#include <QByteArray>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include "Audio/AudioDevice.hpp"
#include "Audio/TxAudioQueue.hpp"
#include "Audio/TxPlaybackEvidence.hpp"
#include "Audio/TxRequest.hpp"

class AudioOutputSink;

TxEvidence::TxStartSnapshot makeJttyTxStartSnapshot (
  TxEvidence::TxRequest const& request, qint64 committedEndSample);

//
// JTTY-only asynchronous transmit source.
//
// A QIODevice adapter around the shared JTTY PCM FIFO. readData() only serves
// samples and records drain readiness; drained() is emitted by a timer outside
// the audio pull path.
//
class JttyTxStream
  : public AudioDevice
{
  Q_OBJECT;

public:
  explicit JttyTxStream (TxAudioQueue& queue, QObject * parent = nullptr);

  bool isActive () const {return m_active;}

  Q_SLOT void start (TxEvidence::TxRequest request, AudioOutputSink * stream);
  Q_SLOT void stop ();
  Q_SLOT void clearQueue (TxAudioQueueEpoch epoch);
  // Reserve queued payload capacity before posting to the audio thread.
  void queuePcm (QByteArray const& samples, TxAudioQueueEpoch epoch,
                 qint64 enqueueId);
  Q_SLOT void enqueuePcm (QByteArray const& samples, TxAudioQueueEpoch epoch,
                          qint64 enqueueId);

  Q_SIGNAL void drained (TxAudioQueueDrainState drain);
  Q_SIGNAL void progress (TxAudioQueueProgress progress);
  Q_SIGNAL void stopped ();
  Q_SIGNAL void txSourceCommitted (TxEvidence::TxStartSnapshot snapshot);
  Q_SIGNAL void enqueueAccepted (qint64 enqueueId, qint64 sampleCount,
                                 TxAudioQueueProgress progress);
  Q_SIGNAL void enqueueFailed (TxAudioQueueEpoch epoch, qint64 enqueueId,
                               TxAudioQueueEnqueueFailure failure);

protected:
  qint64 readData (char * data, qint64 maxSize) override;
  qint64 writeData (char const * /* data */, qint64 /* maxSize */) override
  {
    return -1;                  // we don't consume data
  }

private:
  struct PendingEnqueues;
  std::shared_ptr<PendingEnqueues> m_pendingEnqueues;
  Q_SLOT void pollDrain ();

  TxAudioQueue& m_queue;
  std::atomic<qint64> m_drainGuard;

  QPointer<AudioOutputSink> m_stream;
  QTimer * m_drainTimer;
  std::atomic<bool> m_active;
  TxAudioQueueProgress m_lastProgress;
};

#endif
