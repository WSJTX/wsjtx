// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DECODER_PROCESS_SUPERVISOR_HPP
#define DECODER_PROCESS_SUPERVISOR_HPP

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "DecoderBackend.hpp"
#include "DecoderDiagnostics.hpp"
#include "NonInheritingProcess.hpp"

class QIODevice;

// Runs one decoder process for a transport: launch, startup deadline,
// restart escalation, failure reports and the blocking stop at close.
class DecoderProcessSupervisor
  : public QObject
{
  Q_OBJECT

public:
  enum class Phase
  {
    InitialStarting,
    Ready,
    StopRequested,
    Terminating,
    Killing,
    ReplacementStarting,
    Failed,
    Closing
  };

  struct Command
  {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
  };

  class Transport
  {
  public:
    virtual ~Transport () = default;
    virtual Command command () = 0;
    virtual void prepareStart () = 0;
    // Ask the decoder to exit; the supervisor escalates if it does not.
    virtual void requestStop () = 0;
    virtual bool prepareReplacement () = 0;
    virtual void startupFailed () = 0;
  };

  explicit DecoderProcessSupervisor (Transport&,
                                     DecoderDiagnostics::LineSink = logDecoderDiagnostic,
                                     QObject * parent = nullptr);
  ~DecoderProcessSupervisor ();

  void start ();
  void restart ();
  void markReady ();
  void failStartup (DecoderBackend::Failure::Kind, QString const& reason = {}, int code = 0);
  void prepareForClose ();
  void shutdownAtClose ();

  Phase phase () const {return m_phase;}
  bool starting () const;
  bool ready () const {return Phase::Ready == m_phase;}
  bool restartInProgress () const;
  bool stopped () const;
  bool running () const;
  QIODevice& channel ();
  QIODevice const& channel () const;
  QString failureDetail (QString const& reason);
  QByteArray diagnosticsTail ();

Q_SIGNALS:
  void processStarted ();
  void launched ();
  void becameReady ();
  void restartInitiated ();
  void outputAvailable ();
  void failed (DecoderBackend::Failure const& failure);

private:
  void launch ();
  void processError (QProcess::ProcessError);
  void processFinished (int exitCode, QProcess::ExitStatus);
  void drainDiagnostics (bool final);
  DecoderBackend::Failure failure (DecoderBackend::Failure::Kind, QString const& reason = {},
                                   int code = 0, bool crashed = false) const;

  Transport& m_transport;
  NonInheritingProcess m_process;
  DecoderDiagnostics m_diagnostics;
  Phase m_phase {Phase::InitialStarting};
  QTimer m_shutdownTimer;
  QTimer m_terminateTimer;
  QTimer m_killTimer;
  QTimer m_startTimer;
};

#endif
