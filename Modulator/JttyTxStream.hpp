#ifndef JTTY_TX_STREAM_HPP__
#define JTTY_TX_STREAM_HPP__

#include <atomic>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include "Audio/AudioDevice.hpp"
#include "Modulator/JttyPcmFifo.hpp"

class SoundOutput;

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
  explicit JttyTxStream (QObject * parent = nullptr);

  bool isActive () const {return m_active;}

  // Append a fully rendered message (real PCM samples). Thread-safe; may be
  // called from the GUI thread while readData() runs on the audio thread.
  bool enqueueMessage (QVector<qint16> const& samples, qint64 sessionId);

  // Drop all pending samples and reset counters (abort).
  void clear (qint64 sessionId = 0);

  // Thread-safe progress getters (samples).
  qint64 servedReal () const;
  qint64 totalReal () const;

  Q_SLOT void start (SoundOutput * stream, AudioDevice::Channel channel, qint64 sessionId);
  Q_SLOT void stop ();

  Q_SIGNAL void drained (qint64 sessionId, qint64 totalAtDrain);

protected:
  qint64 readData (char * data, qint64 maxSize) override;
  qint64 writeData (char const * /* data */, qint64 /* maxSize */) override
  {
    return -1;                  // we don't consume data
  }

private:
  Q_SLOT void pollDrain ();

  JttyPcmFifo m_fifo;
  std::atomic<qint64> m_drainGuard;

  QPointer<SoundOutput> m_stream;
  QTimer * m_drainTimer;
  std::atomic<bool> m_active;
};

#endif
