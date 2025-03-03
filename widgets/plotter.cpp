#include "plotter.h"
#include <math.h>
#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QDateTime>
#include <QPen>
#include <QMouseEvent>
#include <QDebug>
#include "qt_helpers.hpp"
#include "commons.h"
#include "moc_plotter.cpp"
#include <fstream>
#include <iostream>

#define MAX_SCREENSIZE 8192

extern "C" {
  void flat4_(float swide[], int* iz, int* nflatten);
  void plotsave_(float swide[], int* m_w , int* m_h1, int* irow);
}

extern dec_data dec_data;

CPlotter::CPlotter(QWidget *parent) :                  //CPlotter Constructor
  QFrame {parent},
//  m_set_freq_action {new QAction {tr ("&Set Rx && Tx Offset"), this}},
  m_bScaleOK {false},
  m_bReference {false},
  m_bReference0 {false},
  m_fSpan {2000.0},
  m_plotZero {0},
  m_plotGain {0},
  m_plot2dGain {0},
  m_plot2dZero {0},
  m_nSubMode {0},
  m_Running {false},
  m_paintEventBusy {false},
  m_fftBinWidth {1500.0/2048.0},
  m_dialFreq {0.},
  m_sum {},
  m_dBStepSize {10},
  m_FreqUnits {1},
  m_hdivs {HORZ_DIVS},
  m_line {0},
  m_fSample {12000},
  m_nsps {6912},
  m_Percent2DScreen {30},      //percent of screen used for 2D display
  m_Percent2DScreen0 {0},
  m_rxFreq {1020},
  m_txFreq {0},
  m_startFreq {0},
  m_lastMouseX {-1},
  m_lastPaintedX {-1}
//  m_tol {100}
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_PaintOnScreen,false);
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setMouseTracking(true);
  m_bReplot=false;

  // contextual pop up menu
//  setContextMenuPolicy (Qt::CustomContextMenu);
//  connect (this, &QWidget::customContextMenuRequested, [this] (QPoint const& pos) {
//      QMenu menu {this};
//      menu.addAction (m_set_freq_action);
//      auto const& connection = connect (m_set_freq_action, &QAction::triggered, [this, pos] () {
//          int newFreq = FreqfromX (pos.x ()) + .5;
//          emit setFreq1 (newFreq, newFreq);
//        });
//      menu.exec (mapToGlobal (pos));
//      disconnect (connection);
//    });
}

CPlotter::~CPlotter() { }                                      // Destructor

QSize CPlotter::minimumSizeHint() const
{
  return QSize(50, 50);
}

QSize CPlotter::sizeHint() const
{
  return QSize(180, 180);
}

void CPlotter::resizeEvent(QResizeEvent* )                    //resizeEvent()
{
  if(!size().isValid()) return;
  if( m_Size != size() or (m_bReference != m_bReference0) or
      m_Percent2DScreen != m_Percent2DScreen0) {
    m_Size = size();
    m_w = m_Size.width();
    m_h = m_Size.height();
    m_h2 = m_Percent2DScreen*m_h/100.0;
    if(m_h2>m_h-30) m_h2=m_h-30;
    if(m_bReference) m_h2=m_h-30;
    if(m_h2<1) m_h2=1;
    m_h1=m_h-m_h2;
//    m_line=0;
    m_DialOverlayPixmap = QPixmap(m_Size.width(), m_h);
    m_DialOverlayPixmap.fill(Qt::transparent);
    m_HoverOverlayPixmap = QPixmap(m_Size.width(), m_h);
    m_HoverOverlayPixmap.fill(Qt::transparent);
    m_2DPixmap = QPixmap(m_Size.width(), m_h2);
    m_2DPixmap.fill(Qt::black);
    m_WaterfallPixmap = QPixmap(m_Size.width(), m_h1);
    m_OverlayPixmap = QPixmap(m_Size.width(), m_h2);
    m_OverlayPixmap.fill(Qt::black);
    m_WaterfallPixmap.fill(Qt::black);
    m_2DPixmap.fill(Qt::black);
    m_ScalePixmap = QPixmap(m_w,30);
    m_ScalePixmap.fill(Qt::white);
    m_Percent2DScreen0 = m_Percent2DScreen;
    m_bResized = true;
    m_vpixperdiv = float(m_h2)/float(VERT_DIVS);
    m_x=0;
  }
  DrawOverlay();
}

void CPlotter::paintEvent(QPaintEvent *)                                // paintEvent()
{
  if(m_paintEventBusy) return;
  m_paintEventBusy=true;
  QPainter painter(this);
  painter.drawPixmap(0,0,m_ScalePixmap);
  painter.drawPixmap(0,30,m_WaterfallPixmap);
  painter.drawPixmap(0,m_h1,m_2DPixmap);
  int x = XfromFreq(m_rxFreq);
  if (m_bars) {
    painter.drawPixmap(0,30,m_DialOverlayPixmap);
    if(m_lastMouseX >= 0 && m_lastMouseX != x){
      painter.drawPixmap(m_lastMouseX, 0, m_HoverOverlayPixmap);
    }
  }
  m_lastPaintedX = m_lastMouseX;
  m_paintEventBusy=false;
}

