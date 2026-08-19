#ifndef JTTY_TX_STREAM_HPP__
#define JTTY_TX_STREAM_HPP__

#include <atomic>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include "Audio/AudioDevice.hpp"
#include "Audio/TxAudioQueue.hpp"
#include "Audio/TxPlaybackEvidence.hpp"
#include "Audio/TxRequest.hpp"

class SoundOutput;

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

  Q_SLOT void start (TxEvidence::TxRequest request, SoundOutput * stream);
  Q_SLOT void stop ();

  Q_SIGNAL void drained (TxAudioQueueDrainState drain);
  Q_SIGNAL void txSourceCommitted (TxEvidence::TxStartSnapshot snapshot);

protected:
  qint64 readData (char * data, qint64 maxSize) override;
  qint64 writeData (char const * /* data */, qint64 /* maxSize */) override
  {
    return -1;                  // we don't consume data
  }

private:
  Q_SLOT void pollDrain ();

  TxAudioQueue& m_queue;
  std::atomic<qint64> m_drainGuard;

  QPointer<SoundOutput> m_stream;
  QTimer * m_drainTimer;
  std::atomic<bool> m_active;
};

#endif
