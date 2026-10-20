// -*- Mode: C++ -*-
#ifndef LIVE_AUDIO_TEST_CONTROLLER_HPP__
#define LIVE_AUDIO_TEST_CONTROLLER_HPP__

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTextBlock>
#include <QElapsedTimer>
#include <QByteArray>
#include <QVector>
#include <vector>
#include "commons.h"

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
    Ft8TwoStage,
    Ft8ThreeStage,
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

  struct Ft8Invocation
  {
    int halfSymbols;
    quint64 cycle;
    QSet<QString> raw, processed, displayed;
  };

  struct Ft8SchedulingPoint
  {
    int halfSymbols;
    bool available;
    bool captured {false};
    bool published {false};
    bool verified {false};
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
  void observeJttyDisplay ();
  void pollJttyDisplay ();
  void checkJttyReviewAndStatus ();
  void completeJttyTest ();
  void fail (QString const& reason);
  void checkForUnexpectedModal ();
  void exerciseSubmittedFt8Rollover ();
  bool schedulingScenario () const {return m_mode == Mode::Ft8TwoStage || m_mode == Mode::Ft8ThreeStage;}
  void observeFt8SchedulingRequest (decoder_params_t const& params,
                                    decoder_input_metadata_t const& metadata,
                                    short const * samples, qint64 period, bool pending);
  void observeFt8SchedulingPoint (int halfSymbols, bool available);
  bool verifyFt8SchedulingPoint ();
  int finalFt8HalfSymbols () const {return m_mode == Mode::Ft8ThreeStage ? 50 : 49;}
  int expectedFt8Stages () const {return m_mode == Mode::Ft8ThreeStage ? 3 : 2;}
  Ft8Invocation * activeFt8Invocation ();
  bool verifyFt8SchedulingOutput ();

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
  std::vector<short> m_ft8FixtureSamples;
  QVector<Ft8Invocation> m_ft8Invocations;
  QVector<Ft8SchedulingPoint> m_ft8SchedulingPoints;
  decoder_input_metadata_t m_ft8FinalMetadata {};
  QSet<quint64> m_ft8CompletedCycles;
  quint64 m_ft8ActiveCycle {0};
  quint64 m_ft8LastPublication {0};
  quint64 m_ft8Epoch {0};
  qint64 m_ft8InputId {0};
  qint64 m_ft8AnalysisId {0};
  int m_ft8LastAttempt {0};
  int m_ft8AcceptedEnd {0};
  QString m_initializationError;
  QTimer m_timeout;
  QTimer m_prepareTimer;
  QTimer m_modalTimer;
  QTimer m_jttyPollTimer;
  QTimer m_jttyStageTimer;
  QTextEdit * m_jttyAllDecodes {nullptr};
  QTextEdit * m_jttyQsoFrequency {nullptr};
  qint64 m_emittedFrames {0};
  qint64 m_captureStartMs {0};
  QTextBlock m_jttyAllPrefixBlock, m_jttyQsoPrefixBlock;
  enum class JttyCheckStage { Live, Review, Stopped, Wav, Drain, TimedMode };
  JttyCheckStage m_jttyCheckStage {JttyCheckStage::Live};
  QElapsedTimer m_jttyCheckElapsed;
  QString m_jttyAllBeforeReview, m_jttyQsoBeforeReview;
  QVector<QTextBlock> m_jttyAllLiveBlocks, m_jttyQsoLiveBlocks;
  QString m_jttyLogPath, m_jttySaveDirectory;
  qint64 m_jttyInitialLogSize = 0;
  QByteArray m_jttyLogBeforeReview;
  QSet<QString> m_jttyInitialRecordings;
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