void CPlotter::draw(float swide[], bool bScroll, bool bRed)
{
  if (!m_TRperiod) return;      // not ready to plot yet
  int j,j0;
  float y,y2,ymin;
  double fac = sqrt(m_binsPerPixel*m_waterfallAvg/15.0);
  double gain = fac*pow(10.0,0.015*m_plotGain);
  double gain2d = pow(10.0,0.02*(m_plot2dGain));

  if(m_bReference != m_bReference0) resizeEvent(NULL);
  m_bReference0=m_bReference;

//move current data down one line (must do this before attaching a QPainter object)
  if(bScroll and !m_bReplot) m_WaterfallPixmap.scroll(0,1,0,0,m_w,m_h1);
  QPainter painter1(&m_WaterfallPixmap);
  if(m_bFirst or bRed or !m_bQ65_Sync or m_mode!=m_mode0
     or m_bResized or m_rxFreq!=m_rxFreq0) {
    m_2DPixmap = m_OverlayPixmap.copy(0,0,m_w,m_h2);
    m_bFirst=false;
    m_bResized=false;
    m_rxFreq0=m_rxFreq;
  }
  m_mode0=m_mode;
  QPainter painter2D(&m_2DPixmap);
  if(!painter2D.isActive()) return;
  QFont Font("Arial");
  Font.setPointSize(12);
  Font.setWeight(QFont::Normal);
  painter2D.setFont(Font);

  if(m_bLinearAvg) {
    painter2D.setPen(Qt::yellow);
  } else if(m_bReference) {
    painter2D.setPen(Qt::blue);
  } else {
    painter2D.setPen(Qt::green);
  }
  static QPoint LineBuf[MAX_SCREENSIZE];
  static QPoint LineBuf2[MAX_SCREENSIZE];
  static QPoint LineBuf3[MAX_SCREENSIZE];
  static QPoint LineBuf4[MAX_SCREENSIZE];
  j=0;
  j0=int(m_startFreq/m_fftBinWidth + 0.5);
  int iz=XfromFreq(5000.0);
  int jz=iz*m_binsPerPixel;
  m_fMax=FreqfromX(iz);
  if(bScroll and swide[0]<1.e29) {
    flat4_(swide,&iz,&m_Flatten);
    if(!m_bReplot) flat4_(&dec_data.savg[j0],&jz,&m_Flatten);
  }

  ymin=1.e30;
  if(swide[0]>1.e29 and swide[0]< 1.5e30) painter1.setPen(Qt::green);
  if(swide[0]>1.4e30) painter1.setPen(Qt::red);
  if(!m_bReplot) {
    m_j=0;
    int irow=-1;
    plotsave_(swide,&m_w,&m_h1,&irow);
  }
  ymin = 0;
  for(int i=0; i<iz; i++) {
    y=swide[i];
    int y1 = 10.0*gain*y + m_plotZero;
    if (y1<0) y1=0;
    if (y1>254) y1=254;
    if (swide[i]<1.e29) painter1.setPen(g_ColorTbl[y1]);
    painter1.drawPoint(i,m_j);
  }
  m_line++;

  float y2min=1.e30;
  float y2max=-1.e30;
  for(int i=0; i<iz; i++) {
    y=swide[i] - ymin;
    y2=0;
    if(m_bCurrent) y2 = gain2d*y + m_plot2dZero;            //Current

    if(bScroll) {
      float sum=0.0;
      int j=j0+m_binsPerPixel*i;
      for(int k=0; k<m_binsPerPixel; k++) {
        sum+=dec_data.savg[j++];
      }
      m_sum[i]=sum;
    }
    if(m_bCumulative) y2=gain2d*(m_sum[i]/m_binsPerPixel + m_plot2dZero);
    if(m_Flatten==0) y2 += 15;                      //### could do better! ###

    if(m_bLinearAvg) {                                   //Linear Avg (yellow)
      float sum=0.0;
      int j=j0+m_binsPerPixel*i;
      for(int k=0; k<m_binsPerPixel; k++) {
        sum+=spectra_.syellow[j++];
      }
      y2=2.0*gain2d*sum/m_binsPerPixel + m_plot2dZero;
    }

    if(m_bReference) {                                   //Reference (red)
      float df_ref=12000.0/6912.0;
      int j=FreqfromX(i)/df_ref + 0.5;
      y2=spectra_.ref[j] + m_plot2dZero;
//      if(gain2d>1.5) y2=spectra_.filter[j] + m_plot2dZero;

    }

    if(i==iz-1 and !m_bQ65_Sync and !m_bTotalPower) {
      painter2D.drawPolyline(LineBuf,j);
    }
    LineBuf[j].setX(i);
    LineBuf[j].setY(int(0.9*m_h2-y2*m_h2/70.0));
    if(y2<y2min) y2min=y2;
    if(y2>y2max) y2max=y2;
    j++;
  }
  if(m_bReplot and m_mode!="Q65") return;

  if(swide[0]>1.0e29) m_line=0;
  if(m_mode=="FT4" and m_line==34) m_line=0;
  if(m_line == painter1.fontMetrics ().height () && m_timestamp!=0) {
    painter1.setPen(Qt::white);
    QString t;
    if(m_nUTC<0) {
      auto start = qt_truncate_date_time_to (QDateTime::currentDateTimeUtc(), m_TRperiod * 1e3)
        .toString (m_TRperiod < 60. ? "hh:mm:ss" : "hh:mm");
      t = QString {"%1    %2"}.arg (start, m_rxBand);
    } else {
      auto hr = m_nUTC / 10000;
      auto start = QTime {hr, (m_nUTC - 10000 * hr) / 100, m_nUTC % 100}
         .toString (m_TRperiod < 60. ? "hh:mm:ss" : "hh:mm");
      t = QString {"%1    %2"}.arg (start).arg (m_rxBand);
    }
    QRect rect{5, -2, m_w-10, painter1.fontMetrics().ascent()};
    QRect boundingRect;
    painter1.drawText(rect, m_timestamp==2?0x0082:0x0081,t, &boundingRect);
  }

  if(m_mode=="JT4" or (m_mode=="Q65" and m_nSubMode>=3)) {
    DrawOverlay();
    QPen pen3(Qt::yellow);                     //Mark freqs of JT4/Q65 single-tone msgs
    painter2D.setPen(pen3);
    Font.setWeight(QFont::Bold);
    painter2D.setFont(Font);
    int x1=XfromFreq(m_rxFreq);
    y=0.25*m_h2;
    painter2D.drawText(x1-4,y,"T");
    x1=XfromFreq(m_rxFreq+250);
    painter2D.drawText(x1-4,y,"M");
    x1=XfromFreq(m_rxFreq+500);
    painter2D.drawText(x1-4,y,"R");
    x1=XfromFreq(m_rxFreq+750);
    painter2D.drawText(x1-4,y,"73");
  }

  if(bRed and m_bQ65_Sync) {      //Plot the Q65 orange (current) and red (average) sync curves
    int k=0;
    int k2=0;
    std::ifstream f;
    f.open(m_redFile.toLatin1());
    if(f) {
      int x,y;
      float freq,xdt,smin,smax,sync_avg,sync_current;
      f >> xdt >> smin >> smax;
      if(f) {
        for(int i=0; i<99999; i++) {
          f >> freq >> sync_avg >> sync_current;
          if(!f or f.eof() or k>=MAX_SCREENSIZE or k2>=MAX_SCREENSIZE) break;
          x=XfromFreq(freq);
          // Plot the red curve only if we have averaged 2 or more Rx sequences.
          if(sync_avg > -99.0 and (smin!=0.0 or smax != 0.0)) {
            y=m_h2*(0.9 - 0.09*gain2d*gain2d*sync_avg) - m_plot2dZero - 10;
            LineBuf2[k2].setX(x);                            //Red sync curve
            LineBuf2[k2].setY(y);
            k2++;
          }
          y=m_h2*(0.9 - 0.09*gain2d*gain2d*sync_current) - m_plot2dZero;
          LineBuf3[k].setX(x);                            //Orange sync curve
          LineBuf3[k].setY(y);
          k++;
        }
      }
      f.close();
      QPen pen0(Qt::red,2);
      painter2D.setPen(pen0);
      if(smin!=0.0 or smax != 0.0) {
        painter2D.drawPolyline(LineBuf2,k2);
      }
      pen0.setColor("orange");
      painter2D.setPen(pen0);
      painter2D.drawPolyline(LineBuf3,k);
      QString t;
      t = t.asprintf("DT = %6.2f",xdt);
      painter2D.setPen(Qt::white);
      Font.setWeight(QFont::Bold);
      painter2D.setFont(Font);
      painter2D.drawText(m_w-100,m_h2/2,t);
    }
  }

  if(m_bTotalPower and m_pdB>1.0) {
    painter2D.setPen(Qt::green);
    if(m_x==m_w-1) {
      for (int i=0; i<m_w-1; i++) {
        LineBuf4[i].setY(LineBuf4[i+1].y());
      }
    }
    int yy=m_h2 - 0.1*m_vpixperdiv*(m_pdB-20.0);
    LineBuf4[m_x].setX(m_x);
    LineBuf4[m_x].setY(yy);
    if(LineBuf4[m_w-1].y()==0) LineBuf4[m_w-1].setY(yy);
    painter2D.drawPolyline(LineBuf4,m_x);
    if(m_x < m_w-1) m_x++;
  }

  update();                                    //trigger a new paintEvent
  m_bScaleOK=true;
}

