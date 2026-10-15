#include "mainwindow.h"
#include "MessageBox.hpp"
#include "TxOutputFaultPolicy.hpp"
#include <QProcess>
#include <QStringList>
#include <QSplashScreen>
#include <QStatusBar>
#include <QTimer>

void MainWindow::showSoundInError(const QString& errorMsg)
{
  if (m_splash && m_splash->isVisible ()) m_splash->hide ();
  MessageBox::critical_message (this, tr ("Error in Sound Input"), errorMsg);
}

void MainWindow::showSoundOutError(TxEvidence::TxOutputError const& error)
{
  TxEvidence::TxPlaybackContext active;
  if (m_txEvidenceSession == m_txEvidenceSourceSession)
    active = {m_txEvidenceSession, m_txEvidenceGeneration};
  bool const jtty = m_jttyTxLifecycle.active () || !m_jttyTransmitQueue.empty ();
  auto const action = TxOutputFaultPolicy::decide (error, active, jtty);
  if (action == TxOutputFaultPolicy::Action::Ignore) return;

  auto const& errorMsg = error.message;
  noteTxStopReason (TxEvidence::TxStopReason::Error);
  m_config.invalidate_audio_output_device (errorMsg);
  if (action == TxOutputFaultPolicy::Action::Stop)
    requestTxStop (TxEvidence::TxStopReason::Error);
  if (jtty) {
    m_jttyQueueNotice = errorMsg;
    updateJttySendButton ();
  }
  if (m_splash && m_splash->isVisible ()) m_splash->hide ();
  MessageBox::critical_message (this, tr ("Error in Sound Output"), errorMsg);
}

void MainWindow::showStatusMessage(const QString& statusMsg)
{
  statusBar()->showMessage(statusMsg, 5000);
}

bool MainWindow::subProcessFailed (QProcess * process, int exit_code, QProcess::ExitStatus status)
{
  if (m_valid && (exit_code || QProcess::NormalExit != status))
    {
      QStringList arguments;
      for (auto argument: process->arguments ())
        {
          if (argument.contains (' ')) argument = '"' + argument + '"';
          arguments << argument;
        }
      if (m_splash && m_splash->isVisible ()) m_splash->hide ();
      MessageBox::critical_message (this, tr ("Subprocess Error")
                                    , tr ("Subprocess failed with exit code %1")
                                    .arg (exit_code)
                                    , tr ("Running: %1\n%2")
                                    .arg (process->program () + ' ' + arguments.join (' '))
                                    .arg (QString {process->readAllStandardError()}));
      return true;
    }
  return false;
}

void MainWindow::subProcessError (QProcess * process, QProcess::ProcessError)
{
  if (m_valid)
    {
      QStringList arguments;
      for (auto argument: process->arguments ())
        {
          if (argument.contains (' ')) argument = '"' + argument + '"';
          arguments << argument;
        }
      if (m_splash && m_splash->isVisible ()) m_splash->hide ();
      MessageBox::critical_message (this, tr ("Subprocess error")
                                    , tr ("Running: %1\n%2")
                                    .arg (process->program () + ' ' + arguments.join (' '))
                                    .arg (process->errorString ()));
      m_valid = false;              // ensures exit if still constructing
      QTimer::singleShot (0, this, SLOT (close ()));
    }
}
