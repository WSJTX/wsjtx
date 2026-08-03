// -*- Mode: C++ -*-
#ifndef LIVE_AUDIO_TEST_CONTROLLER_HPP__
#define LIVE_AUDIO_TEST_CONTROLLER_HPP__

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

class FixtureAudioInput;
class MainWindow;

class LiveAudioTestController final
  : public QObject
{
  Q_OBJECT

public:
  LiveAudioTestController (MainWindow * window, FixtureAudioInput * fixture,
                           QString expectedPath, QObject * parent = nullptr);

  void begin ();
  bool succeeded () const {return m_succeeded;}

private:
  enum class DecoderStage
  {
    None,
    EarlyStandard,
    Multithreaded
  };

  static QSet<QString> readExpectedMessages (QString const& path, QString * error);
  static QString messageFromDecoderLine (QByteArray const& line);
  void prepareWhenReady ();
  void maybeFinish ();
  void fail (QString const& reason);
  void checkForUnexpectedModal ();

  MainWindow * m_window;
  FixtureAudioInput * m_fixture;
  QString m_expectedPath;
  QSet<QString> m_expected;
  QSet<QString> m_observed;
  QSet<QString> m_displayed;
  QSet<QString> m_earlyObserved;
  QSet<QString> m_multithreadedObserved;
  QSet<QString> m_earlyRaw;
  QSet<QString> m_multithreadedRaw;
  QString m_initializationError;
  QTimer m_timeout;
  QTimer m_prepareTimer;
  QTimer m_modalTimer;
  qint64 m_emittedFrames {0};
  int m_completedCycles {0};
  DecoderStage m_decoderStage {DecoderStage::None};
  bool m_sawEarlyStandardDecode {false};
  bool m_sawConfiguredMultithreadedDecode {false};
  bool m_fixtureFinished {false};
  bool m_armed {false};
  bool m_finished {false};
  bool m_succeeded {false};
};

#endif
