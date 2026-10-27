// SPDX-License-Identifier: GPL-3.0-or-later
#include "DecoderProcessSupervisor.hpp"

#include <utility>

#include <QIODevice>

DecoderProcessSupervisor::DecoderProcessSupervisor (Transport& transport,
                                                    DecoderDiagnostics::LineSink sink,
                                                    QObject * parent)
  : QObject {parent}
  , m_transport {transport}
  , m_diagnostics {std::move (sink)}
{
  connect (&m_process, &QProcess::started, this, &DecoderProcessSupervisor::processStarted);
  connect (&m_process, &QProcess::readyReadStandardOutput,
           this, &DecoderProcessSupervisor::outputAvailable);
  connect (&m_process, &QProcess::readyReadStandardError, this, [this] {
      drainDiagnostics (false);
    });
#if QT_VERSION < QT_VERSION_CHECK (5, 6, 0)
  connect (&m_process, static_cast<void (QProcess::*) (QProcess::ProcessError)> (&QProcess::error),
           this, [this] (QProcess::ProcessError error) {processError (error);});
#else
  connect (&m_process, &QProcess::errorOccurred,
           this, [this] (QProcess::ProcessError error) {processError (error);});
#endif
  connect (&m_process, static_cast<void (QProcess::*) (int, QProcess::ExitStatus)> (&QProcess::finished),
           this, [this] (int exitCode, QProcess::ExitStatus status) {
             processFinished (exitCode, status);
           });

  m_shutdownTimer.setSingleShot (true);
  connect (&m_shutdownTimer, &QTimer::timeout, this, [this] {
      if (Phase::StopRequested != m_phase
          || QProcess::NotRunning == m_process.state ()) return;
      m_phase = Phase::Terminating;
      m_process.terminate ();
      m_terminateTimer.start (1000);
    });
  m_terminateTimer.setSingleShot (true);
  connect (&m_terminateTimer, &QTimer::timeout, this, [this] {
      if (Phase::Terminating != m_phase
          || QProcess::NotRunning == m_process.state ()) return;
      m_phase = Phase::Killing;
      m_process.kill ();
      m_killTimer.start (2000);
    });
  m_killTimer.setSingleShot (true);
  connect (&m_killTimer, &QTimer::timeout, this, [this] {
      if (Phase::Killing != m_phase
          || QProcess::NotRunning == m_process.state ()) return;
      Q_EMIT failed (failure (DecoderBackend::Failure::Kind::StopFailed));
    });
  m_startTimer.setSingleShot (true);
  connect (&m_startTimer, &QTimer::timeout, this, [this] {
      if (!starting ()) return;
      // A GUI stall can delay stdout notification past the handshake deadline.
      m_process.waitForReadyRead (0);
      if (m_process.bytesAvailable ()) Q_EMIT outputAvailable ();
      if (!starting ()) return;
      failStartup (DecoderBackend::Failure::Kind::StartTimeout);
    });
}

DecoderProcessSupervisor::~DecoderProcessSupervisor ()
{
  // The process is destroyed after the members its handlers use.
  disconnect (&m_process, nullptr, this, nullptr);
}

void DecoderProcessSupervisor::start ()
{
  launch ();
}

void DecoderProcessSupervisor::launch ()
{
  m_phase = restartInProgress () ? Phase::ReplacementStarting : Phase::InitialStarting;
  m_transport.prepareStart ();
  m_diagnostics.reset ();
  auto const command = m_transport.command ();
  m_process.setProcessEnvironment (command.environment);
  m_process.start (command.program, command.arguments,
                   QIODevice::ReadWrite | QIODevice::Unbuffered);
  m_startTimer.start (5000);
  Q_EMIT launched ();
}

void DecoderProcessSupervisor::restart ()
{
  m_phase = Phase::StopRequested;
  Q_EMIT restartInitiated ();
  m_transport.requestStop ();

  if (QProcess::NotRunning == m_process.state ())
    {
      if (!m_transport.prepareReplacement ())
        {
          Q_EMIT failed (failure (DecoderBackend::Failure::Kind::StateUnavailable));
          return;
        }
      launch ();
      return;
    }
  m_shutdownTimer.start (250);
}

void DecoderProcessSupervisor::markReady ()
{
  m_startTimer.stop ();
  m_phase = Phase::Ready;
  Q_EMIT becameReady ();
}

