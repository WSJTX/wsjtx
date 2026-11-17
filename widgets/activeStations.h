// -*- Mode: C++ -*-
#ifndef ARRL_DIGI_H_
#define ARRL_DIGI_H_

#include <QWidget>
#include <QMap>
#include <QDate>
#include <QSet>

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
  enum class DisplayMode { Standard, Q65, Q65Pileup, Fox };

  explicit ActiveStations(QSettings *, QFont const&, QWidget * parent = 0);
  ~ActiveStations();
  void displayRecentStations(DisplayMode, QString const&);
  void setupUi(DisplayMode);
  void changeFont (QFont const&);
  int  maxRecent();
  int  maxAge();
  void setClickOK(bool b);
  void erase();
  bool readyOnly();
  bool wantedOnly();
  // Days (UTC) whose QSOs count as worked for #, Wanted only and QSOs (Q65
  // list); empty (= every QSO counts) when the "on days" box is not checked.
  QSet<QDate> wantedDays() const;
  // The chosen days themselves, whether or not "on days" is checked (the
  // ARRL EME Cabrillo dialog's first suggestion; it keeps its own after).
  QSet<QDate> chosenDays() const;
  void setRate(int n);
  void setBandChanges(int n);
  void setScore(int n);
  void clearStations();
  void addLine(QString);

  bool m_clickOK=false;
  bool m_bReadyOnly;
  bool m_bWantedOnly;

private:
  void read_settings ();
  void write_settings ();
  Q_SIGNAL void callSandP(int nline);
  Q_SIGNAL void activeStationsDisplay();
  Q_SIGNAL void cursorPositionChanged();
  Q_SIGNAL void queueActiveWindowHound(QString text);

  Q_SLOT void on_cbReadyOnly_toggled(bool b);
  Q_SLOT void on_cbWantedOnly_toggled(bool b);
  Q_SLOT void wantedDaysChanged();
  Q_SLOT void on_textEdit_clicked();
  void chooseWantedDays();
  void showWantedDays();

  QSet<QDate> m_wantedDays;                     // chosen days, kept while unchecked

  DisplayMode m_displayMode = DisplayMode::Standard;
  QSettings * settings_;
  QString m_textbuffer="";                      // F/H mode band decodes
  QMap<int, QString> m_decodes_by_frequency;    // store decodes for F/H band awareness by frequency

  QScopedPointer<Ui::ActiveStations> ui;
};

#endif
