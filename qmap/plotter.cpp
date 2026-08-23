#include "plotter.h"
#include <math.h>
#include <algorithm>
#include <QDebug>
#include <fstream>
#include <iostream>

#define MAX_SCREENSIZE 2048


CPlotter::CPlotter(QWidget *parent) :                  //CPlotter Constructor
  QFrame(parent)
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_PaintOnScreen,false);
  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setMouseTracking(true);

  m_StartFreq = 100;
  m_nSpan=65;                    //Units: kHz
  m_fSpan=(float)m_nSpan;
  m_hdivs = HORZ_DIVS;
  m_FreqUnits = 1;
  m_Running = false;
  m_paintEventBusy=false;
  m_WaterfallPixmap = QPixmap(0,0);
  m_ZoomWaterfallPixmap = QPixmap(0,0);
  m_2DPixmap = QPixmap(0,0);
  m_ScalePixmap = QPixmap(0,0);
  m_ZoomScalePixmap = QPixmap(0,0);
  m_Size = QSize(0,0);
  m_fQSO = 125;
  m_TXkHz = m_fQSO;
  m_line = 0;
  m_fSample = 96000;
  m_paintAllZoom = false;
  m_TxDF=0;
  m_bDecodeFinished=false;
  memset(m_zwf,0,sizeof(m_zwf));   // once, not on every resize -- see resizeEvent()
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
  bool changed = (m_Size != size());
  if(changed) {
    //if changed, resize pixmaps to new screensize
    m_Size = size();
    int w = m_Size.width();
    int h = (m_Size.height()-60)/2;
    m_WaterfallPixmap = QPixmap(w,h);
    m_ZoomWaterfallPixmap = QPixmap(w,h);
    m_2DPixmap = QPixmap(w,h);
    m_WaterfallPixmap.fill(Qt::black);
    m_ZoomWaterfallPixmap.fill(Qt::black);
    m_2DPixmap.fill(Qt::black);
    m_ScalePixmap = QPixmap(w,30);
    m_ZoomScalePixmap = QPixmap(w,30);    //(no change on resize...)
    m_ScalePixmap.fill(Qt::white);
    m_ZoomScalePixmap.fill(Qt::yellow);

    // m_zwf (the zoom row's raw per-bin history) is intentionally NOT
    // cleared here -- it's sized independently of the widget and already
    // holds the real data; only the just-recreated pixmaps above lost
    // their pixels.
  }
  SetCenterFreq(-1);
  DrawOverlay();      // recomputes m_binsPerPixel/m_fSpan for the new width
  if(changed) {
    // Repaint both rows from the raw data that survived, now that
    // DrawOverlay() above has updated m_fSpan/m_binsPerPixel to match
    // the new width -- rebuildWideFromHistory() needs the *current*
    // frequency-per-pixel to remap old rows by frequency, not raw index.
    rebuildWideFromHistory();
    m_paintAllZoom = true;      // forces paintEvent to fully repaint the
                                 // zoom row from m_zwf at the new size
  }
}

