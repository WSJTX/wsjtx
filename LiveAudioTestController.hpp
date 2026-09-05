// -*- Mode: C++ -*-
#ifndef LIVE_AUDIO_TEST_CONTROLLER_HPP__
#define LIVE_AUDIO_TEST_CONTROLLER_HPP__

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <vector>

class FixtureAudioInput;
class MainWindow;
class QTextEdit;

class LiveAudioTestController final
  : public QObject
{
  Q_OBJECT

public:
  enum class Mode
  {
    Ft8,
    Jtty
  };

  LiveAudioTestController (MainWindow * window, FixtureAudioInput * fixture,
                           QString expectedPath, Mode mode = Mode::Ft8,
                           QObject * parent = nullptr);

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
  static QStringList readExpectedJttyMessages (QString const& path, QString * error);
  static QString messageFromDecoderLine (QByteArray const& line);
  void prepareWhenReady ();
  void prepareFt8WhenReady ();
  void prepareJttyWhenReady ();
  void maybeFinish ();
  void maybeFinishFt8 ();
  void maybeFinishJtty ();
  void pollJttyDisplay ();
  void fail (QString const& reason);
  void checkForUnexpectedModal ();
  void exerciseSubmittedFt8Rollover ();

  MainWindow * m_window;
  FixtureAudioInput * m_fixture;
  QString m_expectedPath;
  Mode m_mode;
  QSet<QString> m_expected;
  QStringList m_expectedJtty;
  QString m_expectedJttyPrefix;
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
  QTimer m_jttyPollTimer;
  QTextEdit * m_jttyAllDecodes {nullptr};
  QTextEdit * m_jttyQsoFrequency {nullptr};
  qint64 m_emittedFrames {0};
  int m_completedCycles {0};
  quint64 m_submittedGeneration {0};
  quint64 m_submittedCycle {0};
  quint64 m_rolloverEpoch {0};
  quint64 m_preRolloverEpoch {0};
  int m_rolloverAcceptedEnd {0};
  int m_rolloverAcceptedBlocks {0};
  int m_rolloverRejectedBlocks {0};
  int m_submittedCompletions {0};
  std::vector<short> m_rolloverSamples;
  bool m_rolloverVerified {false};
  DecoderStage m_decoderStage {DecoderStage::None};
  bool m_sawEarlyStandardDecode {false};
  bool m_sawConfiguredMultithreadedDecode {false};
  bool m_completedMultithreadedDecode {false};
  bool m_fixtureFinished {false};
  bool m_armed {false};
  bool m_jttyAllSawPrefix {false};
  bool m_jttyQsoSawPrefix {false};
  QSet<QString> m_jttyAllFinals;
  QSet<QString> m_jttyQsoFinals;
  bool m_finished {false};
  bool m_succeeded {false};
};

#endif
