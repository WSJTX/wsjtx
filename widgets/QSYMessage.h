// -*- Mode: C++ -*-
#ifndef QSYMESSAGE_H
#define QSYMESSAGE_H

#include <QWidget>
#include <QCloseEvent>

class QSettings;
class Configuration;
namespace Ui {
  class QSYMessage;
}

class QSYMessage
  : public QWidget
{
  Q_OBJECT


public:
  explicit QSYMessage(const QString& message, const QString& theCall, QSettings * settings, Configuration const *, QWidget * parent = 0);
  ~QSYMessage();
  void getBandModeFreq();
 
public slots:
  void write_settings ();

protected:
  void closeEvent(QCloseEvent *event) override;

signals:
  void sendReply(const QString &value);

private:
  QSettings * settings_;
  Configuration const * configuration_;
  Ui::QSYMessage *ui;
  QString receivedMessage;
  QString receivedCall;

private slots:
  void on_yesButton_clicked();
  void on_noButton_clicked();
  void read_settings ();
};

#endif //QSYMESSAGE_H
