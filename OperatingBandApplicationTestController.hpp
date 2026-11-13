// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPERATING_BAND_APPLICATION_TEST_CONTROLLER_HPP
#define OPERATING_BAND_APPLICATION_TEST_CONTROLLER_HPP

#include <functional>
#include <QObject>
#include <QString>
#include <QTimer>
#include "Radio.hpp"

class DecoderBackend;
class MainWindow;
class QSettings;

class OperatingBandApplicationTestController final : public QObject
{
public:
  OperatingBandApplicationTestController (MainWindow *, DecoderBackend *, QObject * parent = nullptr);
  static QString prepareSettings (QSettings *);
  void begin ();
  bool succeeded () const {return succeeded_;}

private:
  void prepare ();
  bool run ();
  bool filtering ();
  bool editorRequests ();
  bool rendering ();
  bool modeEntry ();
  bool mskModeExit ();
  bool modalObservation ();
  bool transitions ();
  bool hopping ();
  bool reporting ();
  bool setFrequency (Radio::Frequency, bool band_selection = false);
  bool waitUntil (std::function<bool()> const&, QString const&);
  bool enter (QString const&, Radio::Frequency);
  bool require (bool, QString const&);
  void finish (bool, QString const&);

  MainWindow * window_;
  DecoderBackend * backend_;
  QTimer retry_;
  QTimer timeout_;
  QString scenario_;
  bool finished_ {false};
  bool succeeded_ {false};
  bool echo_startup_ {false};
};

#endif
