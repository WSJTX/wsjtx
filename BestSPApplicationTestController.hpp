// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BEST_SP_APPLICATION_TEST_CONTROLLER_HPP
#define BEST_SP_APPLICATION_TEST_CONTROLLER_HPP

#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

class AudioInputSource;
class AudioOutputSink;
class DecoderBackend;
class MainWindow;
class QSettings;

std::unique_ptr<DecoderBackend> makeBestSPTestBackend ();
std::unique_ptr<AudioInputSource> makeBestSPTestAudioInput ();
std::unique_ptr<AudioOutputSink> makeBestSPTestAudioOutput ();

class BestSPTestEnvironment final
{
public:
  BestSPTestEnvironment ();
  ~BestSPTestEnvironment ();

private:
  QString settingsPath_;
  QString dataPath_;
};

class BestSPApplicationTestController final : public QObject
{
public:
  BestSPApplicationTestController (MainWindow *, DecoderBackend *, QObject * parent = nullptr);
  static QString prepareSettings (QSettings *);
  void begin ();
  bool succeeded () const {return succeeded_;}

private:
  void prepare ();
  bool run ();
  bool armingEligibility ();
  bool audioFrequencyChanges ();
  bool competingCQSelections ();
  bool pounceSelections ();
  bool settingsCQSelection ();
  bool feedbackMatches (bool armed, QString const& expected);
  bool feedbackLifecycle ();
  bool contextAndReplyPaths ();
  bool translatedSelections ();
  bool attemptArming (bool shortcut, QString const& expectedFeedback);
  bool startRequest (quint64 frequency, QString const& scenario);
  bool record (QString const& payload);
  bool complete (QString const& expectedCall);
  bool require (bool condition, QString const& message);
  void finish (bool success, QString const& message);

  MainWindow * window_;
  DecoderBackend * backend_;
  QTimer retry_;
  QTimer timeout_;
  QString scenario_;
  bool finished_ {false};
  bool succeeded_ {false};
};

#endif
