#ifndef MESSAGES_H
#define MESSAGES_H

#include <QDialog>
#include "commons.h"
#include <memory>
#include <QNetworkReply>

namespace Ui {
  class Messages;
}

class PSKReporter;

class Messages : public QDialog
{
  Q_OBJECT

public:
  explicit Messages (QString const& settings_filename, QString const& eclipse_filename,
                     QWidget * parent = nullptr);
  void setText(QString t, QString t2);
  void setColors(QString t);
  void setPSKReportingEnabled(bool enabled);
  void setClosingForShutdown(bool value) { m_closingForShutdown = value; }

  ~Messages();
  
signals:
  void click2OnCallsign(QString hiscall, QString t2, bool ctrl);
  void errorOccurred(const QString &error);  // Emitted on error
  void sendLocalStationData2(QString const& call, QString const& grid, QString const& theUrl);
  void sendRemoteStationData2 (QByteArray const& postByteArray, QString const& theUrl);

protected:  
  void closeEvent(QCloseEvent *event);
  
private slots:
  void selectCallsign2(bool ctrl);
  void on_cbCQ_toggled(bool checked);
  void on_cbCQstar_toggled(bool checked);
  void onFinished(QNetworkReply *reply);     // Handles the reply from web request

private:
  Ui::Messages *ui;
  QString m_settings_filename;
  QString m_t;
  QString m_t2;
  QString m_colorBackground;
  QString m_color0;
  QString m_color1;
  QString m_color2;
  QString m_color3;
  
  QThread* livecqThread; 
  std::unique_ptr<PSKReporter> m_psk_reporter;

  bool m_closingForShutdown = false;
  bool m_cqOnly;
  bool m_cqStarOnly;
  bool doLiveCQ=true; //liveCQ
  void CreateLiveCQ(QStringList cqliveText);  //liveCQ
  void sendPSKReporterData(QStringList decodeList);  //PSKReporter
  void sendLiveCQData(QStringList decodeList);  // This will trigger the web request
  void initializePSKReporting();
  bool m_spot_to_psk_reporter {false};

  QString w3szUrlAddr="https://w3sz.com/livecq_update.php"; //liveCQ

};

#endif
