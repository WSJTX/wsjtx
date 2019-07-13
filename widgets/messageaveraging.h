// -*- Mode: C++ -*-
#ifndef MESSAGEAVERAGING_H_
#define MESSAGEAVERAGING_H_

#include <QWidget>

class QSettings;
class QFont;

namespace Ui {
  class MessageAveraging;
}

class MessageAveraging
  : public QWidget
{
  Q_OBJECT

public:
  explicit MessageAveraging(QSettings *, QFont const&, QWidget * parent = 0);
  ~MessageAveraging();
  void displayAvg(QString const&);
  void changeFont (QFont const&);

private:
  void read_settings ();
  void write_settings ();
  void setContentFont (QFont const&);
  QSettings * settings_;

  QScopedPointer<Ui::MessageAveraging> ui;
};

#endif
