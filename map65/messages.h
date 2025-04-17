#ifndef MESSAGES_H
#define MESSAGES_H

#include <QDialog>
#include "commons.h"

namespace Ui {
  class Messages;
}

class Messages : public QDialog
{
  Q_OBJECT

public:
  explicit Messages (QString const& settings_filename, QWidget * parent = nullptr);
  void setText(QString t, QString t2);
  void setColors(QString t);

  ~Messages();

signals:
  void click2OnCallsign(QString hiscall, QString t2, bool ctrl);

private slots:
  void selectCallsign2(bool ctrl);
  void on_cbCQ_toggled(bool checked);
  void on_cbCQstar_toggled(bool checked);
  void handleReply(); //liveCQ

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

  bool m_cqOnly;
  bool m_cqStarOnly;
  bool doLiveCQ=true; //liveCQ
  void CreateLiveCQ(QStringList cqliveText);  //liveCQ
  void sendLiveCQData(QStringList decodeList);  //liveCQ

  QString w3szUrlAddr="https://w3sz.com/livecq_update.php"; //liveCQ

};

#endif