void CPlotter::drawRed(int ia, int ib, float swide[])
{
  m_ia=ia;
  m_ib=ib;
  draw(swide,false,true);
}

void CPlotter::replot()
{
  resizeEvent(NULL);
  float swide[m_w];
  m_bReplot=true;
  for(int irow=0; irow<m_h1; irow++) {
    m_j=irow;
    plotsave_(swide,&m_w,&m_h1,&irow);
    draw(swide,false,false);
  }
  if(m_mode=="Q65" and m_bQ65_Sync) {
    draw(swide,false,true);
  }
  update();                                    //trigger a new paintEvent
  m_bReplot=false;
}

void CPlotter::DrawOverlay()                   //DrawOverlay()
{
  if(m_OverlayPixmap.isNull()) return;
  if(m_WaterfallPixmap.isNull()) return;
  int w = m_WaterfallPixmap.width();
  int x,y,x1,x2,x3,x4,x5,x6;
  float pixperdiv;

  double df = m_binsPerPixel*m_fftBinWidth;
  QPen penOrange(QColor(255,165,0),3);
  QPen penGreen(QColor(15,153,105), 3);        //Mark Tol range or BW with dark green line
  if(m_useDarkStyle) penGreen.setBrush(Qt::green);   // lighter green for dark style
  QPen penRed(Qt::red, 3);                     //Mark Tx freq with red
  QPainter painter(&m_OverlayPixmap);
  painter.setBackground (palette ().brush (backgroundRole ()));
  QLinearGradient gradient(0, 0, 0 ,m_h2);     //fill background with gradient
  gradient.setColorAt(1, Qt::black);
  gradient.setColorAt(0, Qt::darkBlue);
  painter.setBrush(gradient);
  painter.drawRect(0, 0, m_w, m_h2);
  painter.setBrush(Qt::SolidPattern);

  m_fSpan = w*df;
//  int n=m_fSpan/10;
  m_freqPerDiv=10;
  if(m_fSpan>100) m_freqPerDiv=20;
  if(m_fSpan>250) m_freqPerDiv=50;
  if(m_fSpan>500) m_freqPerDiv=100;
  if(m_fSpan>1000) m_freqPerDiv=200;
  if(m_fSpan>2500) m_freqPerDiv=500;

  if(!m_bTotalPower) {
    pixperdiv = m_freqPerDiv/df;
    m_hdivs = w*df/m_freqPerDiv + 1.9999;
    float xx0=float(m_startFreq)/float(m_freqPerDiv);
    xx0=xx0-int(xx0);
    int x0=xx0*pixperdiv+0.5;
    for( int i=1; i<m_hdivs; i++) {                 //draw vertical grids
      x = (int)((float)i*pixperdiv ) - x0;
      if(x >= 0 and x<=m_w) {
        painter.setPen(QPen(Qt::white, 1,Qt::DotLine));
        painter.drawLine(x, 0, x , m_h2);
      }
    }
  }

  painter.setPen(QPen(Qt::white, 1,Qt::DotLine));
  if(m_bTotalPower) painter.setPen(QPen(Qt::white, 1,Qt::DashLine));
  for( int i=1; i<VERT_DIVS; i++) {                 //draw horizontal grids
    y = int(i*m_vpixperdiv);
    if(m_bTotalPower) {
        painter.drawLine(15, y, w, y);
    } else {
        painter.drawLine(0, y, w, y);
    }
  }

  if(m_bTotalPower) {
    painter.setPen(QPen(Qt::white));
    for( int i=1; i<VERT_DIVS; i++) {               //draw horizontal grids
      y = int(i*m_vpixperdiv);
      painter.drawText(0,y+5,QString::number(10*(VERT_DIVS-i) + 20));
    }
  }

  if(m_bTotalPower and m_h2>100) {
    painter.setPen(QPen(Qt::white, 1,Qt::DotLine));
    for( int i=1; i<5*VERT_DIVS; i++) {             //draw horizontal 2 dB grids
      if(i%5 > 0) {
        y = int(0.2*i*m_vpixperdiv);
        painter.drawLine(0, y, w, y);
      }
    }
  }

  QRect rect0;
  QPainter painter0(&m_ScalePixmap);
  painter0.setBackground (palette ().brush (backgroundRole ()));

  //create Font to use for scales
  QFont Font("Arial");
  Font.setPointSize(12);
  Font.setWeight(QFont::Normal);
  painter0.setFont(Font);
  if (m_useDarkStyle) {
      painter0.setPen(Qt::white);
  } else {
      painter0.setPen(Qt::black);
  }

  if(m_binsPerPixel < 1) m_binsPerPixel=1;
  m_hdivs = w*df/m_freqPerDiv + 0.9999;

  if (m_useDarkStyle) {
      m_ScalePixmap.fill(Qt::black);
  } else {
      m_ScalePixmap.fill(Qt::white);
  }
  painter0.drawRect(0, 0, w, 30);
  MakeFrequencyStrs();

//draw tick marks on upper scale
  pixperdiv = m_freqPerDiv/df;
  for( int i=0; i<m_hdivs; i++) {                    //major ticks
    x = (int)((m_xOffset+i)*pixperdiv );
    painter0.drawLine(x,18,x,30);
  }
  int minor=5;
  if(m_freqPerDiv==200) minor=4;
  for( int i=1; i<minor*m_hdivs; i++) {             //minor ticks
    x = i*pixperdiv/minor;
    painter0.drawLine(x,24,x,30);
  }

  //draw frequency values
  for( int i=0; i<=m_hdivs; i++) {
    x = (int)((m_xOffset+i)*pixperdiv - pixperdiv/2);
    if(int(x+pixperdiv/2) > 70) {
      rect0.setRect(x,0, (int)pixperdiv, 20);
      painter0.drawText(rect0, Qt::AlignHCenter|Qt::AlignVCenter,m_HDivText[i]);
    }
  }

  float bw=9.0*12000.0/m_nsps;               //JT9
  if(m_mode=="FT4") bw=3*12000.0/576.0;      //FT4  ### (3x, or 4x???) ###
  if(m_mode=="FT8") bw=7*12000.0/1920.0;     //FT8
  if(m_mode.startsWith("FST4")) {
    int h=int(pow(2.0,m_nSubMode));
    int nsps=800;
    if(m_TRperiod==30) nsps=1680;
    if(m_TRperiod==60) nsps=4000;
    if(m_TRperiod==120) nsps=8400;
    if(m_TRperiod==300) nsps=21504;
    if(m_TRperiod==900) nsps=66560;
    if(m_TRperiod==1800) nsps=134400;
    float baud=12000.0/nsps;
    bw=3.0*h*baud;
  }
  if(m_mode=="JT4") {                        //JT4
    bw=3*11025.0/2520.0;                     //Max tone spacing (3/4 of actual BW)
    if(m_nSubMode==1) bw=2*bw;
    if(m_nSubMode==2) bw=4*bw;
    if(m_nSubMode==3) bw=9*bw;
    if(m_nSubMode==4) bw=18*bw;
    if(m_nSubMode==5) bw=36*bw;
    if(m_nSubMode==6) bw=72*bw;

    painter0.setPen(penGreen);
    x1=XfromFreq(m_rxFreq-m_tol);
    x2=XfromFreq(m_rxFreq+m_tol);
    painter0.drawLine(x1,29,x2,29);
    for(int i=0; i<4; i++) {
      x1=XfromFreq(m_rxFreq+bw*i/3.0);
      int j=24;
      if(i==0) j=18;
      painter0.drawLine(x1,j,x1,30);
    }
    painter0.setPen(penRed);
    for(int i=0; i<4; i++) {
      x1=XfromFreq(m_txFreq+bw*i/3.0);
      painter0.drawLine(x1,12,x1,18);
    }
  }

  if(m_mode=="JT9" and m_nSubMode>0) {     //JT9
    bw=8.0*12000.0/m_nsps;
    if(m_nSubMode==1) bw=2*bw;   //B
    if(m_nSubMode==2) bw=4*bw;   //C
    if(m_nSubMode==3) bw=8*bw;   //D
    if(m_nSubMode==4) bw=16*bw;  //E
    if(m_nSubMode==5) bw=32*bw;  //F
    if(m_nSubMode==6) bw=64*bw;  //G
    if(m_nSubMode==7) bw=128*bw; //H
  }

  if(m_mode=="Q65") {                      //Q65
    int h=int(pow(2.0,m_nSubMode));
    int nsps=1800;
    if(m_TRperiod==30) nsps=3600;
    if(m_TRperiod==60) nsps=7200;
    if(m_TRperiod==120) nsps=16000;
    if(m_TRperiod==300) nsps=41472;
    float baud=12000.0/nsps;
    bw=65.0*h*baud;
  }
  if(m_mode=="JT65") {                     //JT65
    bw=65.0*11025.0/4096.0;
    if(m_nSubMode==1) bw=2*bw;   //B
    if(m_nSubMode==2) bw=4*bw;   //C
  }

  painter0.setPen(penGreen);
  if(m_mode=="WSPR") {
    x1=XfromFreq(1400);
    x2=XfromFreq(1600);
    painter0.drawLine(x1,26,x2,26);
  }

  if(m_mode=="FST4W") {
    x1=XfromFreq(m_rxFreq-m_tol);
    x2=XfromFreq(m_rxFreq+m_tol);
    painter0.drawLine(x1,26,x2,26);
  }

  if(m_mode=="FreqCal") {                   //FreqCal
    x1=XfromFreq(m_rxFreq-m_tol);
    x2=XfromFreq(m_rxFreq+m_tol);
    painter0.drawLine(x1,29,x2,29);
    x1=XfromFreq(m_rxFreq);
    painter0.drawLine(x1,24,x1,30);
  }

  int yh=5;
  int yTxTop=12;
  int yRxBottom=yTxTop + 2*yh + 4;
  if(m_mode=="JT9" or m_mode=="JT65" or m_mode=="Q65" or m_mode=="FT8"
     or m_mode=="FT4" or m_mode.startsWith("FST4")) {

    if(m_mode=="FST4" and !m_bSingleDecode) {
      x1=XfromFreq(m_nfa);
      x2=XfromFreq(m_nfb);
      painter0.drawLine(x1,25,x1+5,30);   // Mark FST4 F_Low
      painter0.drawLine(x1,25,x1+5,20);
      painter0.drawLine(x2,25,x2-5,30);   // Mark FST4 F_High
      painter0.drawLine(x2,25,x2-5,20);
    }

    if(m_mode=="Q65" or (m_mode=="JT65" and m_bVHF) or (m_mode=="FT8" and m_bSuperHound)) {
      painter0.setPen(penGreen);
      x1=XfromFreq(m_rxFreq-m_tol);
      x2=XfromFreq(m_rxFreq+m_tol);
      painter0.drawLine(x1,26,x2,26);
      x1=XfromFreq(m_rxFreq);
      painter0.drawLine(x1,20,x1,26);

      if(m_mode=="JT65") {
        painter0.setPen(penOrange);
        x3=XfromFreq(m_rxFreq+20.0*bw/65.0);    //RO
        painter0.drawLine(x3,20,x3,26);
        x4=XfromFreq(m_rxFreq+30.0*bw/65.0);    //RRR
        painter0.drawLine(x4,20,x4,26);
        x5=XfromFreq(m_rxFreq+40.0*bw/65.0);    //73
        painter0.drawLine(x5,20,x5,26);
      }
      painter0.setPen(penGreen);
      x6=XfromFreq(m_rxFreq+bw);             //Highest tone
      if(m_mode=="FT8" and m_bSuperHound) x6=XfromFreq(m_rxFreq+1500.0);
      painter0.drawLine(x6,20,x6,26);

    } else {
      // Draw the green goal post
      painter0.setPen(penGreen);
      x1=XfromFreq(m_rxFreq);
      x2=XfromFreq(m_rxFreq+bw);
      painter0.drawLine(x1,yRxBottom-yh,x1,yRxBottom);
      painter0.drawLine(x1,yRxBottom,x2,yRxBottom);
      painter0.drawLine(x2,yRxBottom-yh,x2,yRxBottom);
      if(m_mode.startsWith("FST4")) {
        x1=XfromFreq(m_rxFreq-m_tol);
        x2=XfromFreq(m_rxFreq+m_tol);
        painter0.drawLine(x1,26,x2,26);   // Mark the Tol range
      }
    }
  }

  QPainter overPainter(&m_DialOverlayPixmap);
  if (m_bars) {
    if (!overPainter.isActive()) overPainter.begin(this);
    overPainter.setCompositionMode(QPainter::CompositionMode_Source);
    overPainter.fillRect(0, 0, m_Size.width(), m_h, Qt::transparent);
  }

  if(m_mode.startsWith("FT") or m_mode.startsWith("JT") or m_mode=="Q65" or m_mode.startsWith("FST4")) {
    x1=XfromFreq(m_rxFreq);
    if(m_mode=="FT8" and m_bSuperHound) {
      x2=XfromFreq(m_rxFreq+1500);
    } else {
      x2=XfromFreq(m_rxFreq+bw);
    }
    if (m_bars) {
      overPainter.setPen(Qt::green);                   // Rx bars
      overPainter.drawLine(x1,0,x1,m_h); overPainter.drawLine(x2,0,x2,m_h);
    }
  }

  // red bars
  if(m_mode.startsWith("FT") or m_mode.startsWith("JT") or m_mode=="Q65" or m_mode.startsWith("FST4")
     or m_mode.mid(0,4)=="WSPR") {
    x1=XfromFreq(m_txFreq); x2=XfromFreq(m_txFreq+bw);
    if(m_mode=="FT8" and m_bSuperFox) x2=XfromFreq(m_txFreq+1500.0);
    if(m_mode=="WSPR") {
      bw=4*12000.0/8192.0;                  //WSPR
      x1=XfromFreq(m_txFreq-0.5*bw); x2=XfromFreq(m_txFreq+0.5*bw);
    }
    if (m_bars) {
      overPainter.setPen(Qt::red);                   // Tx bars
      overPainter.drawLine(x1,0,x1,m_h); overPainter.drawLine(x2,0,x2,m_h);
    }
  }

  // red goal post
  if(m_mode=="JT9" or m_mode=="JT65" or m_mode.mid(0,4)=="WSPR" or m_mode=="Q65"
     or m_mode=="FT8" or m_mode=="FT4" or m_mode.startsWith("FST4")) {
    painter0.setPen(penRed);
    x1=XfromFreq(m_txFreq);
    if(m_mode=="FT8" and m_bSuperFox) {
      x2=XfromFreq(m_txFreq+1500.0);
    } else {
      x2=XfromFreq(m_txFreq+bw);
    }
    if(m_mode=="WSPR") {
      bw=4*12000.0/8192.0;                  //WSPR
      x1=XfromFreq(m_txFreq-0.5*bw);
      x2=XfromFreq(m_txFreq+0.5*bw);
    }
    // Draw the red goal post
    painter0.drawLine(x1,yTxTop,x1,yTxTop+yh);
    painter0.drawLine(x1,yTxTop,x2,yTxTop);
    painter0.drawLine(x2,yTxTop,x2,yTxTop+yh);
  }

  QPainter hoverPainter(&m_HoverOverlayPixmap);
  if (m_bars) {
    if (!hoverPainter.isActive()) hoverPainter.begin(this);
//    if(m_mode=="FT8" and m_bSuperHound) bw=1500;  // not useful for the white bars
    int fwidth=XfromFreq(m_rxFreq+bw)-XfromFreq(m_rxFreq);;
    hoverPainter.setCompositionMode(QPainter::CompositionMode_Source);
    hoverPainter.fillRect(0, 0, m_Size.width(), m_h, Qt::transparent);
    hoverPainter.setPen(QPen(Qt::white));              // white bars
    hoverPainter.drawLine(0, 30, 0, m_h);              // first slot, left line hover
    hoverPainter.drawLine(fwidth, 30, fwidth, m_h);    // first slot, right line hover
  }

  if(m_dialFreq>10.13 and m_dialFreq< 10.15 and m_mode.mid(0,4)!="WSPR" and m_mode!="FST4W") {
    float f1=1.0e6*(10.1401 - m_dialFreq);
    float f2=f1+200.0;
    x1=XfromFreq(f1);
    x2=XfromFreq(f2);
    if(x1<=m_w and x2>=0) {
      painter0.setPen(penOrange);               //Mark WSPR sub-band orange
      painter0.drawLine(x1,9,x2,9);
    }
  }
}

