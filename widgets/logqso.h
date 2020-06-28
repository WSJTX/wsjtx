// -*- Mode: C++ -*-
#ifndef LogQSO_H_
#define LogQSO_H_

#include <QDialog>

#include <QString>
#include <QScopedPointer>
#include <QDateTime>

#include "Radio.hpp"

namespace Ui {
  class LogQSO;
}

class QSettings;
class Configuration;
class QByteArray;
class LogBook;

class LogQSO : public QDialog
{
  Q_OBJECT

public:
  explicit LogQSO(QString const& programTitle, QSettings *, Configuration const *, LogBook *, QWidget *parent = 0);
  ~LogQSO();
  void initLogQSO(QString const& hisCall, QString const& hisGrid, QString mode,
                  QString const& rptSent, QString const& rptRcvd, QDateTime const& dateTimeOn,
                  QDateTime const& dateTimeOff, Radio::Frequency dialFreq, 
                  bool noSuffix, QString xSent, QString xRcvd);

public slots:
  void accept();

signals:
  void acceptQSO (QDateTime const& QSO_date_off, QString const& call, QString const& grid
                  , Radio::Frequency dial_freq, QString const& mode
                  , QString const& rpt_sent, QString const& rpt_received
                  , QString const& tx_power, QString const& comments
                  , QString const& name, QDateTime const& QSO_date_on,  QString const& operator_call
                  , QString const& my_call, QString const& my_grid
                  , QString const& exchange_sent, QString const& exchange_rcvd
                  , QString const& propmode, QByteArray const& ADIF);

protected:
  void hideEvent (QHideEvent *);

private:
  void loadSettings ();
  void storeSettings () const;

  QScopedPointer<Ui::LogQSO> ui;
  QSettings * m_settings;
  Configuration const * m_config;
  LogBook * m_log;
  QString m_txPower;
  QString m_comments;
  Radio::Frequency m_dialFreq;
  QString m_myCall;
  QString m_myGrid;
};

#endif // LogQSO_H
