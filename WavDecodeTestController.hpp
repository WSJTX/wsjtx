#ifndef WAV_DECODE_TEST_CONTROLLER_HPP__
#define WAV_DECODE_TEST_CONTROLLER_HPP__

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

class MainWindow;

class WavDecodeTestController final : public QObject
{
public:
  enum class Mode {Jt4, Jt9, Jt65, Q65, Fst4, Fst4w};

  WavDecodeTestController (MainWindow * window, QString wavPath,
                           QString expectedMessage, Mode mode, QObject * parent = nullptr);
  void begin ();
  bool succeeded () const {return m_succeeded;}

private:
  void prepareWhenReady ();
  enum class Stage {ShortFirst, Initial, Repeat, ShortBeforeMonitoring, ShortReplacement, ShortRepeat};
  bool expectsMessage () const {return m_stage == Stage::Initial || m_stage == Stage::Repeat;}
  void loadShortWav ();
  void decodeShortWavWhenReady ();
  bool rejectsLiveRepeat ();
  void completeCycle (quint64 generation);
  void finish (QString const& error = {});

  MainWindow * m_window;
  Mode m_mode;
  int m_period;
  QString m_modeName;
  QString m_wavPath;
  QString m_expectedMessage;
  QTemporaryDir m_fixtureDirectory;
  Stage m_stage {Stage::ShortFirst};
  QTimer m_timeout;
  QTimer m_prepareTimer;
  QTimer m_modalTimer;
  quint64 m_activeGeneration {0};
  quint64 m_lastGeneration {0};
  int m_completedCycles {0};
  bool m_awaitingCycle {false};
  bool m_observed {false};
  bool m_configured {false};
  bool m_started {false};
  bool m_finished {false};
  bool m_succeeded {false};
};

#endif
