// -*- Mode: C++ -*-
#ifndef SOUNDOUT_H__
#define SOUNDOUT_H__

#include <QObject>
#include <QString>
#include <QAudioOutput>
#include <QAudioDeviceInfo>
#include <QOperatingSystemVersion>

#include "Audio/TxPlaybackEvidence.hpp"

class QIODevice;
class QAudioDeviceInfo;

// An instance of this sends audio data to a specified soundcard.

class SoundOutput
  : public QObject
{
  Q_OBJECT;
  
public:
  SoundOutput ()
    : m_framesBuffered {0}
    , m_volume {1.0}
    , error_ {false}
    , m_backendStartSequence {0}
  {
  }

  qreal attenuation () const;

  // Device buffer size in bytes, or 0 when no stream is active. Call only on the
  // audio thread (this object's thread); QAudioOutput is not cross-thread safe.
  int bufferSize () const;

public Q_SLOTS:
  void setFormat (QAudioDeviceInfo const& device, unsigned channels, int frames_buffered = 0);
  void restart (QIODevice *);
  void suspend ();
  void resume ();
  void reset ();
  void stop ();
  void setAttenuation (qreal);	/* unsigned */
  void resetAttenuation ();	/* to zero */
  
Q_SIGNALS:
  void error (QString message) const;
  void status (QString message) const;
  void audioOutputActive () const;
  void audioOutputIdle () const;
  void rawTxPlayoutSnapshot (TxEvidence::TxRawPlayoutSnapshot snapshot) const;

private:
  bool checkStream () const;
  TxEvidence::TxRawPlayoutSnapshot makeRawTxPlayoutSnapshot (bool startEvent = false) const;
  void publishRawTxPlayoutSnapshot (bool startEvent = false) const;
  void publishUnavailableTxPlayoutSnapshot (bool startEvent, QString const& diagnostic) const;

private Q_SLOTS:
  void handleStateChanged (QAudio::State);

private:
  QAudioDeviceInfo m_device;
  unsigned m_channels;
  QScopedPointer<QAudioOutput> m_stream;
  int m_framesBuffered;
  qreal m_volume;
  bool error_;
  qint64 m_backendStartSequence;
};

#endif
