///////////////////////////////////////////////////////////////////////////
// Some code in this file and accompanying files is based on work by
// Moe Wheatley, AE4Y, released under the "Simplified BSD License".
// For more details see the accompanying file LICENSE_WHEATLEY.TXT
///////////////////////////////////////////////////////////////////////////

#ifndef PLOTTER_H
#define PLOTTER_H

#include <QtGui>
#include <QFrame>
#include <QImage>
#include <QToolTip>
#include <cstring>
#include "commons.h"

#define VERT_DIVS 7	//specify grid screen divisions
#define HORZ_DIVS 20

class CPlotter : public QFrame
{
  Q_OBJECT
public:
  explicit CPlotter(QWidget *parent = 0);
  ~CPlotter();

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;
  QColor  m_ColorTbl[256];
  bool    m_bDecodeFinished;
  int     m_plotZero;
  int     m_plotGain;
  float   m_fSpan;
  qint32  m_nSpan;
  qint32  m_binsPerPixel;
  qint32  m_fQSO;
  qint32  m_DF;
  qint32  m_tol;
  qint32  m_fCal;

  void draw(float sw[], int i0, float splot[]);		//Update the waterfalls
  void SetRunningState(bool running);
  void setPlotZero(int plotZero);
  int  getPlotZero();
  void setPlotGain(int plotGain);
  int  getPlotGain();
  void SetCenterFreq(int f);
  qint64 centerFreq();
  void SetStartFreq(quint64 f);
  qint64 startFreq();
  void SetFreqOffset(quint64 f);
  qint64 freqOffset();
  int  plotWidth();
  void setNSpan(int n);
  void UpdateOverlay();
  void setDataFromDisk(bool b);
  void setTol(int n);
  void setBinsPerPixel(int n);
  int  binsPerPixel();
  void setFQSO(int n, bool bf);
  void setFcal(int n);
  void setNkhz(int n);
  void DecodeFinished();
  void DrawOverlay();
  int  fQSO();
  int  DF();
  int  autoZero();
  void setPalette(QString palette);
  void setFsample(int n);
  void setMode65(int n);
  void set2Dspec(bool b);
  double fGreen();
  void setLockTxRx(bool b);
  double rxFreq();
  double txFreq();
//  void updateFreqLabel();

signals:
  void freezeDecode0(int n);
  void freezeDecode1(int n);

protected:
  //re-implemented widget event handlers
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mouseMoveEvent(QMouseEvent * event) override;

private:

  void MakeFrequencyStrs();
  void UTCstr();
  int XfromFreq(float f);
  float FreqfromX(int x);
  qint64 RoundFreq(qint64 freq, int resolution);

  QPixmap m_WaterfallPixmap;
  QPixmap m_ZoomWaterfallPixmap;
  QPixmap m_2DPixmap;
  unsigned char m_zwf[32768*400];
  QPixmap m_ScalePixmap;
  QPixmap m_ZoomScalePixmap;
  QSize   m_Size;
  QString m_Str;
  QString m_HDivText[483];
  bool    m_Running;
  bool    m_paintEventBusy;
  bool    m_2Dspec;
  bool    m_paintAllZoom;
  bool    m_bLockTxRx;
  double  m_CenterFreq;
  double  m_fGreen;
  double  m_TXfreq;
  qint64  m_StartFreq;
  qint64  m_ZoomStartFreq;
  qint64  m_FreqOffset;
  qint32  m_dBStepSize;
  qint32  m_FreqUnits;
  qint32  m_hdivs;
  bool    m_dataFromDisk;
  QString m_sutc;
  qint32  m_line;
  qint32  m_hist1[256];
  qint32  m_hist2[256];
  qint32  m_z1;
  qint32  m_z2;
  qint32  m_nkhz;
  qint32  m_fSample;
  qint32  m_mode65;
  qint32  m_i0;
  qint32  m_xClick;
  qint32  m_TXkHz;
  qint32  m_TxDF;

private slots:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
};

#endif // PLOTTER_H
