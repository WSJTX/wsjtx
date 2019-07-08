#include "echoplot.h"
#include "commons.h"
#include <math.h>
#include <QPainter>
#include <QPen>
#include <QDebug>
#include "moc_echoplot.cpp"

#define MAX_SCREENSIZE 2048


EPlotter::EPlotter(QWidget *parent) :                  //EPlotter Constructor
  QFrame(parent)
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_PaintOnScreen,false);
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_NoSystemBackground, true);

  m_StartFreq = -200;
  m_fftBinWidth=12000.0/32768.0;
  m_binsPerPixel=1;
  m_fSpan=1000.0;
  m_hdivs = HORZ_DIVS;
  m_Running = false;
  m_paintEventBusy=false;
  m_2DPixmap = QPixmap(0,0);
  m_ScalePixmap = QPixmap(0,0);
  m_OverlayPixmap = QPixmap(0,0);
  m_Size = QSize(0,0);
  m_TxFreq = 1500;
  m_line = 0;
  m_dBStepSize=10;
}

EPlotter::~EPlotter() { }                                      // Destructor

QSize EPlotter::minimumSizeHint() const
{
  return QSize(50, 50);
}

QSize EPlotter::sizeHint() const
{
  return QSize(180, 180);
}

void EPlotter::resizeEvent(QResizeEvent* )                    //resizeEvent()
{
  if(!size().isValid()) return;
  if( m_Size != size() ) {  //if changed, resize pixmaps to new screensize
    m_Size = size();
    m_w = m_Size.width();
    m_h = m_Size.height();
    m_h1=30;
    m_h2=m_h-m_h1;
    m_2DPixmap = QPixmap(m_Size.width(), m_h2);
    m_2DPixmap.fill(Qt::black);
    m_OverlayPixmap = QPixmap(m_Size.width(), m_h2);
    m_ScalePixmap = QPixmap(m_w,30);
    m_ScalePixmap.fill(Qt::white);
    m_fSpan=m_w*m_fftBinWidth*m_binsPerPixel;
    m_StartFreq=50 * int((-0.5*m_fSpan)/50.0 - 0.5);
  }
  DrawOverlay();
  draw();
}

void EPlotter::paintEvent(QPaintEvent *)                    // paintEvent()
{
  if(m_paintEventBusy) return;
  m_paintEventBusy=true;
  QPainter painter(this);
  painter.drawPixmap(0,0,m_ScalePixmap);
  painter.drawPixmap(0,m_h1,m_2DPixmap);
  m_paintEventBusy=false;
}

void EPlotter::draw()                           //draw()
{
  int i,j,y;
  float blue[4096],red[4096];
  float gain = pow(10.0,(m_plotGain/20.0));
  QPen penBlue(QColor(0,255,255),1);
  QPen penRed(Qt::red,1);
  QPen penRed2(Qt::red,2);
  QPen penBlack(Qt::black,1);
  QPen penBlack2(Qt::black,2);

  if(m_2DPixmap.size().width()==0) return;
  QPainter painter2D(&m_2DPixmap);
  QRect tmp(0,0,m_w,m_h2);
  if(m_nColor < 2) {
    painter2D.fillRect(tmp,Qt::black);
  } else {
    painter2D.fillRect(tmp,Qt::white);
    painter2D.setPen(penBlack);
    painter2D.drawLine(0,0,m_w,0);
  }

  QPoint LineBuf[MAX_SCREENSIZE];

  if(m_binsPerPixel==0) m_binsPerPixel=1;
  j=0;
  for(i=0; i<4096/m_binsPerPixel; i++) {
    blue[i]=0.0;
    red[i]=0.0;
    for(int k=0; k<m_binsPerPixel; k++) {
      blue[i]+=echocom_.blue[j];
      red[i]+=echocom_.red[j];
      j++;
    }
  }
  if(m_smooth>0) {
    for(i=0; i<m_smooth; i++) {
      int n4096=4096;
      smo121_(blue,&n4096);
      smo121_(red,&n4096);
    }
  }

// check i0 value! ...
  int i0=2048/m_binsPerPixel + int(m_StartFreq/(m_fftBinWidth*m_binsPerPixel));
  if(m_blue) {
    painter2D.setPen(penBlue);
    j=0;
    for(i=0; i<m_w; i++) {
      y = 0.9*m_h2 - gain*(m_h/10.0)*(blue[i0+i]-1.0) - 0.01*m_h2*m_plotZero;
      LineBuf[j].setX(i);
      LineBuf[j].setY(y);
      j++;
    }
    painter2D.drawPolyline(LineBuf,j);
  }
  switch (m_nColor) {
    case 0: painter2D.setPen(penRed); break;
    case 1: painter2D.setPen(penRed2); break;
    case 2: painter2D.setPen(penRed); break;
    case 3: painter2D.setPen(penRed2); break;
    case 4: painter2D.setPen(penBlack); break;
    case 5: painter2D.setPen(penBlack2); break;
  }

  j=0;
  for(int i=0; i<m_w; i++) {
    y = 0.9*m_h2 - gain*(m_h/10.0)*(red[i0+i]-1.0) - 0.01*m_h2*m_plotZero;
    LineBuf[j].setX(i);
    LineBuf[j].setY(y);
    j++;
  }
  painter2D.drawPolyline(LineBuf,j);
  update();                              //trigger a new paintEvent
}

