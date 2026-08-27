// -*- Mode: C++ -*-
#ifndef JTTY_TX_LOOPBACK_TEST_CONTROLLER_HPP__
#define JTTY_TX_LOOPBACK_TEST_CONTROLLER_HPP__

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

class FixtureSoundOutput;
class MainWindow;

class JttyTxLoopbackTestController final
  : public QObject
{
  Q_OBJECT

public:
  JttyTxLoopbackTestController (MainWindow * window,
                                FixtureSoundOutput * output,
                                QString capturePath,
                                QObject * parent = nullptr);

  void begin ();
  bool succeeded () const {return m_succeeded;}

private:
  static QString contestExchangeMessage ();
  static QString adjacentStructuredFramesMessage ();
  static qint64 encodedSampleFrames (QString const& message);
  void prepareWhenReady ();
  void submitSecondMessage ();
  void maybeFinish ();
  bool validateCapture (QString * error) const;
  void fail (QString const& reason);
  void checkForUnexpectedModal ();

  MainWindow * m_window;
  FixtureSoundOutput * m_output;
  QString m_capturePath;
  QSet<qint64> m_acceptedRequests;
  QSet<qint64> m_completedRequests;
  QVector<qint64> m_acceptedOrder;
  QVector<qint64> m_completedOrder;
  QTimer m_timeout;
  QTimer m_prepareTimer;
  QTimer m_modalTimer;
  qint64 m_firstRequestId {0};
  qint64 m_secondRequestId {0};
  qint64 m_capturedFrames {0};
  qint64 m_expectedAudioFrames {0};
  qint64 m_firstNonSilentFrame {0};
  int m_captureStartCount {0};
  int m_captureStopCount {0};
  int m_sessionDrainCount {0};
  bool m_prepared {false};
  bool m_nonSilentAudioSeen {false};
  bool m_finished {false};
  bool m_succeeded {false};
};

#endif