void CPlotter::paintEvent(QPaintEvent *)                    // paintEvent()
{
  static int x00=-99;

  if(m_paintEventBusy) return;
  m_paintEventBusy=true;
  QPainter painter(this);
  int w = m_Size.width();
  int h = (m_Size.height()-60)/2;
  painter.drawPixmap(0,0,m_ScalePixmap);
  painter.drawPixmap(0,30,m_WaterfallPixmap);
  if(m_2Dspec) {
    painter.drawPixmap(0,h+30,m_ScalePixmap);
    painter.drawPixmap(0,h+60,m_2DPixmap);
    m_paintEventBusy=false;
    return;
  }

  painter.drawPixmap(0,h+30,m_ZoomScalePixmap);
  painter.drawPixmap(0,h+60,m_ZoomWaterfallPixmap);

  QRect target(0,h+30,w,30);           // (x,y,width,height)
  QRect source(0,0,w,30);
  painter.drawPixmap(target,m_ZoomScalePixmap,source);

  float df=m_fSample/32768.0;
  int x0=16384 + (0.001*(m_ZoomStartFreq+m_fCal)+m_fQSO-m_nkhz) * 1000.0/df + 0.5;

  QPainter painter1(&m_WaterfallPixmap);
  QPainter painter2(&m_ZoomWaterfallPixmap);
  for(int i=0; i<w; i++) {                      //Paint the top row
    painter2.setPen(m_ColorTbl[m_zwf[x0+i]]);
    painter2.drawPoint(i,0);
  }
  if(m_paintAllZoom or (x0 != x00 and x00 != -99)) {
    // If new fQSO, paint all rows
    int k=x0;
    for(int j=1; j<h; j++) {
      k += 32768;
      if(x0 != x00 and x00 != -99) {
        for(int i=0; i<w; i++) {
          painter2.setPen(m_ColorTbl[m_zwf[i+k]]);
          painter2.drawPoint(i,j);
        }
      }
      if(j == 13 and x0 == x00) {
        painter2.setPen(m_ColorTbl[255]);
        painter2.drawText(5,10,m_sutc);
      }
    }
  } else if(m_line == 13) {
    painter2.setPen(m_ColorTbl[255]);
    UTCstr();
    painter2.drawText(5,10,m_sutc);
  }

//  if(m_line==63) {
  if(m_bDecodeFinished) {
    /*
    std::ifstream f;
    f.open("./red.dat");
    if(f) {
      int x,y;
      float freq,sync;
      float slimit=6.0;
      QPen pen0(Qt::red,1);
      painter2.setPen(pen0);
      for(int i=0; i<99999; i++) {
        f >> freq >> sync;
        if(f.eof()) break;
        x=(freq - m_ZoomStartFreq)/df;
        y=(sync-slimit)*3.0;
        if(y>0) {
          if(y>15.0) y=15.0;
          if(x>=0 and x<=w) painter2.drawLine(x,0,x,y);
          x=XfromFreq(float(fQSO()+0.001*freq));
          painter1.setPen(pen0);
          painter1.drawLine(x,0,x,y);
        }
      }
      f.close();
    }
    */
    m_bDecodeFinished=false;
  }

  m_paintAllZoom=false;
  x00=x0;

  QRect target2(0,h+60,w,h);           // (x,y,width,height)
  QRect source2(0,0,w,h);
  painter.drawPixmap(target2,m_ZoomWaterfallPixmap,source2);
  // Decoded-callsign overlay -- rendered last so labels sit on top of the
  // upper waterfall pixmap. WideGraph maintains the list lifecycle.
  if (!m_decodeLabels.isEmpty()) {
    paintDecodeLabels(painter);
  }
  m_paintEventBusy=false;
}

void CPlotter::DecodeFinished()
{
  m_bDecodeFinished=true;
  update();                              //trigger a new paintEvent
}

