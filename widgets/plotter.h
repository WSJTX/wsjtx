// -*- Mode: C++ -*-
///////////////////////////////////////////////////////////////////////////
// Some code in this file and accompanying files is based on work by
// Moe Wheatley, AE4Y, released under the "Simplified BSD License".
// For more details see the accompanying file LICENSE_WHEATLEY.TXT
///////////////////////////////////////////////////////////////////////////

#ifndef PLOTTER_H_
#define PLOTTER_H_

#include <QFrame>
#include <QSize>
#include <QImage>
#include <QVector>
#include <QColor>
#include <QToolTip>

#define VERT_DIVS 7	//specify grid screen divisions
#define HORZ_DIVS 20

extern bool g_single_decode;

class QAction;

class CPlotter : public QFrame
{
  Q_OBJECT

public:
  explicit CPlotter(QWidget *parent = 0);
  ~CPlotter();

  QSize minimumSizeHint() const Q_DECL_OVERRIDE;
  QSize sizeHint() const Q_DECL_OVERRIDE;

  void draw(float swide[], bool bScroll, bool bRed);		//Update the waterfall
  void drawTotalPower(float pdB);
  void replot();
  void SetRunningState(bool running);
  void setPlotZero(int plotZero);
  int  plotZero();
  void setPlotGain(int plotGain);
  int  plotGain();
  int  plot2dGain();
  void setPlot2dGain(int n);
  int  plot2dZero();
  void setPlot2dZero(int plot2dZero);
  void setStartFreq(int f);
  int startFreq();
  int  plotWidth();
  void UpdateOverlay();
  void setDataFromDisk(bool b);
  void setRxRange(int fMin);
  void setBinsPerPixel(int n);
  int  binsPerPixel();
  void setWaterfallAvg(int n);
  void setRxFreq(int n);
  void DrawOverlay();
  int  rxFreq();
  void setFsample(int n);
  void setNsps(double trperiod, int nsps);
  void setTxFreq(int n);
  void setMode(QString mode);
  void setSubMode(int n);
  void SetPercent2DScreen(int percent);
  int  Fmax();
  void setDialFreq(double d);
  void setCurrent(bool b) {m_bCurrent = b;}
  bool current() const {return m_bCurrent;}
  void setCumulative(bool b) {m_bCumulative = b;}
  bool cumulative() const {return m_bCumulative;}
  void setLinearAvg(bool b) {m_bLinearAvg = b;}
  bool linearAvg() const {return m_bLinearAvg;}
  void setBreadth(qint32 w) {m_w = w;}
  qint32 breadth() const {return m_w;}
  float fSpan() const {return m_fSpan;}
  void setColours(QVector<QColor> const& cl);
  void setTimestamp(int n);
  void setBars(bool b);
  void showFreq(bool b);
  void setFlatten(bool b1, bool b2);
  void setTol(int n);
  void setRxBand(QString band);
  void setReference(bool b) {m_bReference = b;}
  bool Reference() const {return m_bReference;}
  void setQ65_Sync(bool b) {m_bQ65_Sync = b;}
  bool Q65_Sync() const {return m_bQ65_Sync;}
  void setTotalPower(bool b) {m_bTotalPower = b;}
  bool TotalPower() const {return m_bTotalPower;}
  void drawRed(int ia, int ib, float swide[]);
  void setVHF(bool bVHF);
  void setRedFile(QString fRed);
  void setFST4_FreqRange(int fLow,int fHigh);
  void setSingleDecode(bool b);
  void setDiskUTC(int nutc);
  void restartTotalPower();
  bool scaleOK () const {return m_bScaleOK;}
  void setDarkStyle(bool b);

signals:
  void freezeDecode1(int n);
  void setFreq1(int rxFreq, int txFreq);

protected:
  //re-implemented widget event handlers
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mouseMoveEvent(QMouseEvent * event) override;
  void mouseReleaseEvent (QMouseEvent * event) override;
  void mouseDoubleClickEvent (QMouseEvent * event) override;

private:

  void MakeFrequencyStrs();
  int XfromFreq(float f);
  float FreqfromX(int x);

  QAction * m_set_freq_action;

  bool    m_bScaleOK;
  bool    m_bCurrent;
  bool    m_bCumulative;
  bool    m_bLinearAvg;
  bool    m_bReference;
  bool    m_bReference0;
  bool    m_bQ65_Sync;
  bool    m_bTotalPower;
  bool    m_bVHF;
  bool    m_bSingleDecode;
  bool    m_bFirst=true;
  bool    m_bResized;
  bool	  m_bars;
  bool    m_freq;
  float   m_fSpan;
  float   m_pdB=0.0;
  float   m_vpixperdiv;
  bool	  m_useDarkStyle=false;

  qint32  m_plotZero;
  qint32  m_plotGain;
  qint32  m_plot2dGain;
  qint32  m_plot2dZero;
  qint32  m_binsPerPixel;
  qint32  m_waterfallAvg;
  qint32  m_w;
  qint32  m_Flatten;
  qint32  m_nSubMode;
  qint32  m_ia;
  qint32  m_ib;
  qint32  m_nfa;
  qint32  m_nfb;
  qint32  m_nUTC;
  qint32  m_x=0;
  qint32  m_timestamp;

  QPixmap m_DialOverlayPixmap;
  QPixmap m_HoverOverlayPixmap;
  QPixmap m_WaterfallPixmap;
  QPixmap m_2DPixmap;
  QPixmap m_ScalePixmap;
  QPixmap m_OverlayPixmap;
  QPoint  m_pos;
  QSize   m_Size;
  QString m_Str;
  QString m_HDivText[483];
  QString m_mode;
  QString m_mode0;
  QString m_rxBand;
  QString m_redFile;

  bool    m_Running;
  bool    m_paintEventBusy;
  bool    m_dataFromDisk;
  bool    m_bReplot;

  double  m_fftBinWidth;
  double  m_dialFreq;
  double  m_xOffset;
  double  m_TRperiod;

  float   m_sum[2048];

  qint32  m_dBStepSize;
  qint32  m_FreqUnits;
  qint32  m_hdivs;
  qint32  m_line;
  qint32  m_fSample;
  qint32  m_xClick;
  qint32  m_freqPerDiv;
  qint32  m_nsps;
  qint32  m_Percent2DScreen;
  qint32  m_Percent2DScreen0;
  qint32  m_h;
  qint32  m_h1;
  qint32  m_h2;
  qint32  m_rxFreq;
  qint32  m_rxFreq0=0;
  qint32  m_txFreq;
  qint32  m_fMin;
  qint32  m_fMax;
  qint32  m_startFreq;
  qint32  m_tol;
  qint32  m_lastMouseX;
  qint32  m_lastPaintedX;
  qint32  m_j;
  char    m_sutc[6];

private slots:
  void leaveEvent(QEvent *event) override;

};

extern QVector<QColor> g_ColorTbl;

#endif // PLOTTER_H
