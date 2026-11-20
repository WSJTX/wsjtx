// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WAIT_FEATURES_APPLICATION_TEST_CONTROLLER_HPP
#define WAIT_FEATURES_APPLICATION_TEST_CONTROLLER_HPP

#include <functional>

#include <QObject>
#include <QString>
#include <QTimer>

class DecoderBackend;
class MainWindow;
class QSettings;

class WaitFeaturesApplicationTestController final : public QObject
{
public:
  WaitFeaturesApplicationTestController (MainWindow *, DecoderBackend *, QObject * parent = nullptr);
  static QString prepareSettings (QSettings *);
  void begin ();
  bool succeeded () const {return succeeded_;}

private:
  void prepare ();
  bool run ();
  bool correctionScenarios ();
  bool legacyMessageScenarios ();
  bool msk144Scenarios ();
  bool tuneShutdownScenarios ();
  bool waitUntil (std::function<bool()> const&, QString const& failure);
  bool setOption (QString const& name, bool enabled,
                  std::function<bool()> const& afterAccept = {});
  bool startReception (QString const& mode, QString const& scenario);
  bool nextReception ();
  bool publishReception ();
  void record (QString const& payload, int frequency = 1500, QString const& suffix = {});
  bool enableTxIs (bool enabled) const;
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