void CPlotter::draw(float s[], int i0, float splot[])                 //draw()
{
  int i,j,w,h;
  float y;

  m_i0=i0;
  w = m_WaterfallPixmap.width();
  h = m_WaterfallPixmap.height();
  double gain = pow(10.0,0.05*(m_plotGain+7));

  //move current data down one line
  //(must do this before attaching a QPainter object)
  m_WaterfallPixmap.scroll(0,1,0,0,w,h);
  m_ZoomWaterfallPixmap.scroll(0,1,0,0, w, h);
  memmove(&m_zwf[32768],m_zwf,32768*(h-1));
  QPainter painter1(&m_WaterfallPixmap);
  QPainter painter2D(&m_2DPixmap);

  for(i=0; i<256; i++) {                     //Zero the histograms
    m_hist1[i]=0;
    m_hist2[i]=0;
  }

  painter2D.setPen(Qt::green);
  QRect tmp(0,0,w,h);
  painter2D.fillRect(tmp,Qt::black);
  QPoint LineBuf[MAX_SCREENSIZE];
  j=0;
  bool strong0=false;
  bool strong=false;

  for(i=0; i<w; i++) {
    strong=false;
    if(s[i]<0) {
      strong=true;
      s[i]=-s[i];
    }
    y = 10.0*log10(s[i]);
    int y1 = 5.0*gain*(y + 29 -m_plotZero);
    if (y1<0) y1=0;
    if (y1>254) y1=254;
    if (s[i]>1.e29) y1=255;
    m_hist1[y1]++;
    painter1.setPen(m_ColorTbl[y1]);
    painter1.drawPoint(i,0);
    if(m_2Dspec) {
      int y2 = gain*(y + 34 -m_plotZero);
      if (y2<0) y2=0;
      if (y2>254) y2=254;
      if (s[i]>1.e29) y2=255;
      if(strong != strong0 or i==w-1) {
        painter2D.drawPolyline(LineBuf,j);
        j=0;
        strong0=strong;
        if(strong0) painter2D.setPen(Qt::red);
        if(!strong0) painter2D.setPen(Qt::green);
      }
      LineBuf[j].setX(i);
      LineBuf[j].setY(h-y2);
      j++;
    }
  }

  // Remember this row (raw, pre-color-map, post-abs), along with the
  // frequency-per-pixel it was captured at, so a later resize can repaint
  // m_WaterfallPixmap by remapping frequency to the new width instead of
  // just blanking it (or, worse, copying pixel index i to pixel index i
  // regardless of how many kHz that pixel now represents).
  {
    QVector<float> row(w);
    for (int k=0; k<w; k++) row[k]=s[k];
    WideHistoryLine line;
    line.row = row;
    line.startFreq = m_StartFreq;
    line.df = (w>0) ? m_fSpan/w : 0.0;
    m_wideHistory.prepend(line);
    if (m_wideHistory.size() > kMaxWideHistory) m_wideHistory.removeLast();
  }

  for(i=0; i<32768; i++) {
    y = 10.0*log10(splot[i]);
    int y1 = 5.0*gain*(y + 30 - m_plotZero);
    if (y1<0) y1=0;
    if (y1>254) y1=254;
    if (splot[i]>1.e29) y1=255;
    m_hist2[y1]++;
    m_zwf[i]=y1;
  }

  if(s[0]>1.0e29) m_line=0;
  m_line++;
  if(m_line == 13) {
    UTCstr();
    painter1.setPen(m_ColorTbl[255]);
    painter1.drawText(5,10,m_sutc);
    m_paintAllZoom=true;
  }
  update();                              //trigger a new paintEvent
}

// Repaints m_WaterfallPixmap from m_wideHistory, for use right after a
// resize recreates it as a blank pixmap. Remaps by frequency (using each
// row's own captured startFreq/df against the *current* m_StartFreq/
// m_fSpan) rather than by raw pixel index, so a width change rescales
// the preserved data instead of misaligning it. Uses the same
// color-mapping formula as draw()'s main loop; kept as a separate small
// duplicate rather than factored out, since draw()'s loop is also
// entangled with the histogram and 2D-spectrum bookkeeping that don't
// apply here.
void CPlotter::rebuildWideFromHistory()
{
  if (m_WaterfallPixmap.isNull()) return;
  int w = m_WaterfallPixmap.width();
  int h = m_WaterfallPixmap.height();
  if (w<=0 || m_fSpan<=0.0) return;
  double gain = pow(10.0,0.05*(m_plotGain+7));
  double newDf = m_fSpan/w;         // kHz per pixel, current width

  QPainter painter(&m_WaterfallPixmap);
  int rows = qMin(h, m_wideHistory.size());
  for (int y=0; y<rows; y++) {
    WideHistoryLine const& line = m_wideHistory.at(y);
    int n = line.row.size();
    if (n<=0 || line.df<=0.0) continue;
    for (int i=0; i<w; i++) {
      double freq = m_StartFreq + i*newDf;
      int j = int((freq - line.startFreq)/line.df + 0.5);
      if (j<0 || j>=n) continue;   // outside what this row captured
      float sv = line.row.at(j);
      double yv = 10.0*log10(sv);
      int y1 = 5.0*gain*(yv + 29 - m_plotZero);
      if (y1<0) y1=0;
      if (y1>254) y1=254;
      if (sv>1.e29) y1=255;
      painter.setPen(m_ColorTbl[y1]);
      painter.drawPoint(i,y);
    }
  }
}

void CPlotter::UTCstr()
{
  int ihr,imin;
  if(datcom_.ndiskdat != 0) {
    ihr=datcom_.nutc/100;
    imin=(datcom_.nutc) % 100;
  } else {
    qint64 ms = QDateTime::currentMSecsSinceEpoch() % 86400000;
    imin=ms/60000;
    ihr=imin/60;
    imin=imin % 60;
  }
  m_sutc = QString {"%1:%2"}.arg (ihr,2,10,QLatin1Char('0')).arg (imin,2,10,QLatin1Char('0'));
}

