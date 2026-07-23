#include "qsymonitor.h"
#include "ui_qsymonitor.h"
#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "widgets/QSYMessageParser.h"
#include "qt_helpers.hpp"
#include "commons.h"
#include <QSettings>
#include <QObject>
#include <QCloseEvent>
#include <QThread>
#include <QLabel>
#include <QWidget>
#include <QMessageBox>
#include <QStringList>

QSYMonitor::QSYMonitor(QSettings * settings, QFont const& font, Configuration const * configuration, QWidget *parent)
  : QWidget(parent),
  settings_ {settings},
  configuration_ {configuration},
  ui(new Ui::QSYMonitor)
{
  ui->setupUi(this);
  setWindowTitle (QApplication::applicationName () + " - " + tr ("QSY Monitor"));
  ui->QSYMonitorTextBrowser->setReadOnly (true);
  changeFont (font);
  read_settings ();
  ui->qsyMonitorLabel->setText("  UTC    Call      Freq        Mode");
}

QSYMonitor::~QSYMonitor()
{
  delete ui;
}

void QSYMonitor::getQSYData(QString value)
{
  createQSYLine(value);
}

void QSYMonitor::read_settings ()
{
  SettingsGroup g (settings_, "QSYMonitor");
  move (settings_->value ("window/pos", pos ()).toPoint ());
  restoreGeometry(settings_->value("geometry").toByteArray());
}

void QSYMonitor::write_settings ()
{
  SettingsGroup g (settings_, "QSYMonitor");
  settings_->setValue ("window/pos", pos ());
  settings_->setValue ("geometry", saveGeometry());
}

void QSYMonitor::changeFont (QFont const& font)
{
  ui->qsyMonitorLabel->setStyleSheet (font_as_stylesheet (font));
  ui->QSYMonitorTextBrowser->setStyleSheet (font_as_stylesheet (font));
  updateGeometry ();
}

void QSYMonitor::closeEvent (QCloseEvent * e)
{
  write_settings();
  e->accept();                 // was ignore
}

void QSYMonitor::on_clearButton_clicked()
{
  ui->QSYMonitorTextBrowser->clear();
}

void QSYMonitor::createQSYLine(QString value)
{
  QStringList qsySpot = value.split(' ', SkipEmptyParts);
  if(qsySpot.length() == 3) {
    QString theTime = qsySpot[0];
    QString theCall = qsySpot[1];
    getBandModeFreq(theTime, theCall, qsySpot[2]);
  }
}

void QSYMonitor::getBandModeFreq(QString theTime, QString theCall, QString value)
{
  QSYMessageParser::Message const message =
      QSYMessageParser::decode (value, configuration_->region ());
  if (message.type != QSYMessageParser::Type::Frequency) return;

  int numSpaces1 = 2;
  int numSpaces2 = 10 - theCall.length();
  int numSpaces3 = 11 - message.frequency_mhz.length();
  QString spaces1_theCall = QString(numSpaces1, ' ').append(theCall);
  QString spaces2_theFreq = QString(numSpaces2, ' ').append(message.frequency_mhz);
  QString spaces3_theMode = QString(numSpaces3, ' ').append(message.mode);
  QString qsyMessageString = theTime + spaces1_theCall + spaces2_theFreq + spaces3_theMode;
  ui->QSYMonitorTextBrowser->append(qsyMessageString);
}