void CPlotter::MakeFrequencyStrs()                       //MakeFrequencyStrs
{
  int f=(m_startFreq+m_freqPerDiv-1)/m_freqPerDiv;
  f*=m_freqPerDiv;
  m_xOffset=float(f-m_startFreq)/m_freqPerDiv;
  for(int i=0; i<=m_hdivs; i++) {
    m_HDivText[i].setNum(f);
    f+=m_freqPerDiv;
  }
}

int CPlotter::XfromFreq(float f)                               //XfromFreq()
{
  int x = int(m_w * (f - m_startFreq)/m_fSpan + 0.5);
  if(x<0 ) return 0;
  if(x>m_w) return m_w;
  return x;
}

float CPlotter::FreqfromX(int x)                               //FreqfromX()
{
  return float(m_startFreq + x*m_binsPerPixel*m_fftBinWidth);
}

void CPlotter::SetRunningState(bool running)              //SetRunningState()
{
  m_Running = running;
}

void CPlotter::setPlotZero(int plotZero)                  //setPlotZero()
{
  m_plotZero=plotZero;
}

int CPlotter::plotZero()                                  //PlotZero()
{
  return m_plotZero;
}

void CPlotter::setPlotGain(int plotGain)                  //setPlotGain()
{
  m_plotGain=plotGain;
}