void CPlotter::DrawOverlay()                                 //DrawOverlay()
{
  if(m_WaterfallPixmap.isNull()) return;
  int w = m_WaterfallPixmap.width();
  int x,y;
  float pixperdiv;

  QRect rect0;
  QPainter painter0(&m_ScalePixmap);

  //create Font to use for scales
  QFont Font("Arial");
  Font.setPointSize(12);
  QFontMetrics metrics(Font);
  Font.setWeight(QFont::Normal);
  painter0.setFont(Font);
  painter0.setPen(Qt::black);

  m_binsPerPixel = m_nSpan * 32768.0/(w*0.001*m_fSample) + 0.5;
  double FreqPerDiv=5.0;
  double df = m_binsPerPixel*0.001*m_fSample/32768.0;
  m_fSpan = w*df;
  m_ScalePixmap.fill(Qt::white);
  painter0.drawRect(0, 0, w, 30);

  // Wideband (upper) scale ticks and labels. Snap the first division to
  // the nearest round FreqPerDiv-kHz frequency at or after m_StartFreq,
  // rather than starting the grid at the raw left-edge frequency (which
  // is generally not a multiple of FreqPerDiv) -- otherwise labels read
  // e.g. 34,39,44,... instead of the intended 35,40,45,..., and the
  // ticks themselves sit up to FreqPerDiv/2 kHz off from where the
  // labels claim, not just a cosmetic mislabeling. Mirrors
  // CVertPlotter::drawScale()'s equivalent fix for the vertical
  // waterfall's own frequency scale.
  pixperdiv = FreqPerDiv/df;
  double f0 = ceil(m_StartFreq/FreqPerDiv)*FreqPerDiv;
  double x0 = (f0 - m_StartFreq)/df;
  int ndivs = qMin(63, (int)((m_StartFreq+m_fSpan-f0)/FreqPerDiv + 1.e-6));

  for (int i=0; i<=ndivs; i++) {
    x = (int)(x0 + i*pixperdiv + 0.5);
    painter0.drawLine(x,18,x,30);
    if (i>0) {
      for (int k=1; k<5; k++) {           //minor ticks between this major and the previous one
        int xm = (int)(x0 + (i-1)*pixperdiv + k*pixperdiv/5.0 + 0.5);
        painter0.drawLine(xm,24,xm,30);
      }
    }
    m_HDivText[i].setNum((int)(f0 + i*FreqPerDiv + 0.5));
  }

  //draw frequency values
  for (int i=0; i<=ndivs; i++) {
    x = (int)(x0 + i*pixperdiv + 0.5);
    rect0.setRect(x-(int)(pixperdiv/2),0, (int)pixperdiv, 20);
    painter0.drawText(rect0, Qt::AlignHCenter|Qt::AlignVCenter,
                     m_HDivText[i]);
  }


  if(m_2Dspec) {
    QPen pen0(Qt::green, 3);                 //Mark Cal Freq with green tick
    painter0.setPen(pen0);
    x = m_xClick;
    painter0.drawLine(x,15,x,30);
    int x0=(16384-m_i0)/m_binsPerPixel;
    m_fGreen=(x-x0)*df;
    x0 += (x0-x);
    QPen pen3(Qt::red, 3);
    painter0.setPen(pen3);
    if(x0>0 and x0<w) painter0.drawLine(x0,15,x0,30);
  } else {
    QPen pen0(Qt::green, 3);                 //Mark fQSO with green tick
    painter0.setPen(pen0);
    x = XfromFreq(float(fQSO()));
    painter0.drawLine(x,15,x,30);
    QPen pen3(Qt::red, 3);                  //Mark Tx freq (kHz) with red tick
    painter0.setPen(pen3);
    x = XfromFreq(float(m_TXkHz));
    painter0.drawLine(x,0,x,15);
  }

  // Now make the zoomed scale, using m_ZoomScalePixmap and painter3
  QRect rect1;
  QPainter painter3(&m_ZoomScalePixmap);
  painter3.setFont(Font);
  painter3.setPen(Qt::black);

  FreqPerDiv=0.2;
  df = 0.001*m_fSample/32768.0;
  m_hdivs = 32768*df/FreqPerDiv + 0.9999;
  int nlabs=df*w/0.2 + 1.0;
  m_ZoomScalePixmap.fill(Qt::white);
  painter3.drawRect(0, 0, w, 30);

  pixperdiv = FreqPerDiv/df;
  for( int i=0; i<10*nlabs; i++) {
    x = i*pixperdiv/10;
    y=24;
    if ((i%5) == 0) y=18;
    painter3.drawLine(x,y,x,30);
  }

  //draw frequency values
  MakeFrequencyStrs();
  for( int i=0; i<=nlabs; i++) {
    x = (int)( (float)i*pixperdiv - pixperdiv/2);
    rect1.setRect(x,0, (int)pixperdiv, 20);
    painter3.drawText(rect1, Qt::AlignHCenter|Qt::AlignVCenter,
                      m_HDivText[i]);
  }

  df=m_fSample/32768.0;
  if(m_bLockTxRx) m_TxDF=m_DF;
  x = (m_DF + m_mode65*66*11025.0/4096.0 - m_ZoomStartFreq)/df;
  QPen pen2(Qt::darkGreen, 3);      //Mark top JT65B tone with dark Green tick
  painter3.setPen(pen2);
  painter3.drawLine(x,15,x,30);
  x = (m_DF - m_ZoomStartFreq)/df;
  QPen pen1(Qt::green, 3);                //Mark DF with a green tick
  painter3.setPen(pen1);
  painter3.drawLine(x,15,x,30);
  for(int i=2; i<5; i++) {                //Mark the shorthand freqs
    x = (m_DF + m_mode65*10*i*11025.0/4096.0 - m_ZoomStartFreq)/df;
    painter3.drawLine(x,20,x,30);
  }
  int x1=(m_DF - m_tol - m_ZoomStartFreq)/df;
  int x2=(m_DF + m_tol - m_ZoomStartFreq)/df;
  pen1.setWidth(4);
  painter3.setPen(pen1);
  painter3.drawLine(x1,28,x2,28);

  pen1.setWidth(3);
  pen1.setColor(Qt::red);                  //Mark Tx DF with red tick
  painter3.setPen(pen1);
  x = (m_TxDF - m_ZoomStartFreq)/df;
  painter3.drawLine(x,0,x,15);
}

