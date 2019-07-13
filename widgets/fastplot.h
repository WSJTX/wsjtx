///////////////////////////////////////////////////////////////////////////
// Some code in this file and accompanying files is based on work by
// Moe Wheatley, AE4Y, released under the "Simplified BSD License".
// For more details see the accompanying file LICENSE_WHEATLEY.TXT
///////////////////////////////////////////////////////////////////////////

#ifndef FPLOTTER_H_
#define FPLOTTER_H_

#include <QFrame>
#include <QString>
#include <QPixmap>
#include <QVector>
#include <QColor>

class QMouseEvent;

class FPlotter : public QFrame
{
  Q_OBJECT
public:
  explicit FPlotter(QWidget *parent = 0);
  ~FPlotter();

  qint32  m_w;
  qint32  m_plotZero;
  qint32  m_plotGain;
  qint32  m_greenGain;
  qint32  m_greenZero;
  qint32  m_x0;
  qint32  m_x1;
  qint32  m_y0;
  qint32  m_UTCdisk;
  bool    m_diskData;

  void draw();		                                    //Update the Fast plot
  void setPlotZero(int plotZero);
  void setPlotGain(int plotGain);
  void setGreenZero(int n);
  void setTRperiod(double p);
  void drawScale();
  void setMode(QString mode);

signals:
  void fastPick (int x0, int x1, int y);

protected:
  //re-implemented widget event handlers
  void paintEvent(QPaintEvent *event);
//  void resizeEvent(QResizeEvent* event);

private slots:
  void mousePressEvent(QMouseEvent *event);
  void mouseMoveEvent(QMouseEvent *event);

private:

  void MakeTimeStrs();
  int XfromTime(float t);
  float TimefromX(int x);
  qint64 RoundFreq(qint64 freq, int resolution);

  QPixmap m_ScalePixmap;
  QString m_HDivText[483];
  QString m_t;
  QString m_t0;
  QString m_t1;
  QString m_mode;

  double  m_pixPerSecond;
  double  m_TRperiod;

  qint32  m_hdivs;
  qint32  m_h;
  qint32  m_h1;
  qint32  m_h2;
  QPixmap m_HorizPixmap;
  qint32  m_jh0;

  bool    m_bPaint2;
};

extern float fast_green[703];
extern float fast_green2[703];
extern float fast_s[44992];                                    //44992=64*703
extern float fast_s2[44992];
extern int   fast_jh;
extern int   fast_jh2;
extern QVector<QColor> g_ColorTbl;

#endif // FPLOTTER_H
