#include "logqso.h"

#include <QLocale>
#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

#include "logbook/logbook.h"
#include "MessageBox.hpp"
#include "Configuration.hpp"
#include "models/Bands.hpp"
#include "models/CabrilloLog.hpp"
#include "validators/MaidenheadLocatorValidator.hpp"

#include "ui_logqso.h"
#include "moc_logqso.cpp"

namespace
{
  struct PropMode
  {
    char const * id_;
    char const * name_;
  };
  constexpr PropMode prop_modes[] =
    {
     {"", ""}
     , {"AS", QT_TRANSLATE_NOOP ("LogQSO", "Aircraft scatter")}
     , {"AUE", QT_TRANSLATE_NOOP ("LogQSO", "Aurora-E")}
     , {"AUR", QT_TRANSLATE_NOOP ("LogQSO", "Aurora")}
     , {"BS", QT_TRANSLATE_NOOP ("LogQSO", "Back scatter")}
     , {"ECH", QT_TRANSLATE_NOOP ("LogQSO", "Echolink")}
     , {"EME", QT_TRANSLATE_NOOP ("LogQSO", "Earth-moon-earth")}
     , {"ES", QT_TRANSLATE_NOOP ("LogQSO", "Sporadic E")}
     , {"F2", QT_TRANSLATE_NOOP ("LogQSO", "F2 Reflection")}
     , {"FAI", QT_TRANSLATE_NOOP ("LogQSO", "Field aligned irregularities")}
     , {"INTERNET", QT_TRANSLATE_NOOP ("LogQSO", "Internet-assisted")}
     , {"ION", QT_TRANSLATE_NOOP ("LogQSO", "Ionoscatter")}
     , {"IRL", QT_TRANSLATE_NOOP ("LogQSO", "IRLP")}
     , {"MS", QT_TRANSLATE_NOOP ("LogQSO", "Meteor scatter")}
     , {"RPT", QT_TRANSLATE_NOOP ("LogQSO", "Non-satellite repeater or transponder")}
     , {"RS", QT_TRANSLATE_NOOP ("LogQSO", "Rain scatter")}
     , {"SAT", QT_TRANSLATE_NOOP ("LogQSO", "Satellite")}
     , {"TEP", QT_TRANSLATE_NOOP ("LogQSO", "Trans-equatorial")}
     , {"TR", QT_TRANSLATE_NOOP ("LogQSO", "Troposheric ducting")}
    };
}

LogQSO::LogQSO(QString const& programTitle, QSettings * settings
               , Configuration const * config, LogBook * log, QWidget *parent)
  : QDialog {parent, Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint}
  , ui(new Ui::LogQSO)
  , m_settings (settings)
  , m_config {config}
  , m_log {log}
{
  ui->setupUi(this);
  setWindowTitle(programTitle + " - Log QSO");
  for (auto const& prop_mode : prop_modes)
    {
      ui->comboBoxPropMode->addItem (prop_mode.name_, prop_mode.id_);
    }
  loadSettings ();
  auto date_time_format = QLocale {}.dateFormat (QLocale::ShortFormat) + " hh:mm:ss";
  ui->start_date_time->setDisplayFormat (date_time_format);
  ui->end_date_time->setDisplayFormat (date_time_format);
  ui->grid->setValidator (new MaidenheadLocatorValidator {this});
}

LogQSO::~LogQSO ()
{
}

void LogQSO::loadSettings ()
{
  m_settings->beginGroup ("LogQSO");
  restoreGeometry (m_settings->value ("geometry", saveGeometry ()).toByteArray ());
  ui->cbTxPower->setChecked (m_settings->value ("SaveTxPower", false).toBool ());
  ui->cbComments->setChecked (m_settings->value ("SaveComments", false).toBool ());
  ui->cbPropMode->setChecked (m_settings->value ("SavePropMode", false).toBool ());
  m_txPower = m_settings->value ("TxPower", "").toString ();
  m_comments = m_settings->value ("LogComments", "").toString();
  int prop_index {0};
  if (ui->cbPropMode->isChecked ())
    {
      prop_index = ui->comboBoxPropMode->findData (m_settings->value ("PropMode", "").toString());
    }
  ui->comboBoxPropMode->setCurrentIndex (prop_index);
  m_settings->endGroup ();
}