void CPlotter::MakeFrequencyStrs()        //MakeFrequencyStrs
{
  double StartFreq = m_StartFreq;
  double freq;
  int i,j;
  int FreqPerDiv=5;

  if(m_hdivs > 100) {
    m_FreqUnits = 1;
    FreqPerDiv = 200;
    int w = m_WaterfallPixmap.width();
    float df=m_fSample/32768.0;
    StartFreq = -w*df/2;
    int n=StartFreq/FreqPerDiv;
    StartFreq=n*200;
    m_ZoomStartFreq = (int)StartFreq;
  }
  int numfractdigits = (int)log10((double)m_FreqUnits);

  if(1 == m_FreqUnits) {
    //if units is Hz then just output integer freq
    for(int i=0; i<=m_hdivs; i++) {
      freq = StartFreq/(double)m_FreqUnits;
      m_HDivText[i].setNum((int)freq);
      StartFreq += FreqPerDiv;
    }
    return;
  }
  //here if is fractional frequency values
  //so create max sized text based on frequency units
  for(int i=0; i<=m_hdivs; i++) {
    freq = StartFreq/(double)m_FreqUnits;
    m_HDivText[i].setNum(freq,'f', numfractdigits);
    StartFreq += FreqPerDiv;
  }
  //now find the division text with the longest non-zero digit
  //to the right of the decimal point.
  int max = 0;
  for(i=0; i<=m_hdivs; i++) {
    int dp = m_HDivText[i].indexOf('.');
    int l = m_HDivText[i].length()-1;
    for(j=l; j>dp; j--) {
      if(m_HDivText[i][j] != '0')
        break;
    }
    if( (j-dp) > max)
      max = j-dp;
  }
  //truncate all strings to maximum fractional length
  StartFreq = m_CenterFreq - 0.5*m_fSpan;
  for( i=0; i<=m_hdivs; i++) {
    freq = (double)StartFreq/(double)m_FreqUnits;
    m_HDivText[i].setNum(freq,'f', max);
    StartFreq += FreqPerDiv;
  }
}

