// -*- Mode: C++ -*-
#ifndef ARRL_DIGI_H_
#define ARRL_DIGI_H_

#include <QWidget>

class QSettings;
class QFont;

namespace Ui {
  class ActiveStations;
}

class ActiveStations
  : public QWidget
{
  Q_OBJECT

public:
  explicit ActiveStations(QSettings *, QFont const&, QWidget * parent = 0);
  ~ActiveStations();
  void displayRecentStations(QString mode, QString const&);
  void changeFont (QFont const&);
  int  maxRecent();
  int  maxAge();
  void setClickOK(bool b);
  void erase();
  bool readyOnly();
  void setRate(int n);
  void setBandChanges(int n);
  void setScore(int n);

  bool m_clickOK=false;
  bool m_bReadyOnly;

private:
  void read_settings ();
  void write_settings ();
  Q_SIGNAL void callSandP(int nline);
  Q_SIGNAL void activeStationsDisplay();
  Q_SIGNAL void cursorPositionChanged();

  Q_SLOT void on_cbReadyOnly_toggled(bool b);
  Q_SLOT void on_textEdit_clicked();

//  qint64 m_msec0=0;
  QString m_mode="";
  QSettings * settings_;

  QScopedPointer<Ui::ActiveStations> ui;
};

#endif
