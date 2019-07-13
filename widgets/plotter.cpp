#include "plotter.h"
#include <math.h>
#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QDateTime>
#include <QPen>
#include <QMouseEvent>
#include <QDebug>
#include "commons.h"
#include "moc_plotter.cpp"
#include <fstream>
#include <iostream>

#define MAX_SCREENSIZE 2048

extern "C" {
  void flat4_(float swide[], int* iz, int* nflatten);
  void plotsave_(float swide[], int* m_w , int* m_h1, int* irow);
}

CPlotter::CPlotter(QWidget *parent) :                  //CPlotter Constructor
  QFrame {parent},
  m_set_freq_action {new QAction {tr ("&Set Rx && Tx Offset"), this}},
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
  m_startFreq {0}
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_PaintOnScreen,false);
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_NoSystemBackground, true);
  m_bReplot=false;

  // contextual pop up menu
  setContextMenuPolicy (Qt::CustomContextMenu);
  connect (this, &QWidget::customContextMenuRequested, [this] (QPoint const& pos) {
      QMenu menu {this};
      menu.addAction (m_set_freq_action);
      auto const& connection = connect (m_set_freq_action, &QAction::triggered, [this, pos] () {
          int newFreq = FreqfromX (pos.x ()) + .5;
          emit setFreq1 (newFreq, newFreq);
        });
      menu.exec (mapToGlobal (pos));
      disconnect (connection);
    });
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
  m_paintEventBusy=false;
}

