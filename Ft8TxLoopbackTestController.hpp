// -*- Mode: C++ -*-
#ifndef FT8_TX_LOOPBACK_TEST_CONTROLLER_HPP__
#define FT8_TX_LOOPBACK_TEST_CONTROLLER_HPP__

#include <QObject>
#include <QString>
#include <QTimer>

class FixtureSoundOutput;
class MainWindow;

class Ft8TxLoopbackTestController final
  : public QObject
{
  Q_OBJECT

public:
  Ft8TxLoopbackTestController (MainWindow * window,
                               FixtureSoundOutput * output,
                               QString capturePath,
                               QObject * parent = nullptr);

  void begin ();
  bool succeeded () const {return m_succeeded;}

private:
  void prepareWhenReady ();
  void startWhenScheduled ();
  void maybeFinish ();
  bool validateCapture (QString * error) const;
  void fail (QString const& reason);
  void checkForUnexpectedModal ();

  MainWindow * m_window;
  FixtureSoundOutput * m_output;
  QString m_capturePath;
  QTimer m_timeout;
  QTimer m_prepareTimer;
  QTimer m_startTimer;
  QTimer m_modalTimer;
  qint64 m_targetPeriodStartMs {0};
  qint64 m_latestStartMs {0};
  qint64 m_startCallbackMs {-1};
  qint64 m_startCompletedMs {-1};
  qint64 m_firstNonSilentFrame {-1};
  qint64 m_capturedFrames {0};
  int m_captureStartCount {0};
  int m_captureStopCount {0};
  int m_startAttemptCount {0};
  bool m_prepared {false};
  bool m_autoDisabledDuringTransmit {false};
  bool m_finished {false};
  bool m_succeeded {false};
};

#endif