void EPlotter::DrawOverlay()                                 //DrawOverlay()
{
  if(m_OverlayPixmap.isNull() or m_2DPixmap.isNull()) return;
//  int w = m_WaterfallPixmap.width();
  int x,y;

  QPainter painter(&m_OverlayPixmap);
  painter.setBackground (palette ().brush (backgroundRole ()));
  QLinearGradient gradient(0, 0, 0 ,m_h2);  //fill background with gradient
  gradient.setColorAt(1, Qt::black);
  gradient.setColorAt(0, Qt::darkBlue);
  painter.setBrush(gradient);
  painter.drawRect(0, 0, m_w, m_h2);
  painter.setBrush(Qt::SolidPattern);

  m_fSpan = m_w*m_fftBinWidth*m_binsPerPixel;
  m_freqPerDiv=20;
  if(m_fSpan>250) m_freqPerDiv=50;
  if(m_fSpan>500) m_freqPerDiv=100;
  if(m_fSpan>1000) m_freqPerDiv=200;
  if(m_fSpan>2000) m_freqPerDiv=500;

//  m_StartFreq=50 * int((-0.5*m_fSpan)/50.0 - 0.5);
  m_StartFreq=m_freqPerDiv * int((-0.5*m_fSpan)/m_freqPerDiv - 0.5);

  float pixPerHdiv = m_freqPerDiv/(m_fftBinWidth*m_binsPerPixel);
  float pixPerVdiv = float(m_h2)/float(VERT_DIVS);

  m_hdivs = m_w*m_fftBinWidth*m_binsPerPixel/m_freqPerDiv + 0.9999;

  painter.setPen(QPen(Qt::white, 1,Qt::DotLine));
  for( int i=1; i<m_hdivs; i++)                   //draw vertical grids
  {
    x=int(i*pixPerHdiv);
    painter.drawLine(x,0,x,m_h2);
  }

  for( int i=1; i<VERT_DIVS; i++)                 //draw horizontal grids
  {
    y = (int)( (float)i*pixPerVdiv );
    painter.drawLine(0,y,m_w,y);
  }

  QRect rect0;
  QPainter painter0(&m_ScalePixmap);
  painter0.setBackground (palette ().brush (backgroundRole ()));

  //create Font to use for scales
  QFont Font("Arial");
  Font.setPointSize(12);
  QFontMetrics metrics(Font);
  Font.setWeight(QFont::Normal);
  painter0.setFont(Font);
  painter0.setPen(Qt::black);

  m_ScalePixmap.fill(Qt::white);
  painter0.drawRect(0, 0, m_w, 30);

//draw tick marks on upper scale
  for( int i=1; i<m_hdivs; i++) {         //major ticks
    x = (int)( (float)i*pixPerHdiv );
    painter0.drawLine(x,18,x,30);
  }
  int minor=5;
  if(m_freqPerDiv==200) minor=4;
  for( int i=1; i<minor*m_hdivs; i++) {   //minor ticks
    x = i*pixPerHdiv/minor;
    painter0.drawLine(x,24,x,30);
  }

//draw frequency values
  MakeFrequencyStrs();
  for( int i=0; i<=m_hdivs; i++) {
    if(0==i) {
      //left justify the leftmost text
      x = (int)( (float)i*pixPerHdiv);
      rect0.setRect(x,0, (int)pixPerHdiv, 20);
      painter0.drawText(rect0, Qt::AlignLeft|Qt::AlignVCenter,
                       m_HDivText[i]);
    }
    else if(m_hdivs == i) {
      //right justify the rightmost text
      x = (int)( (float)i*pixPerHdiv - pixPerHdiv);
      rect0.setRect(x,0, (int)pixPerHdiv, 20);
      painter0.drawText(rect0, Qt::AlignRight|Qt::AlignVCenter,
                       m_HDivText[i]);
    } else {
      //center justify the rest of the text
      x = (int)( (float)i*pixPerHdiv - pixPerHdiv/2);
      rect0.setRect(x,0, (int)pixPerHdiv, 20);
      painter0.drawText(rect0, Qt::AlignHCenter|Qt::AlignVCenter,
                       m_HDivText[i]);
    }
  }

/*
  QPen pen1(Qt::red, 3);                         //Mark Tx Freq with red tick
  painter0.setPen(pen1);
  x = XfromFreq(m_TxFreq);
  painter0.drawLine(x,17,x,30);
*/
}

void EPlotter::MakeFrequencyStrs()                       //MakeFrequencyStrs
{
  float freq;
  for(int i=0; i<=m_hdivs; i++) {
    freq=m_StartFreq + i*m_freqPerDiv;
    m_HDivText[i].setNum((int)freq);
  }
}

int EPlotter::XfromFreq(float f)                               //XfromFreq()
{
  int x = (int) m_w * (f - m_StartFreq)/m_fSpan;
  if(x<0 ) return 0;
  if(x>m_w) return m_w;
  return x;
}

float EPlotter::FreqfromX(int x)                               //FreqfromX()
{
  return float(m_StartFreq + x*m_fftBinWidth*m_binsPerPixel);
}

void EPlotter::SetRunningState(bool running)              //SetRunningState()
{
  m_Running = running;
}

void EPlotter::setPlotZero(int plotZero)                  //setPlotZero()
{
  m_plotZero=plotZero;
}

int EPlotter::getPlotZero()                               //getPlotZero()
{
  return m_plotZero;
}

void EPlotter::setPlotGain(int plotGain)                  //setPlotGain()
{
  m_plotGain=plotGain;
}

int EPlotter::getPlotGain()                               //getPlotGain()
{
  return m_plotGain;
}

void EPlotter::setSmooth(int n)                               //setSmooth()
{
  m_smooth=n;
}

int EPlotter::getSmooth()                                    //getSmooth()
{
  return m_smooth;
}

void EPlotter::setColors(qint32 n)                               //setSmooth()
{
  m_nColor=n;
  draw();
}

int EPlotter::plotWidth(){return m_2DPixmap.width();}

void EPlotter::UpdateOverlay() {DrawOverlay();}