void DecoderProcessSupervisor::failStartup (DecoderBackend::Failure::Kind kind,
                                            QString const& reason, int code)
{
  if (stopped ()) return;
  m_startTimer.stop ();
  m_phase = Phase::Failed;
  m_transport.startupFailed ();
  Q_EMIT failed (failure (kind, reason, code));
}

void DecoderProcessSupervisor::prepareForClose ()
{
  m_phase = Phase::Closing;
  m_shutdownTimer.stop ();
  m_terminateTimer.stop ();
  m_killTimer.stop ();
  m_startTimer.stop ();
}

void DecoderProcessSupervisor::shutdownAtClose ()
{
  m_transport.requestStop ();
  if (m_process.state () != QProcess::NotRunning) {
    if (!m_process.waitForFinished (5000)) {
      m_process.terminate ();
      if (!m_process.waitForFinished (1000)) {
        m_process.kill ();
        m_process.waitForFinished (1000);
      }
    }
  }
  m_process.close ();
}

bool DecoderProcessSupervisor::starting () const
{
  return Phase::InitialStarting == m_phase || Phase::ReplacementStarting == m_phase;
}

bool DecoderProcessSupervisor::restartInProgress () const
{
  return Phase::StopRequested == m_phase
    || Phase::Terminating == m_phase
    || Phase::Killing == m_phase
    || Phase::ReplacementStarting == m_phase;
}

bool DecoderProcessSupervisor::stopped () const
{
  return Phase::Failed == m_phase || Phase::Closing == m_phase;
}

bool DecoderProcessSupervisor::running () const
{
  return QProcess::Running == m_process.state ();
}

QIODevice& DecoderProcessSupervisor::channel ()
{
  return m_process;
}

QIODevice const& DecoderProcessSupervisor::channel () const
{
  return m_process;
}

QString DecoderProcessSupervisor::failureDetail (QString const& reason)
{
  drainDiagnostics (true);
  auto const& diagnostic = m_diagnostics.tail ();
  return diagnostic.isEmpty () ? reason : reason + "\n\n" + QString::fromLocal8Bit (diagnostic);
}

QByteArray DecoderProcessSupervisor::diagnosticsTail ()
{
  drainDiagnostics (true);
  return m_diagnostics.tail ();
}

void DecoderProcessSupervisor::processError (QProcess::ProcessError error)
{
  if (stopped ()) return;
  if (starting ())
    {
      failStartup (DecoderBackend::Failure::Kind::StartError, m_process.errorString ());
      return;
    }
  if ((Phase::StopRequested == m_phase
       || Phase::Terminating == m_phase
       || Phase::Killing == m_phase)
      && QProcess::Crashed == error) return;
  Q_EMIT failed (failure (DecoderBackend::Failure::Kind::ProcessError, m_process.errorString ()));
}

void DecoderProcessSupervisor::processFinished (int exitCode, QProcess::ExitStatus status)
{
  drainDiagnostics (true);
  if (stopped ()) return;
  if (starting ())
    {
      Q_EMIT outputAvailable ();
      failStartup (DecoderBackend::Failure::Kind::StartExit, {}, exitCode);
      return;
    }
  if (restartInProgress ())
    {
      m_shutdownTimer.stop ();
      m_terminateTimer.stop ();
      m_killTimer.stop ();
      m_startTimer.stop ();
      m_process.readAllStandardOutput ();
      if (!m_transport.prepareReplacement ())
        {
          Q_EMIT failed (failure (DecoderBackend::Failure::Kind::StateUnavailable));
          return;
        }
      launch ();
      return;
    }
  Q_EMIT failed (failure (DecoderBackend::Failure::Kind::Exit, {}, exitCode,
                          QProcess::NormalExit != status));
}

void DecoderProcessSupervisor::drainDiagnostics (bool final)
{
  m_diagnostics.append (m_process.readAllStandardError ());
  if (final) m_diagnostics.finish ();
}

DecoderBackend::Failure DecoderProcessSupervisor::failure (DecoderBackend::Failure::Kind kind,
                                                           QString const& reason,
                                                           int code, bool crashed) const
{
  return {kind, reason, code, crashed, m_process.program (), m_process.arguments ()};
}