void LogQSO::storeSettings () const
{
  m_settings->beginGroup ("LogQSO");
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue ("SaveTxPower", ui->cbTxPower->isChecked ());
  m_settings->setValue ("SaveComments", ui->cbComments->isChecked ());
  m_settings->setValue ("SavePropMode", ui->cbPropMode->isChecked ());
  m_settings->setValue ("TxPower", m_txPower);
  m_settings->setValue ("LogComments", m_comments);
  m_settings->setValue ("PropMode", ui->comboBoxPropMode->currentData ());
  m_settings->endGroup ();
}

void LogQSO::initLogQSO(QString const& hisCall, QString const& hisGrid, QString mode,
                        QString const& rptSent, QString const& rptRcvd,
                        QDateTime const& dateTimeOn, QDateTime const& dateTimeOff,
                        Radio::Frequency dialFreq, bool noSuffix, QString xSent, QString xRcvd)
{
  if(!isHidden()) return;
  ui->call->setText (hisCall);
  ui->grid->setText (hisGrid);
  ui->name->clear ();
  if (ui->cbTxPower->isChecked ())
    {
      ui->txPower->setText (m_txPower);
    }
  else
    {
      ui->txPower->clear ();
    }
  if (ui->cbComments->isChecked ())
    {
      ui->comments->setText (m_comments);
    }
  else
    {
      ui->comments->clear ();
    }
  if (m_config->report_in_comments()) {
    auto t=mode;
    if(rptSent!="") t+="  Sent: " + rptSent;
    if(rptRcvd!="") t+="  Rcvd: " + rptRcvd;
    ui->comments->setText(t);
  }
  if(noSuffix and mode.mid(0,3)=="JT9") mode="JT9";
  if(m_config->log_as_RTTY() and mode.mid(0,3)=="JT9") mode="RTTY";
  ui->mode->setText(mode);
  ui->sent->setText(rptSent);
  ui->rcvd->setText(rptRcvd);
  ui->start_date_time->setDateTime (dateTimeOn);
  ui->end_date_time->setDateTime (dateTimeOff);
  m_dialFreq=dialFreq;
  m_myCall=m_config->my_callsign();
  m_myGrid=m_config->my_grid();
  ui->band->setText (m_config->bands ()->find (dialFreq));
  ui->loggedOperator->setText(m_config->opCall());
  ui->exchSent->setText (xSent);
  ui->exchRcvd->setText (xRcvd);
  if (!ui->cbPropMode->isChecked ())
    {
      ui->comboBoxPropMode->setCurrentIndex (-1);
    }

  using SpOp = Configuration::SpecialOperatingActivity;
  auto special_op = m_config->special_op_id ();
  if (SpOp::FOX == special_op
      || (m_config->autoLog ()
          && SpOp::NONE < special_op && special_op < SpOp::FOX))
    {
      // allow auto logging in Fox mode and contests
      accept();
    }
  else
    {
      show();
    }
}