int CPlotter::XfromFreq(float f)                               //XfromFreq()
{
  float w = m_WaterfallPixmap.width();
  int x = (int) w * (f - m_StartFreq)/m_fSpan;
  if(x<0 ) return 0;
  if(x>(int)w) return m_WaterfallPixmap.width();
  return x;
}

float CPlotter::FreqfromX(int x)                               //FreqfromX()
{
  float w = m_WaterfallPixmap.width();
  double f =m_CenterFreq - 0.5*m_fSpan + m_fSpan * x/w;
  return f;
}

void CPlotter::setDecodeLabels(const QList<WideDecodeLabel>& labels)
{
  m_decodeLabels = labels;
  update();
}

void CPlotter::paintDecodeLabels(QPainter& painter)
{
  // Sort left-to-right so the stacking pass below assigns rows in order
  // of x-position.
  QList<WideDecodeLabel> sorted = m_decodeLabels;
  std::sort(sorted.begin(), sorted.end(),
            [this](WideDecodeLabel const& a, WideDecodeLabel const& b) {
                return XfromFreq(static_cast<float>(a.freq_khz))
                     < XfromFreq(static_cast<float>(b.freq_khz));
            });

  QFont font("Arial", 8, QFont::Bold);
  painter.setFont(font);
  QFontMetrics metrics(font);
  const int row_height = metrics.height() + 1;

  // Up to 5 stack rows -- plenty for how many simultaneous decoded
  // stations typically show up in one wideband span.
  constexpr int max_rows = 5;
  // Upper-waterfall geometry: paintEvent() draws the scale at y=0..30
  // and the waterfall row starting at y=30, height (m_Size.height()-60)/2.
  constexpr int waterfall_top_y = 30;
  int row_right_edge[max_rows];
  for (int i = 0; i < max_rows; ++i) row_right_edge[i] = -1000;

  for (auto const& l : sorted) {
    const int x = XfromFreq(static_cast<float>(l.freq_khz));
    const int text_w = metrics.horizontalAdvance(l.callsign);
    const int rect_w = text_w + 4;
    const int rect_x = x - rect_w / 2;

    int row = 0;
    for (; row < max_rows; ++row) {
      if (rect_x > row_right_edge[row] + 4) break;
    }
    if (row >= max_rows) continue;
    row_right_edge[row] = rect_x + rect_w;

    const int y_top = waterfall_top_y + row_height * row;
    const QRect rect(rect_x, y_top, rect_w, row_height);
    painter.fillRect(rect, QColor(0, 0, 0, 180));

    // Primary (60s) decodes yellow; second-30s-half decodes a distinct
    // hue (orange) rather than a shade, so they stay legible at a glance.
    const QColor col = l.second_half ? QColor(255, 165, 0) : QColor(255, 255, 0);
    painter.setPen(QColor(col.red(), col.green(), col.blue(), 200));
    // Tick points down into the waterfall trace at the label's exact frequency.
    painter.drawLine(x, y_top + row_height, x, y_top + row_height + 4);
    painter.setPen(col);
    painter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, l.callsign);
  }
}

void CPlotter::SetRunningState(bool running)              //SetRunningState()
{
  m_Running = running;
}

void CPlotter::setPlotZero(int plotZero)                  //setPlotZero()
{
  m_plotZero=plotZero;
}

int CPlotter::getPlotZero()                               //getPlotZero()
{
  return m_plotZero;
}

void CPlotter::setPlotGain(int plotGain)                  //setPlotGain()
{
  m_plotGain=plotGain;
}

int CPlotter::getPlotGain()                               //getPlotGain()
{
  return m_plotGain;
}

void CPlotter::SetCenterFreq(int f)                   //setCenterFreq()
{
// f is the integer kHz portion of cfreq, from Linrad packets
  if(f<0) f=m_nkhz;
  int ns = (f+m_FreqOffset-0.5*m_fSpan)/5.0 + 0.5;
  double fs = 5*ns;
  m_CenterFreq = fs + 0.5*m_fSpan;
}

qint64 CPlotter::centerFreq()                             //centerFreq()
{
  return m_CenterFreq;
}

void CPlotter::SetStartFreq(quint64 f)                    //SetStartFreq()
{
  m_StartFreq=f;
//    resizeEvent(NULL);
  DrawOverlay();
}

