// -*- Mode: C++ -*-
#ifndef AUDIO_AUDIOOUTPUTSINK_HPP_
#define AUDIO_AUDIOOUTPUTSINK_HPP_

#include <QObject>
#include <QString>
#include <QtGlobal>

#include "Audio/TxPlaybackEvidence.hpp"

class QAudioDeviceInfo;
class QIODevice;

class AudioOutputSink
  : public QObject
{
  Q_OBJECT

public:
  explicit AudioOutputSink (QObject * parent = nullptr)
    : QObject {parent}
  {
  }

  ~AudioOutputSink () override = default;

  Q_SLOT virtual void setFormat (QAudioDeviceInfo const&, unsigned channels,
                                  int frames_buffered = 0) = 0;
  Q_SLOT virtual void restart (QIODevice *, TxEvidence::TxPlaybackContext,
                              qint64 period_offset_ms = -1) = 0;
  Q_SLOT virtual void suspend () = 0;
  Q_SLOT virtual void resume () = 0;
  Q_SLOT virtual void reset () = 0;
  Q_SLOT virtual void stop () = 0;
  Q_SLOT virtual void setAttenuation (qreal) = 0;
  Q_SLOT virtual void resetAttenuation () = 0;

  virtual int bufferSize () const = 0;

  // Bytes buffered for output but not yet played, estimated from device
  // capacity when the sink cannot report a more accurate value.
  virtual int unplayedBufferBytes () const { return bufferSize (); }

  Q_SIGNAL void error (TxEvidence::TxOutputError error) const;
  Q_SIGNAL void status (QString message) const;
  Q_SIGNAL void audioOutputActive () const;
  Q_SIGNAL void audioOutputIdle () const;
  Q_SIGNAL void rawTxPlayoutSnapshot (
    TxEvidence::TxRawPlayoutSnapshot snapshot) const;
};

#endif
