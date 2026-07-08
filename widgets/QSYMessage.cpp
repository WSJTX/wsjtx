#include <QApplication>
#include <QSettings>
#include <QCloseEvent>
#include <QDateTime>
#include "SettingsGroup.hpp"
#include "Configuration.hpp"
#include "QSYMessage.h"
#include "widgets/QSYMessageParser.h"
#include "ui_QSYMessage.h"

QSYMessage::QSYMessage(const QString& message,const QString& theCall, QSettings * settings, Configuration const * configuration, QWidget *parent) :
  QWidget {parent},
  settings_ {settings},
  configuration_ {configuration},
  ui(new Ui::QSYMessage),
  receivedMessage(message),receivedCall(theCall)
{
  ui->setupUi(this);
  read_settings();
  setWindowTitle ("Message" + QDateTime::currentDateTimeUtc().toString(" [hh:mm:ss]"));
  ui->label->setStyleSheet("font: bold; font-size: 30pt");
  getBandModeFreq();
}

QSYMessage::~QSYMessage()
{
  delete ui;
}

void QSYMessage::closeEvent (QCloseEvent * e) {
  write_settings();
  e->accept();
}

void QSYMessage::read_settings () {
  SettingsGroup g (settings_, "QSYMessage");
  move (settings_->value ("window/pos", pos()).toPoint());
}

void QSYMessage::write_settings () {
  SettingsGroup g (settings_, "QSYMessage");
  settings_->setValue ("window/pos", pos());
}

void QSYMessage::on_yesButton_clicked()
{
  QString message = QString(Radio::base_callsign(receivedCall)) + QString(".OKQSY");
  Q_EMIT sendReply(message);
  ui->yesButton->setStyleSheet("background-color:#00ff00");
  ui->noButton->setStyleSheet("background-color:palette(button).color()");
}

void QSYMessage::on_noButton_clicked()
{
  QString message = QString(Radio::base_callsign(receivedCall)) + QString(".NOQSY");
  Q_EMIT sendReply(message);
  ui->noButton->setStyleSheet("background-color:red; color:white");
  ui->yesButton->setStyleSheet("background-color:palette(button).color()");
}

void QSYMessage::getBandModeFreq()
{
  QSYMessageParser::Message const message =
      QSYMessageParser::decode (receivedMessage, configuration_->region ());
  switch (message.type) {
  case QSYMessageParser::Type::Reply:
    ui->label->setText (message.call + " replied " + message.response);
    ui->yesButton->hide ();
    ui->noButton->hide ();
    break;
  case QSYMessageParser::Type::General:
    ui->noButton->setHidden (true);
    ui->yesButton->setText ("OK");
    ui->label->setText (message.text);
    break;
  case QSYMessageParser::Type::Frequency:
    ui->label->setText ("QSY to\n" + message.frequency_mhz + " MHz\n" + "mode " + message.mode);
    break;
  case QSYMessageParser::Type::Invalid:
    break;
  }
}