void CPlotter::draw(float swide[], bool bScroll, bool bRed)
{
  if (!m_TRperiod) return;      // not ready to plot yet
  int j,j0;
  static int ktop=0;
  float y,y2,ymin;
  double fac = sqrt(m_binsPerPixel*m_waterfallAvg/15.0);
  double gain = fac*pow(10.0,0.015*m_plotGain);
  double gain2d = pow(10.0,0.02*(m_plot2dGain));

  if(m_bReference != m_bReference0) resizeEvent(NULL);
  m_bReference0=m_bReference;

//move current data down one line (must do this before attaching a QPainter object)
  if(bScroll and !m_bReplot) m_WaterfallPixmap.scroll(0,1,0,0,m_w,m_h1);
  QPainter painter1(&m_WaterfallPixmap);
  m_2DPixmap = m_OverlayPixmap.copy(0,0,m_w,m_h2);
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
  for(int i=0; i<iz; i++) {
    y=swide[i];
    if(y<ymin) ymin=y;
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
      y2=gain2d*sum/m_binsPerPixel + m_plot2dZero;
    }

    if(m_bReference) {                                   //Reference (red)
      float df_ref=12000.0/6912.0;
      int j=FreqfromX(i)/df_ref + 0.5;
      y2=spectra_.ref[j] + m_plot2dZero;
//      if(gain2d>1.5) y2=spectra_.filter[j] + m_plot2dZero;

    }

    if(i==iz-1) {
      painter2D.drawPolyline(LineBuf,j);
      if(m_mode=="QRA64") {
        painter2D.setPen(Qt::red);
        painter2D.drawPolyline(LineBuf2,ktop);
      }
    }
    LineBuf[j].setX(i);
    LineBuf[j].setY(int(0.9*m_h2-y2*m_h2/70.0));
    if(y2<y2min) y2min=y2;
    if(y2>y2max) y2max=y2;
    j++;
  }
  if(m_bReplot) return;

  if(swide[0]>1.0e29) m_line=0;
  if(m_mode=="FT4" and m_line==34) m_line=0;
  if(m_line == painter1.fontMetrics ().height ()) {
    painter1.setPen(Qt::white);
    QString t;
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    int n = fmod(0.001*ms,m_TRperiod);
    QDateTime t1=QDateTime::currentDateTimeUtc().addSecs(-n);
    if(m_TRperiod<60.0) {
      t=t1.toString("hh:mm:ss") + "    " + m_rxBand;
    } else {
      t=t1.toString("hh:mm") + "    " + m_rxBand;
    }
    painter1.drawText (5, painter1.fontMetrics ().ascent (), t);
  }

  if(m_mode=="JT4" or m_mode=="QRA64") {
    QPen pen3(Qt::yellow);                     //Mark freqs of JT4 single-tone msgs
    painter2D.setPen(pen3);
    Font.setWeight(QFont::Bold);
    painter2D.setFont(Font);
    int x1=XfromFreq(m_rxFreq);
    y=0.2*m_h2;
    painter2D.drawText(x1-4,y,"T");
    x1=XfromFreq(m_rxFreq+250);
    painter2D.drawText(x1-4,y,"M");
    x1=XfromFreq(m_rxFreq+500);
    painter2D.drawText(x1-4,y,"R");
    x1=XfromFreq(m_rxFreq+750);
    painter2D.drawText(x1-4,y,"73");
  }

    if(bRed) {
      std::ifstream f;
      f.open(m_redFile.toLatin1());
      if(f) {
        int x,y;
        float freq,sync;
        float slimit=6.0;
        QPen pen0(Qt::red,1);
        painter1.setPen(pen0);
        for(int i=0; i<99999; i++) {
          f >> freq >> sync;
          if(f.eof()) break;
          x=XfromFreq(freq);
          y=(sync-slimit)*3.0;
          if(y>0) {
            if(y>15.0) y=15.0;
            if(x>=0 and x<=m_w) {
              painter1.setPen(pen0);
              painter1.drawLine(x,0,x,y);
            }
          }
        }
        f.close();
      }
//      m_bDecodeFinished=false;
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
  float swide[m_w];
  m_bReplot=true;
  for(int irow=0; irow<m_h1; irow++) {
    m_j=irow;
    plotsave_(swide,&m_w,&m_h1,&irow);
    draw(swide,false,false);
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
  QRect rect;
  QPen penOrange(QColor(255,165,0),3);
  QPen penGreen(Qt::green, 3);                 //Mark Tol range with green line
  QPen penRed(Qt::red, 3);                     //Mark Tx freq with red
  QPainter painter(&m_OverlayPixmap);
  painter.initFrom(this);
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

  pixperdiv = m_freqPerDiv/df;
  m_hdivs = w*df/m_freqPerDiv + 1.9999;

  float xx0=float(m_startFreq)/float(m_freqPerDiv);
  xx0=xx0-int(xx0);
  int x0=xx0*pixperdiv+0.5;
  for( int i=1; i<m_hdivs; i++) {                  //draw vertical grids
    x = (int)((float)i*pixperdiv ) - x0;
    if(x >= 0 and x<=m_w) {
      painter.setPen(QPen(Qt::white, 1,Qt::DotLine));
      painter.drawLine(x, 0, x , m_h2);
    }
  }

  pixperdiv = (float)m_h2 / (float)VERT_DIVS;
  painter.setPen(QPen(Qt::white, 1,Qt::DotLine));
  for( int i=1; i<VERT_DIVS; i++) {                //draw horizontal grids
    y = (int)( (float)i*pixperdiv );
    painter.drawLine(0, y, w, y);
  }

  QRect rect0;
  QPainter painter0(&m_ScalePixmap);
  painter0.initFrom(this);

  //create Font to use for scales
  QFont Font("Arial");
  Font.setPointSize(12);
  Font.setWeight(QFont::Normal);
  painter0.setFont(Font);
  painter0.setPen(Qt::black);

  if(m_binsPerPixel < 1) m_binsPerPixel=1;
  m_hdivs = w*df/m_freqPerDiv + 0.9999;

  m_ScalePixmap.fill(Qt::white);
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

  if(m_modeTx=="JT9" and m_nSubMode>0) {     //JT9
    bw=8.0*12000.0/m_nsps;
    if(m_nSubMode==1) bw=2*bw;   //B
    if(m_nSubMode==2) bw=4*bw;   //C
    if(m_nSubMode==3) bw=8*bw;   //D
    if(m_nSubMode==4) bw=16*bw;  //E
    if(m_nSubMode==5) bw=32*bw;  //F
    if(m_nSubMode==6) bw=64*bw;  //G
    if(m_nSubMode==7) bw=128*bw; //H
  }

  if(m_mode=="QRA64") {                      //QRA64
    bw=63.0*12000.0/m_nsps;
    if(m_nSubMode==1) bw=2*bw;   //B
    if(m_nSubMode==2) bw=4*bw;   //C
    if(m_nSubMode==3) bw=8*bw;   //D
    if(m_nSubMode==4) bw=16*bw;  //E
  }

  if(m_modeTx=="JT65") {                     //JT65
    bw=65.0*11025.0/4096.0;
    if(m_nSubMode==1) bw=2*bw;   //B
    if(m_nSubMode==2) bw=4*bw;   //C
  }

  painter0.setPen(penGreen);
  if(m_mode=="WSPR") {
    x1=XfromFreq(1400);
    x2=XfromFreq(1600);
    painter0.drawLine(x1,29,x2,29);
  }

  if(m_mode=="WSPR-LF") {
    x1=XfromFreq(1600);
    x2=XfromFreq(1700);
    painter0.drawLine(x1,29,x2,29);
  }

  if(m_mode=="FreqCal") {                   //FreqCal
    x1=XfromFreq(m_rxFreq-m_tol);
    x2=XfromFreq(m_rxFreq+m_tol);
    painter0.drawLine(x1,29,x2,29);
    x1=XfromFreq(m_rxFreq);
    painter0.drawLine(x1,24,x1,30);
  }

  if(m_mode=="JT9" or m_mode=="JT65" or m_mode=="JT9+JT65"
     or m_mode=="QRA64" or m_mode=="FT8" or m_mode=="FT4") {

    if(m_mode=="QRA64" or (m_mode=="JT65" and m_bVHF)) {
      painter0.setPen(penGreen);
      x1=XfromFreq(m_rxFreq-m_tol);
      x2=XfromFreq(m_rxFreq+m_tol);
      painter0.drawLine(x1,28,x2,28);
      x1=XfromFreq(m_rxFreq);
      painter0.drawLine(x1,24,x1,30);

      if(m_mode=="JT65") {
        painter0.setPen(penOrange);
        x3=XfromFreq(m_rxFreq+20.0*bw/65.0);    //RO
        painter0.drawLine(x3,24,x3,30);
        x4=XfromFreq(m_rxFreq+30.0*bw/65.0);    //RRR
        painter0.drawLine(x4,24,x4,30);
        x5=XfromFreq(m_rxFreq+40.0*bw/65.0);    //73
        painter0.drawLine(x5,24,x5,30);
      }
      painter0.setPen(penGreen);
      x6=XfromFreq(m_rxFreq+bw);             //Highest tone
      painter0.drawLine(x6,24,x6,30);

    } else {
      painter0.setPen(penGreen);
      x1=XfromFreq(m_rxFreq);
      x2=XfromFreq(m_rxFreq+bw);
      painter0.drawLine(x1,24,x1,30);
      painter0.drawLine(x1,28,x2,28);
      painter0.drawLine(x2,24,x2,30);
    }
  }

  if(m_mode=="JT9" or m_mode=="JT65" or m_mode=="JT9+JT65" or
     m_mode.mid(0,4)=="WSPR" or m_mode=="QRA64" or m_mode=="FT8"
     or m_mode=="FT4") {
    painter0.setPen(penRed);
    x1=XfromFreq(m_txFreq);
    x2=XfromFreq(m_txFreq+bw);
    if(m_mode=="WSPR") {
      bw=4*12000.0/8192.0;                  //WSPR
      x1=XfromFreq(m_txFreq-0.5*bw);
      x2=XfromFreq(m_txFreq+0.5*bw);
    }
    if(m_mode=="WSPR-LF") {
      bw=3*12000.0/8640.0;                  //WSPR-LF
      x1=XfromFreq(m_txFreq-0.5*bw);
      x2=XfromFreq(m_txFreq+0.5*bw);
    }
    painter0.drawLine(x1,17,x1,21);
    painter0.drawLine(x1,17,x2,17);
    painter0.drawLine(x2,17,x2,21);
  }

  if(m_mode=="JT9+JT65") {
    QPen pen2(Qt::blue, 3);                //Mark the JT65 | JT9 divider
    painter0.setPen(pen2);
    x1=XfromFreq(m_fMin);
    if(x1<2) x1=2;
    x2=x1+30;
    painter0.drawLine(x1,8,x1,28);
  }

  if(m_dialFreq>10.13 and m_dialFreq< 10.15 and m_mode.mid(0,4)!="WSPR") {
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
//  float w = m_WaterfallPixmap.width();
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
  resizeEvent(NULL);
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

void CPlotter::mouseReleaseEvent (QMouseEvent * event)
{
  if (Qt::LeftButton == event->button ()) {
    int x=event->x();
    if(x<0) x=0;
    if(x>m_Size.width()) x=m_Size.width();
    bool ctrl = (event->modifiers() & Qt::ControlModifier);
    bool shift = (event->modifiers() & Qt::ShiftModifier);
    int newFreq = int(FreqfromX(x)+0.5);
    int oldTxFreq = m_txFreq;
    int oldRxFreq = m_rxFreq;
    if (ctrl) {
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
}

void CPlotter::mouseDoubleClickEvent (QMouseEvent * event)
{
  if (Qt::LeftButton == event->button ()) {
    bool ctrl = (event->modifiers() & Qt::ControlModifier);
    int n=2;
    if(ctrl) n+=100;
    emit freezeDecode1(n);
  }
  else {
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

void CPlotter::setModeTx(QString modeTx)                        //setModeTx
{
  m_modeTx=modeTx;
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

void CPlotter::setTol(int n)                                 //setTol()
{
  m_tol=n;
  DrawOverlay();
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