int CPlotter::plotGain()                                 //plotGain()
{
  return m_plotGain;
}

int CPlotter::plot2dGain()                                //plot2dGain
{
  return m_plot2dGain;
}

void CPlotter::setPlot2dGain(int n)                       //setPlot2dGain
{
  m_plot2dGain=n;
  update();
}

int CPlotter::plot2dZero()                                //plot2dZero
{
  return m_plot2dZero;
}

void CPlotter::setPlot2dZero(int plot2dZero)              //setPlot2dZero
{
  m_plot2dZero=plot2dZero;
}

void CPlotter::setStartFreq(int f)                    //SetStartFreq()
{
  m_startFreq=f;
  m_fMax=FreqfromX(XfromFreq(5000.0));
  resizeEvent(NULL);
  DrawOverlay();
  update();
}

int CPlotter::startFreq()                              //startFreq()
{
  return m_startFreq;
}

int CPlotter::plotWidth(){return m_WaterfallPixmap.width();}     //plotWidth
void CPlotter::UpdateOverlay() {DrawOverlay();}                  //UpdateOverlay
void CPlotter::setDataFromDisk(bool b) {m_dataFromDisk=b;}       //setDataFromDisk

void CPlotter::setRxRange(int fMin)                           //setRxRange
{
  m_fMin=fMin;
}