qint64 CPlotter::startFreq()                              //startFreq()
{
  return m_StartFreq;
}

void CPlotter::SetFreqOffset(quint64 f)                   //SetFreqOffset()
{
  m_FreqOffset=f;
  DrawOverlay();
}

qint64 CPlotter::freqOffset() {return m_FreqOffset;}         //freqOffset()
int CPlotter::plotWidth(){return m_WaterfallPixmap.width();}
void CPlotter::UpdateOverlay() {DrawOverlay();}
void CPlotter::setDataFromDisk(bool b) {m_dataFromDisk=b;}

void CPlotter::setTol(int n)                                 //setTol()
{
  m_tol=n;
  DrawOverlay();
}

void CPlotter::setBinsPerPixel(int n) {m_binsPerPixel = n;}  //set nbpp

int CPlotter::binsPerPixel(){return m_binsPerPixel;}         //get nbpp

void CPlotter::setFQSO(int x, bool bf)                       //setFQSO()
{
  if(bf) {
    m_fQSO=x;         // x is freq in kHz
  } else {
    if(x<0) x=0;      // x is pixel number
    if(x>m_Size.width()) x=m_Size.width();
    m_fQSO = int(FreqfromX(x)+0.5);
    m_xClick=x;
  }
  if(m_bLockTxRx) m_TXkHz=m_fQSO;
  m_TXfreq = floor(datcom_.fcenter) + 0.001*m_TXkHz;
  DrawOverlay();
  update();
}

void CPlotter::setFcal(int n)                                  //setFcal()
{
  m_fCal=n;
}

void CPlotter::setNkhz(int n)                                  //setNkhz()
{
  m_nkhz=n;
}

int CPlotter::fQSO() {return m_fQSO;}                          //get fQSO

int CPlotter::DF() {return m_DF;}                              // get DF

void CPlotter::mousePressEvent(QMouseEvent *event)       //mousePressEvent
{
  int h = (m_Size.height()-60)/2;
  int x=event->x();
  int y=event->y();
  int button=event->button();
  if(y < h+30) {                                      // Wideband waterfall
    if(button==1) {
      setFQSO(x,false);
      if(event->modifiers() & Qt::ControlModifier) emit freezeDecode1(3);
    }
    if(button==2 and !m_bLockTxRx) {
      if(x<0) x=0;      // x is pixel number
      if(x>m_Size.width()) x=m_Size.width();
      m_TXkHz = int(FreqfromX(x)+0.5);
      m_TXfreq = floor(datcom_.fcenter) + 0.001*m_TXkHz;
    }
  } else {                                            // Zoomed waterfall
    if(button==1) m_DF=int(m_ZoomStartFreq + x*m_fSample/32768.0);
    if(button==2 and !m_bLockTxRx) m_TxDF=int(m_ZoomStartFreq + x*m_fSample/32768.0);
    if(m_bLockTxRx) m_TxDF=m_DF;
  }
  DrawOverlay();
  update();
}

void CPlotter::mouseDoubleClickEvent(QMouseEvent *event)  //mouse2click
{
  if(event->button()!=1) return;       //Act only on left double-click
  int h = (m_Size.height()-60)/2;
  int x=event->x();
  int y=event->y();
  if(y < h+30) {
    m_DF=0;
    setFQSO(x,false);
    emit freezeDecode1(2);
  } else {
    float f = m_ZoomStartFreq + x*m_fSample/32768.0;
    m_DF=int(f);
    emit freezeDecode1(1);
    DrawOverlay();
  }
}

int CPlotter::autoZero()                                        //autoZero()
{
  m_z1=0;
  m_z2=0;
  int sum1=0;
  for(int i=0; i<256; i++) {
    sum1 += m_hist1[i];
    if(sum1 > m_Size.width()/2) {
      m_z1=i;
      break;
    }
  }
  int sum2=0;
  for(int i=0; i<256; i++) {
    sum2 += m_hist2[i];
    if(sum2 > 16384) {
      m_z2=i;
      break;
    }
  }
  double gain = pow(10.0,0.05*(m_plotGain+7));
//  double dz1 = (m_z1-38)/(5.0*gain);
  double dz2 = (m_z2-28)/(5.0*gain);
  if(m_z2 < 255) m_plotZero = int(m_plotZero + dz2 + 0.5);
  return m_plotZero;
}

