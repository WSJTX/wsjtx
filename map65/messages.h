#ifndef MESSAGES_H
#define MESSAGES_H

#include <QDialog>

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
};

#endif
