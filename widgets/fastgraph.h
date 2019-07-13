#ifndef FASTGRAPH_H_
#define FASTGRAPH_H_

#include <QDialog>
#include <QScopedPointer>

namespace Ui {
  class FastGraph;
}

class QSettings;
class QCloseEvent;
class QKeyEvent;

class FastGraph : public QDialog
{
  Q_OBJECT

public:
  explicit FastGraph(QSettings *, QWidget *parent = 0);
  ~FastGraph ();

  void   plotSpec(bool diskData, int UTCdisk);
  void   saveSettings();
  void   setTRPeriod(double p);
  void   setMode(QString mode);

signals:
  void fastPick(int x0, int x1, int y);

private slots:
  void on_gainSlider_valueChanged(int value);
  void on_zeroSlider_valueChanged(int value);  
  void on_greenZeroSlider_valueChanged(int value);
  void on_pbAutoLevel_clicked();

protected:
  void closeEvent (QCloseEvent *) override;
  void keyPressEvent( QKeyEvent *e ) override;

private:
  QSettings * m_settings;
  float   m_ave;
  double  m_TRperiod;

  QScopedPointer<Ui::FastGraph> ui;
};

extern float fast_green[703];
extern int   fast_jh;

#endif // FASTGRAPH_H