void CPlotter::setNSpan(int n)                                  //setNSpan()
{
  m_nSpan=n;
}

void CPlotter::setPalette(QString palette)                      //setPalette()
{
  if(palette=="Linrad") {
    float twopi=6.2831853;
    float r,g,b,phi,x;
    for(int i=0; i<256; i++) {
      r=0.0;
      if(i>105 and i<=198) {
        phi=(twopi/4.0) * (i-105.0)/(198.0-105.0);
        r=sin(phi);
      } else if(i>=198) {
          r=1.0;
      }

      g=0.0;
      if(i>35 and i<198) {
        phi=(twopi/4.0) * (i-35.0)/(122.5-35.0);
        g=0.625*sin(phi);
      } else if(i>=198) {
        x=(i-186.0);
        g=-0.014 + 0.0144*x -0.00007*x*x +0.000002*x*x*x;
        if(g>1.0) g=1.0;
      }

      b=0.0;
      if(i<=117) {
        phi=(twopi/2.0) * i/117.0;
        b=0.4531*sin(phi);
      } else if(i>186) {
        x=(i-186.0);
        b=-0.014 + 0.0144*x -0.00007*x*x +0.000002*x*x*x;
        if(b>1.0) b=1.0;
      }
      m_ColorTbl[i].setRgb(int(255.0*r),int(255.0*g),int(255.0*b));
    }
    m_ColorTbl[255].setRgb(255,255,100);

  }

  if(palette=="CuteSDR") {
      for( int i=0; i<256; i++) {
      if( (i<43) )
        m_ColorTbl[i].setRgb( 0,0, 255*(i)/43);
      if( (i>=43) && (i<87) )
        m_ColorTbl[i].setRgb( 0, 255*(i-43)/43, 255 );
      if( (i>=87) && (i<120) )
        m_ColorTbl[i].setRgb( 0,255, 255-(255*(i-87)/32));
      if( (i>=120) && (i<154) )
        m_ColorTbl[i].setRgb( (255*(i-120)/33), 255, 0);
      if( (i>=154) && (i<217) )
        m_ColorTbl[i].setRgb( 255, 255 - (255*(i-154)/62), 0);
      if( (i>=217)  )
        m_ColorTbl[i].setRgb( 255, 0, 128*(i-217)/38);
    }
    m_ColorTbl[255].setRgb(255,255,100);
  }
}

void CPlotter::setFsample(int n)
{
  m_fSample=n;
}

void CPlotter::setMode65(int n)
{
  m_mode65=n;
  DrawOverlay();                         //Redraw scales and ticks
  update();                              //trigger a new paintEvent
}

void CPlotter::set2Dspec(bool b)
{
  m_2Dspec=b;
  m_paintAllZoom=!b;
  DrawOverlay();                         //Redraw scales and ticks
  update();                              //trigger a new paintEvent}
}

double CPlotter::fGreen()
{
  return m_fGreen;
}

void CPlotter::setLockTxRx(bool b)
{
  m_bLockTxRx=b;
  if(m_bLockTxRx) {
    m_TXkHz=m_fQSO;
    m_TXfreq=floor(m_TXfreq) + 0.001*m_TXkHz;
    m_TxDF=m_DF;
    DrawOverlay();                         //Redraw scales and ticks
    update();                              //trigger a new paintEvent}
  }
}

void CPlotter::mouseMoveEvent (QMouseEvent * event)
{
  int h = m_WaterfallPixmap.height();
  int x=event->x();
  int y=event->y();
  bool lower=(y > 30+h);
  float freq=FreqfromX(x);
  float df=m_fSample/32768.0;
  int ndf=x*df + m_ZoomStartFreq;
  if(lower) {
    QToolTip::showText(event->globalPos(),QString::number(ndf));
  } else {
    QToolTip::showText(event->globalPos(),QString::number(freq,'f',3));
  }
  QWidget::mouseMoveEvent(event);
}

double CPlotter::rxFreq()
{
  return floor(datcom_.fcenter) + 0.001*m_fQSO + 0.000001*m_DF;
}

double CPlotter::txFreq()
{
  return m_TXfreq + 0.000001*m_TxDF;
}
