#include "logqso.h"

#include <QLocale>
#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QDir>
#include <QTimer>
#include <QPushButton>

#include "HelpTextWindow.hpp"
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
  auto const sat_file_name = "sat.dat";
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
  QString sat_file_location;
  QDir dataPath {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  sat_file_location = dataPath.exists(sat_file_name) ? dataPath.absoluteFilePath(sat_file_name) : m_config->data_dir ().absoluteFilePath (sat_file_name);
  QFile file {sat_file_location};
  QStringList wordList;
  QTextStream stream(&file);
  if(file.open (QIODevice::ReadOnly | QIODevice::Text)) {
      while (!stream.atEnd()) {
          QString line = stream.readLine();
          wordList = line.split('|');
          ui->comboBoxSatellite->addItem (wordList[1], wordList[0]);
      }
      stream.flush();
      file.close();
  }
  for (auto const& prop_mode : prop_modes)
    {
      ui->comboBoxPropMode->addItem (prop_mode.name_, prop_mode.id_);
    }
  loadSettings ();
  connect (ui->comboBoxPropMode, &QComboBox::currentTextChanged, this, &LogQSO::propModeChanged);
  connect (ui->comments, &QComboBox::currentTextChanged, this, &LogQSO::commentsChanged);
  connect (ui->addButton, &QPushButton::clicked, this, &LogQSO::on_addButton_clicked);
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
  ui->cbSatellite->setChecked (m_settings->value ("SaveSatellite", false).toBool ());
  m_comments = m_settings->value ("LogComments", "").toString();
  ui->comments->setCurrentIndex(0);
  ui->comments->setItemText (ui->comments->currentIndex(), m_comments);
  m_txPower = m_settings->value ("TxPower", "").toString ();

  int prop_index {0};
  if (ui->cbPropMode->isChecked ())
    {
      prop_index = ui->comboBoxPropMode->findData (m_settings->value ("PropMode", "").toString());
    }
  ui->comboBoxPropMode->setCurrentIndex (prop_index);
  int satellite {0};
  if (ui->cbSatellite->isChecked ())
    {
      satellite = ui->comboBoxSatellite->findData (m_settings->value ("Satellite", "").toString());
    }
  ui->comboBoxSatellite->setCurrentIndex (satellite);
  if (m_settings->value ("PropMode", "") != "SAT")
  {
      ui->cbSatellite->setDisabled(true);
      ui->comboBoxSatellite->setDisabled(true);
  }
  m_freqRx = m_settings->value ("FreqRx", "").toString ();
  ui->cbFreqRx->setChecked (m_settings->value ("SaveFreqRx", false).toBool ());

  QString comments_location;  // load the content of comments.txt file to the comments combo box
  QDir dataPath {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  comments_location = dataPath.exists("comments.txt") ? dataPath.absoluteFilePath("comments.txt") : m_config->data_dir ().absoluteFilePath ("comments.txt");
  QFile file2 = {comments_location};
  QTextStream stream2(&file2);
  if(file2.open (QIODevice::ReadOnly | QIODevice::Text)) {
      while (!stream2.atEnd()) {
          QString line = stream2.readLine();
          ui->comments->addItem (line);
      }
      stream2.flush();
      file2.close();
  }

  m_settings->endGroup ();
}

void LogQSO::storeSettings () const
{
  m_settings->beginGroup ("LogQSO");
  m_settings->setValue ("geometry", saveGeometry ());
  m_settings->setValue ("SaveTxPower", ui->cbTxPower->isChecked ());
  m_settings->setValue ("SaveComments", ui->cbComments->isChecked ());
  m_settings->setValue ("SavePropMode", ui->cbPropMode->isChecked ());
  m_settings->setValue ("SaveSatellite", ui->cbSatellite->isChecked ());
  m_settings->setValue ("SaveFreqRx", ui->cbFreqRx->isChecked ());
  m_settings->setValue ("TxPower", m_txPower);
  m_settings->setValue ("LogComments", m_comments);
  m_settings->setValue ("PropMode", ui->comboBoxPropMode->currentData ());
  m_settings->setValue ("Satellite", ui->comboBoxSatellite->currentData ());
  m_settings->setValue ("FreqRx", m_freqRx);
  m_settings->endGroup ();
}

void LogQSO::initLogQSO(QString const& hisCall, QString const& hisGrid, QString mode,
                        QString const& rptSent, QString const& rptRcvd,
                        QDateTime const& dateTimeOn, QDateTime const& dateTimeOff,
                        Radio::Frequency dialFreq, bool noSuffix, QString xSent, QString xRcvd)
{
  if(!isHidden()) return;
/*
  QPushButton* okBtn = ui->buttonBox->button(QDialogButtonBox::Ok);
  okBtn->setAutoDefault(true);
  okBtn->setDefault(true);
  okBtn->setFocus();
  QPushButton* caBtn = ui->buttonBox->button(QDialogButtonBox::Cancel);
  caBtn->setAutoDefault(false);
  caBtn->setDefault(false);
*/
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
  if (ui->cbFreqRx->isChecked ())
    {
      ui->freqRx->setText (m_freqRx);
    }
  else
    {
      ui->freqRx->clear ();
    }
  if (ui->cbComments->isChecked ())
    {
      ui->comments->setCurrentIndex(0);
      ui->comments->setItemText(ui->comments->currentIndex(), m_comments);
    }
  else
    {
      ui->comments->setCurrentIndex(0);
      ui->comments->setItemText(ui->comments->currentIndex(), "");
    }
  if (m_config->report_in_comments()) {
    auto t=mode;
    if(rptSent!="") t+="  Sent: " + rptSent;
    if(rptRcvd!="") t+="  Rcvd: " + rptRcvd;
    ui->comments->setCurrentIndex(0);
    ui->comments->setItemText(ui->comments->currentIndex(), t);
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
  if (!ui->cbSatellite->isChecked ())
    {
      ui->comboBoxSatellite->setCurrentIndex (-1);
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
  // put contest name in comments
  if (SpOp::NONE != special_op && m_config->Individual_Contest_Name() && !m_config->report_in_comments()
      && m_config->Contest_Name() !="" && !ui->cbComments->isChecked()) {
    QString Contest_Name = (m_config->Contest_Name() + " Contest");
    ui->comments->setCurrentIndex(0);
    ui->comments->setItemText(ui->comments->currentIndex(), Contest_Name);
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
  auto const& satellite = ui->comboBoxSatellite->currentData ().toString ();
  m_freqRx = ui->freqRx->text ();
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
      "," + m_comments + "," + name + "," + prop_mode +
      "," + satellite + "," + m_freqRx;
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
                    , satellite
                    , m_freqRx
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
                                        , prop_mode
                                        , satellite
                                        , m_freqRx));
  QDialog::accept();
}

void LogQSO::propModeChanged()
{
  if (ui->comboBoxPropMode->currentData() != "SAT") {
      ui->comboBoxSatellite->setDisabled(true);
      ui->cbSatellite->setDisabled(true);
  } else {
      ui->comboBoxSatellite->setDisabled(false);
      ui->cbSatellite->setDisabled(false);
  }
}

void LogQSO::commentsChanged(const QString& text)   // UR
{
  int index = ui->comments->findText(text);
  if(index != -1)
  {
    ui->comments->setCurrentIndex(index);
   }
  ui->comments->setItemText(ui->comments->currentIndex(), text);
  m_comments = text;
  m_comments_temp = text;
}

void LogQSO::on_addButton_clicked()
{
  m_settings->setValue ("LogComments", m_comments_temp);
  if (m_comments_temp != "") {
      QString comments_location = m_config->writeable_data_dir().absoluteFilePath("comments.txt");
      if(QFileInfo::exists(m_config->writeable_data_dir().absoluteFilePath("comments.txt"))) {
      QFile file2 = {comments_location};
        if (file2.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
            QTextStream out(&file2);
            out << m_comments_temp              // append new line to comments.txt
    #if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
            << Qt::endl
    #else
            << endl
    #endif
            ;
          file2.close();
          MessageBox::information_message (this,
                                           "Your comment has been added to the comments list.\n\n"
                                           "To edit your comments list, open the file\n"
                                           "\"comments.txt\" from your log directory");
        }
      } else {
          QFile file2 = {comments_location};
         if (file2.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
             QTextStream out(&file2);
             out << ("\n" + m_comments_temp)    // create file "comments.txt" and add a blank line
    #if QT_VERSION >= QT_VERSION_CHECK (5, 15, 0)
              << Qt::endl
    #else
              << endl
    #endif
            ;
          file2.close();
          MessageBox::information_message (this,
                                           "Your comment has been added to the comments list.\n\n"
                                           "To edit your comments list, open the file\n"
                                           "\"comments.txt\" from your log directory");
         }
      }
      ui->comments->clear();               // clear the comments combo box and reload updated content
      QFile file2 = {comments_location};
      QTextStream stream2(&file2);
      if(file2.open (QIODevice::ReadOnly | QIODevice::Text)) {
          while (!stream2.atEnd()) {
              QString line = stream2.readLine();
              ui->comments->addItem (line);
          }
          stream2.flush();
          file2.close();
          if (ui->cbComments->isChecked ()) {
              m_comments = m_settings->value ("LogComments", "").toString();
              ui->comments->setCurrentIndex(0);
              ui->comments->setItemText (ui->comments->currentIndex(), m_comments);
          }
      }
  }
}

// closeEvent is only called from the system menu close widget for a
// modeless dialog so we use the hideEvent override to store the
// window settings
void LogQSO::hideEvent (QHideEvent * e)
{
  storeSettings ();
  QDialog::hideEvent (e);
}