void CPlotter::setBinsPerPixel(int n)                         //setBinsPerPixel
{
  m_binsPerPixel = n;
  m_fMax=FreqfromX(XfromFreq(5000.0));
  DrawOverlay();                         //Redraw scales and ticks
  update();                              //trigger a new paintEvent}
}

int CPlotter::binsPerPixel()                                   //binsPerPixel
{
  return m_binsPerPixel;
}

void CPlotter::setWaterfallAvg(int n)                         //setNavg
{
  m_waterfallAvg = n;
}

void CPlotter::setRxFreq (int x)                               //setRxFreq
{
  m_rxFreq = x;         // x is freq in Hz
  DrawOverlay();
  update();
}

int CPlotter::rxFreq() {return m_rxFreq;}                      //rxFreq

void CPlotter::leaveEvent(QEvent *event)
{
    m_lastMouseX = -1;
    m_lastPaintedX = -1;
    event->ignore();
}

void CPlotter::mouseMoveEvent (QMouseEvent * event)
{
  int x=event->x();
  if(x < 0) x = 0;
  if(x>m_Size.width()) x = m_Size.width();
  if(m_freq) m_pos = event->globalPos();
  m_lastMouseX = x;
  update();

  event->ignore();
  if(m_freq) {
    if (!m_bTotalPower){
        QToolTip::showText(event->globalPos(),QString::number(int(FreqfromX(x))));
    } else {
        int y=event->y();
        float pdB=10.0*(m_h-y)/m_vpixperdiv + 20.0;
        if(y<(m_h-m_h2)) {
            QToolTip::showText(event->globalPos(),QString::number(int(FreqfromX(x))));
        } else {
            QString t;
            t=t.asprintf("%4.1f dB",pdB);
            QToolTip::showText(event->globalPos(),t);
        }
    }
  }
  QWidget::mouseMoveEvent(event);
}