void LogQSO::accept()
{
  auto hisCall = ui->call->text ();
  auto hisGrid = ui->grid->text ();
  auto mode = ui->mode->text ();
  auto rptSent = ui->sent->text ();
  auto rptRcvd = ui->rcvd->text ();
  auto dateTimeOn = ui->start_date_time->dateTime ();
  auto dateTimeOff = ui->end_date_time->dateTime ();
  auto band = ui->band->text ();
  auto name = ui->name->text ();
  m_txPower = ui->txPower->text ();
  m_comments = ui->comments->text ();
  auto strDialFreq = QString::number (m_dialFreq / 1.e6,'f',6);
  auto operator_call = ui->loggedOperator->text ();
  auto xsent = ui->exchSent->text ();
  auto xrcvd = ui->exchRcvd->text ();

  using SpOp = Configuration::SpecialOperatingActivity;
  auto special_op = m_config->special_op_id ();

  if (special_op == SpOp::NA_VHF or special_op == SpOp::WW_DIGI) {
    if(xrcvd!="" and hisGrid!=xrcvd) hisGrid=xrcvd;
  }

  if ((special_op == SpOp::RTTY and xsent!="" and xrcvd!="")) {
    if(rptSent=="" or !xsent.contains(rptSent+" ")) rptSent=xsent.split(" "
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                                                                        , QString::SkipEmptyParts
#else
                                                                        , Qt::SkipEmptyParts
#endif
                                                                        ).at(0);
    if(rptRcvd=="" or !xrcvd.contains(rptRcvd+" ")) rptRcvd=xrcvd.split(" "
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                                                                        , QString::SkipEmptyParts
#else
                                                                        , Qt::SkipEmptyParts
#endif
                                                                        ).at(0);
  }

  // validate
  if (SpOp::NONE < special_op && special_op < SpOp::FOX)
    {
      if (xsent.isEmpty () || xrcvd.isEmpty ())
        {
          show ();
          MessageBox::warning_message (this, tr ("Invalid QSO Data"),
                                       tr ("Check exchange sent and received"));
          return;               // without accepting
        }

      if (!m_log->contest_log ()->add_QSO (m_dialFreq, mode, dateTimeOff, hisCall, xsent, xrcvd))
        {
          show ();
          MessageBox::warning_message (this, tr ("Invalid QSO Data"),
                                       tr ("Check all fields"));
          return;               // without accepting
        }
    }

  auto const& prop_mode = ui->comboBoxPropMode->currentData ().toString ();
  //Log this QSO to file "wsjtx.log"
  static QFile f {QDir {QStandardPaths::writableLocation (QStandardPaths::DataLocation)}.absoluteFilePath ("wsjtx.log")};
  if(!f.open(QIODevice::Text | QIODevice::Append)) {
    MessageBox::warning_message (this, tr ("Log file error"),
                                 tr ("Cannot open \"%1\" for append").arg (f.fileName ()),
                                 tr ("Error: %1").arg (f.errorString ()));
  } else {
    QString logEntry=dateTimeOn.date().toString("yyyy-MM-dd,") +
      dateTimeOn.time().toString("hh:mm:ss,") +
      dateTimeOff.date().toString("yyyy-MM-dd,") +
      dateTimeOff.time().toString("hh:mm:ss,") + hisCall + "," +
      hisGrid + "," + strDialFreq + "," + mode +
      "," + rptSent + "," + rptRcvd + "," + m_txPower +
      "," + m_comments + "," + name + "," + prop_mode;
    QTextStream out(&f);
    out << logEntry <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl
#else
                 Qt::endl
#endif
                 ;
    f.close();
  }

  //Clean up and finish logging
  Q_EMIT acceptQSO (dateTimeOff
                    , hisCall
                    , hisGrid
                    , m_dialFreq
                    , mode
                    , rptSent
                    , rptRcvd
                    , m_txPower
                    , m_comments
                    , name
                    , dateTimeOn
                    , operator_call
                    , m_myCall
                    , m_myGrid
                    , xsent
                    , xrcvd
                    , prop_mode
                    , m_log->QSOToADIF (hisCall
                                        , hisGrid
                                        , mode
                                        , rptSent
                                        , rptRcvd
                                        , dateTimeOn
                                        , dateTimeOff
                                        , band
                                        , m_comments
                                        , name
                                        , strDialFreq
                                        , m_myCall
                                        , m_myGrid
                                        , m_txPower
                                        , operator_call
                                        , xsent
                                        , xrcvd
                                        , prop_mode));
  QDialog::accept();
}

// closeEvent is only called from the system menu close widget for a
// modeless dialog so we use the hideEvent override to store the
// window settings
void LogQSO::hideEvent (QHideEvent * e)
{
  storeSettings ();
  QDialog::hideEvent (e);
}
