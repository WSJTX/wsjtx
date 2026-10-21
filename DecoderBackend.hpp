// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DECODER_BACKEND_HPP
#define DECODER_BACKEND_HPP

#include <functional>
#include <memory>

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include "CapturedDecode.hpp"
#include "DecoderParams.h"

// The decoder as MainWindow sees it. MainWindow decides what results and
// failures mean; a backend runs the decoder, takes its input and requests, and
// reports what it does.
//
// Every backend keeps these contracts:
// - Members are called, and signals emitted, on the thread that owns the
//   backend.
// - outputAvailable () is answered by calling deliverOutput () before control
//   returns to the event loop; the startup handshake, and the failures it
//   reveals, are recognised inside deliverOutput (). Deadlines and process
//   errors arrive from the backend's own timers and process signals.
// - The OutputHandler may call restart () or abandonDecode (), and may run a
//   nested event loop (a modal dialog) that calls deliverOutput () again. A
//   backend that emits failed () from inside deliverOutput () enters its
//   terminal state first, because MainWindow may answer it with a dialog.
class DecoderBackend
  : public QObject
{
  Q_OBJECT

public:
  struct LaunchSettings
  {
    QString programDirectory;
    QString writeableDataDirectory;
    QString temporaryDirectory;
    QString dataDirectory;
    QProcessEnvironment environment;
  };

  enum class Audio
  {
    Current,                    // the current input as received so far
    Retained                    // the audio of the last completed decode, again
  };

  struct RetainedAudio
  {
    bool available {false};
    int mode {0};               // DecoderEngine.h DECODER_MODE_* (66 Q65, 240 FST4, 241 FST4W)
    int period {0};             // TR period, seconds
    bool fromCurrentInput {false}; // no beginInput () since it was received
  };

  struct Counts
  {
    qint32 synchronized {0};
    qint32 decoded {0};
    qint32 average {0};         // Q65: 1000 * even-period average count + odd-period count
  };

  // MainWindow drops Records that are not current, quarantines a CycleStarted
  // that is not current (restarting the decoder if it owns a decode), ends its
  // decode on a current CycleFinished, restarts the decoder on CycleLost and
  // MalformedBoundary, logs Rejected and Unframed, and counts Unframed as
  // progress.
  struct Output
  {
    enum class Kind
    {
      Record,                   // a line inside a cycle, decode or not, as jt9 writes it, with its newline
      CycleStarted,             // possibly started by the decoder itself
      CycleFinished,
      CycleLost,                // finished, but its result could not be taken
      Rejected,                 // not terminal: the backend still ends the cycle
      Unframed,                 // text outside any cycle, or an unreadable control line inside one
      MalformedBoundary         // a cycle start or finish that is unreadable or out of order;
                                // never a valid event MainWindow does not know
    };
    Kind kind {Kind::Unframed};
    bool current {false};       // belongs to a cycle the client has not abandoned
    qint64 request {0};         // the backend's id for the cycle, for logs
    qint32 period {0};          // the period's UTC label as decoded (HHMM or HHMMSS); 0 when unknown
    QByteArray text;
    Counts counts;
  };
  using OutputHandler = std::function<void (Output const&)>;

  struct Failure
  {
    enum class Kind
    {
      StartError,               // could not start, or failed while starting; reason: the system error
      StartExit,                // exited before its handshake; code: exit code
      StartTimeout,             // no handshake within five seconds
      HandshakeInvalid,         // a handshake at the wrong time or for another protocol; code: protocol version
      HandshakeRefused,         // the decoder refused its startup contract; reason: its status
      HandshakeMalformed,       // an unreadable handshake, or a cycle before it
      ProcessError,             // a process error after startup; reason: the system error
      Exit,                     // an unexpected exit after startup; code: exit code
      StopFailed,               // still running two seconds after the kill during a restart
      StateUnavailable,         // the decoder's state could not be prepared for a replacement
      Unresponsive              // no progress on a cycle within the backend's deadline
    };
    Kind kind {Kind::ProcessError};
    QString reason;
    int code {0};
    bool crashed {false};       // Exit only: the process did not exit normally
    QString program;
    QStringList arguments;
  };

  explicit DecoderBackend (QObject * parent = nullptr)
    : QObject {parent}
  {
  }

  // When true the decoder decides when to decode from the audio and settings it
  // receives: the client does not call submit (Audio::Current) at its own stop
  // points nor capture (), and applies no busy flag of its own.
  virtual bool schedulesDecodes () const = 0;

  virtual void start (LaunchSettings const&) = 0;
  virtual void restart () = 0;
  virtual void prepareForClose () = 0;
  virtual void shutdownAtClose () = 0;

  virtual bool ready () const = 0;
  virtual bool running () const = 0;
  bool available () const {return ready () && running ();}
  virtual bool restartInProgress () const = 0;

  // The receiver restarted its input: a new period, device or file. This
  // re-anchors the period phase; decodes already due or deferred still run,
  // and a period that has not reached its stop is dropped.
  virtual void beginInput () = 0;
  // samples[0, end) of the current input are final, after any reference
  // filtering. The pointer is borrowed for the call. MainWindow calls it in
  // every mode except WSPR, JTTY, MSK144, fast JT9, Echo and FreqCal.
  virtual void acceptAudio (short const * samples, int end) = 0;
  // Settings in force for audio accepted after the call. A backend that
  // schedules its own decodes needs them before the first audio after
  // becameReady (), on every change, and with the period's label and
  // per-period state before the period's first sample; MainWindow keeps that
  // timing once such a backend exists. Until then it calls applySettings ()
  // once per request, in decode (), before submit (). kin, nzhsym, npts8,
  // newdat, nagain, nagainfil and ndiskdat describe one request, not a
  // setting; nclearave, lmodechanged, lbandchanged and nsecbandchanged are
  // edges that apply to the next decode only.
  virtual void applySettings (decoder_params_t const&) = 0;

  // Decode now: the current input as received so far, or the retained audio.
  virtual bool submit (Audio) = 0;
  // Never null. Only for FT8 with the multithreaded decoder; the capture stays
  // valid across restart () of the backend that made it.
  virtual std::unique_ptr<CapturedDecode> capture () = 0;
  virtual bool submit (CapturedDecode const&) = 0;
  // Forget the active request and the retained audio; output still arriving
  // for the request is no longer current.
  virtual void abandonDecode () = 0;
  // Only while no request is active.
  virtual void discardRetainedAudio () = 0;
  virtual RetainedAudio retainedAudio () const = 0;
  // Zero when no request is active.
  virtual qint64 activeRequest () const = 0;

  virtual bool hasPendingOutput () const = 0;
  virtual void deliverOutput (OutputHandler const&) = 0;
  // False when the decoder reports no progress for the active request.
  virtual bool activeProgress (qint32& progress) const = 0;

  // The reason followed by the decoder's recent diagnostic output.
  virtual QString failureDetail (QString const& reason) = 0;
  virtual QByteArray diagnosticsTail () = 0;
  virtual QString diagnostics () const = 0;

Q_SIGNALS:
  void processStarted ();
  void launched ();
  void becameReady ();
  // Emitted before the decoder is stopped, so the active decode can be aborted.
  void restartInitiated ();
  void outputAvailable ();
  void failed (DecoderBackend::Failure const& failure);
};

Q_DECLARE_METATYPE (DecoderBackend::Failure)

#endif
