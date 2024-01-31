#ifndef ASTRO_H
#define ASTRO_H

#include <QWidget>
#include <QDateTime>

namespace Ui {
  class Astro;
}

class Astro : public QWidget
{
  Q_OBJECT

public:
  explicit Astro (QString const& settings_filename, QWidget *parent = 0);
  void astroUpdate(QDateTime t, QString mygrid, QString azelDir, double xavg);
  void setFontSize(int n);
  int getSelfDop();

  ~Astro ();

private slots:
  void on_cbOnOff_clicked(bool checked);
  void on_cbAutoCycle_clicked(bool checked);

private:
  Ui::Astro *ui;
  QString m_settings_filename;
  QString m_AzElDir0;

  qint32  m_ndop00=0;
};

#endif
