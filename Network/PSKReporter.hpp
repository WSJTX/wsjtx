#ifndef PSK_REPORTER_HPP_
#define PSK_REPORTER_HPP_

#include <QObject>
#include <QDateTime>
#include <QString>
#include "Radio.hpp"
#include "pimpl_h.hpp"

class QString;
class Configuration;

class PSKReporter final
  : public QObject
{
  Q_OBJECT

public:
  struct Options
  {
    bool use_tcpip;
    QString eclipse_file_path;
    QString program_info;
  };

  explicit PSKReporter (Configuration const *, QString const& program_info);
  explicit PSKReporter (Options const& options);
  ~PSKReporter ();

  void reconnect ();

  void setLocalStation (QString const& call, QString const& grid, QString const& antenna, QString const& rigInformation);

  // Returns false if accepting the spot required dropping an older pending spot.
  bool addRemoteStation (QString const& call, QString const& grid, Radio::Frequency freq, QString const& mode, int snr, QDateTime spotTime);

  //
  // Flush any pending spots to PSK Reporter
  //
  void sendReport (bool last = false);

  //
  // True if current time falls withing a +/- window of a solar eclipse for HamSCI use
  bool eclipse_active(QDateTime now);

  Q_SIGNAL void errorOccurred (QString const& reason);

private:
  class impl;
  pimpl<impl> m_;
};

#endif