void CPlotter::mouseReleaseEvent (QMouseEvent * event)
{
  bool rightbutton = (event->button() & Qt::RightButton);
  bool leftbutton = (event->button() & Qt::LeftButton);
  bool shift = (event->modifiers() & Qt::ShiftModifier);
  if (rightbutton) {
     leftbutton = true;
     shift = true;
  }
  if (leftbutton) {
    int x=event->x();
    if(x<0) x=0;
    if(x>m_Size.width()) x=m_Size.width();
    bool ctrl = (event->modifiers() & Qt::ControlModifier);
    if(!shift and m_mode=="FST4W") return;
    int newFreq = int(FreqfromX(x)+0.5);
    int oldTxFreq = m_txFreq;
    int oldRxFreq = m_rxFreq;
    if (ctrl and m_mode!="FST4W") {
      emit setFreq1 (newFreq, newFreq);
    } else if (shift) {
      emit setFreq1 (oldRxFreq, newFreq);
    } else {
      emit setFreq1(newFreq,oldTxFreq);
    }

    int n=1;
    if(ctrl) n+=100;
    emit freezeDecode1(n);
  }
  else {
    event->ignore ();           // let parent handle
  }
//  replot();                // ### Not needed?  ###
}

void CPlotter::mouseDoubleClickEvent (QMouseEvent * event)
{
    bool rightbutton = (event->button() & Qt::RightButton);
    bool leftbutton = (event->button() & Qt::LeftButton);
    bool ctrl = (event->modifiers() & Qt::ControlModifier);
  if (leftbutton) {
    int n=2;
    if(ctrl) n+=100;
    emit freezeDecode1(n);
  } else if (rightbutton) {
      int x=event->x();
      if(x<0) x=0;
      if(x>m_Size.width()) x=m_Size.width();
      int newFreq = int(FreqfromX(x)+0.5);
      if (m_mode!="FST4W") emit setFreq1 (newFreq, newFreq);
      int n=1;
      if(ctrl) n+=100;
      emit freezeDecode1(n);
  } else {
      event->ignore ();           // let parent handle
  }
}

void CPlotter::setNsps(double trperiod, int nsps)                    //setNsps
{
  m_TRperiod=trperiod;
  m_nsps=nsps;
  m_fftBinWidth=1500.0/2048.0;
  if(m_nsps==15360)  m_fftBinWidth=1500.0/2048.0;
  if(m_nsps==40960)  m_fftBinWidth=1500.0/6144.0;
  if(m_nsps==82944)  m_fftBinWidth=1500.0/12288.0;
  if(m_nsps==252000) m_fftBinWidth=1500.0/32768.0;
  DrawOverlay();                         //Redraw scales and ticks
  update();                              //trigger a new paintEvent}
}

void CPlotter::setTxFreq(int n)                                 //setTxFreq
{
  m_txFreq=n;
  DrawOverlay();
  update();
}

void CPlotter::setMode(QString mode)                            //setMode
{
  m_mode=mode;
}

void CPlotter::setSubMode(int n)                                //setSubMode
{
  m_nSubMode=n;
}

int CPlotter::Fmax()
{
  return m_fMax;
}

void CPlotter::setDialFreq(double d)
{
  m_dialFreq=d;
  DrawOverlay();
  update();
}

void CPlotter::setRxBand(QString band)
{
  m_rxBand=band;
}

void CPlotter::setFlatten(bool b1, bool b2)
{
  m_Flatten=0;
  if(b1) m_Flatten=1;
  if(b2) m_Flatten=2;
}

void CPlotter::setSuperFox(bool b)
{
  m_bSuperFox=b;
  if(m_bSuperFox) m_bSuperHound=false;
}
void CPlotter::setSuperHound(bool b)
{
  m_bSuperHound=b;
  if(m_bSuperHound) m_bSuperFox=false;
}

void CPlotter::setTol(int n)                                 //setTol()
{
  m_tol=n;
  DrawOverlay();
}

void CPlotter::setFST4_FreqRange(int fLow,int fHigh)
{
  m_nfa=fLow;
  m_nfb=fHigh;
  DrawOverlay();
  update();
}

void CPlotter::setSingleDecode(bool b)
{
  m_bSingleDecode=b;
}


void CPlotter::setColours(QVector<QColor> const& cl)
{
  g_ColorTbl = cl;
}

void CPlotter::SetPercent2DScreen(int percent)
{
  m_Percent2DScreen=percent;
  resizeEvent(NULL);
  update();
}
void CPlotter::setVHF(bool bVHF)
{
  m_bVHF=bVHF;
}

void CPlotter::setRedFile(QString fRed)
{
  m_redFile=fRed;
}

void CPlotter::setDiskUTC(int nutc)
{
  m_nUTC=nutc;
}

void CPlotter::drawTotalPower(float pdB)
{
  m_pdB=pdB;
}

void CPlotter::restartTotalPower()
{
  m_x=0;
}

void CPlotter::setTimestamp(int n)
{
    m_timestamp=n;
}

void CPlotter::setBars(bool b)
{
  setMouseTracking(b || m_freq);
  m_bars=b;
  DrawOverlay();
  update();
}

void CPlotter::showFreq(bool b)
{
  setMouseTracking(b || m_bars);
  m_freq=b;
}

void CPlotter::setDarkStyle (bool b)     //set wide filter lines for Hound mode
{
  m_useDarkStyle=b;
  DrawOverlay();
  update();
}
