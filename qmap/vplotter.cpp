#include "vplotter.h"
#include <algorithm>
#include <math.h>
#include <QPainter>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QtMath>

CVertPlotter::CVertPlotter(QWidget *parent) :
  QFrame(parent),
  m_decodeClickCoalescer {this, [this] (QByteArray const& decodeRow, DecodeClickGesture gesture) {
    emit decodeLabelClicked (decodeRow, gesture);
  }}
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_NoSystemBackground, true);

  m_scaleWidth   = 40;
  m_labelWidth   = 90;
  m_startFreqKHz = 0.0;
  m_fSpanKHz     = 1.0;
  m_hdivs        = 0;
  m_size         = QSize(0,0);
  m_waterfallPixmap = QPixmap(0,0);
  m_scalePixmap     = QPixmap(0,0);
  setPalette("Linrad");
}

CVertPlotter::~CVertPlotter() { }

QSize CVertPlotter::minimumSizeHint() const
{
  return QSize(m_scaleWidth + m_labelWidth + 40, 100);
}

QSize CVertPlotter::sizeHint() const
{
  return QSize(m_scaleWidth + m_labelWidth + 120, 800);
}

void CVertPlotter::resizeEvent(QResizeEvent*)
{
  if(!size().isValid()) return;
  if(m_size != size()) {
    m_size = size();
    int w = qMax(1, m_size.width() - m_scaleWidth - m_labelWidth);
    int h = m_size.height();
    m_waterfallPixmap = QPixmap(w,h);
    m_waterfallPixmap.fill(Qt::black);
    m_scalePixmap = QPixmap(m_scaleWidth,h);
    m_scalePixmap.fill(QColor(235,235,230));
    rebuildFromHistory();
  }
  drawScale();
}

// y=0 is the top of the spectrogram (highest frequency in span); y=h-1
// is the bottom (lowest frequency), matching "frequency runs bottom to
// top" as requested.
int CVertPlotter::yFromFreq(double freq_khz) const
{
  int h = m_waterfallPixmap.height();
  if(h<=0 || m_fSpanKHz<=0.0) return 0;
  double frac = (freq_khz - m_startFreqKHz)/m_fSpanKHz;   // 0=bottom .. 1=top
  int y = int((1.0 - frac)*h + 0.5);
  if(y<0) y=0;
  if(y>h-1) y=h-1;
  return y;
}

void CVertPlotter::draw(const float swide[], int n, double startFreqKHz, double fSpanKHz,
                        int plotZero, int plotGain)
{
  if(n<=0 || m_waterfallPixmap.isNull()) return;
  m_startFreqKHz = startFreqKHz;
  m_fSpanKHz     = fSpanKHz;

  // Remember this column (raw, pre-color-map) so a later resize can
  // repaint the spectrogram instead of blanking it.
  {
    HistoryLine line;
    line.swide.resize(n);
    for (int k=0; k<n; k++) line.swide[k]=swide[k];
    line.startFreqKHz = startFreqKHz;
    line.fSpanKHz     = fSpanKHz;
    line.plotZero     = plotZero;
    line.plotGain     = plotGain;
    m_history.prepend(line);
    if (m_history.size() > kMaxHistory) m_history.removeLast();
  }

  int w = m_waterfallPixmap.width();
  int h = m_waterfallPixmap.height();
  double gain = pow(10.0,0.05*(plotGain+7));

  //move current data left one column (must do this before attaching a QPainter)
  m_waterfallPixmap.scroll(-1,0,0,0,w,h);
  QPainter painter(&m_waterfallPixmap);

  for(int y=0; y<h; y++) {
    // Row y (top=high freq) maps to sample j (increasing index = increasing freq)
    int j = int((double)(h-1-y)*(n-1)/qMax(1,h-1) + 0.5);
    if(j<0) j=0;
    if(j>n-1) j=n-1;
    float sv = swide[j];
    if(sv<0) sv=-sv;                    //strong-signal tag, ignored for color here
    double yv = 10.0*log10(sv);
    int y1 = int(5.0*gain*(yv + 29 - plotZero));
    if(y1<0) y1=0;
    if(y1>254) y1=254;
    if(sv>1.e29) y1=255;
    painter.setPen(m_ColorTbl[y1]);
    painter.drawPoint(w-1,y);
  }

  drawScale();
  update();
}

// Repaints the spectrogram from m_history, for use right after a resize
// recreates m_waterfallPixmap as blank. Same color-mapping formula as
// draw()'s loop (kept as a separate small duplicate rather than shared,
// same tradeoff as CPlotter::rebuildWideFromHistory). A column narrower
// or wider than the current width is left as-is (each column already
// resamples to the current height independent of its own sample count).
void CVertPlotter::rebuildFromHistory()
{
  if (m_waterfallPixmap.isNull()) return;
  int w = m_waterfallPixmap.width();
  int h = m_waterfallPixmap.height();
  m_waterfallPixmap.fill(Qt::black);
  QPainter painter(&m_waterfallPixmap);

  int cols = qMin(w, m_history.size());
  for (int age=0; age<cols; age++) {
    HistoryLine const& line = m_history.at(age);
    int n = line.swide.size();
    if (n<=0) continue;
    int x = w-1-age;
    double gain = pow(10.0,0.05*(line.plotGain+7));
    for (int y=0; y<h; y++) {
      int j = int((double)(h-1-y)*(n-1)/qMax(1,h-1) + 0.5);
      if(j<0) j=0;
      if(j>n-1) j=n-1;
      float sv = line.swide.at(j);
      if(sv<0) sv=-sv;
      double yv = 10.0*log10(sv);
      int y1 = int(5.0*gain*(yv + 29 - line.plotZero));
      if(y1<0) y1=0;
      if(y1>254) y1=254;
      if(sv>1.e29) y1=255;
      painter.setPen(m_ColorTbl[y1]);
      painter.drawPoint(x,y);
    }
  }
}

// Chooses a round kHz step (5/10/25/50/...) that keeps ticks from
// crowding, then the first (bottom-most) tick frequency at or above
// m_startFreqKHz, and formats the label text for each tick.
void CVertPlotter::makeFrequencyStrs()
{
  m_hdivs = 0;
  int h = m_scalePixmap.height();
  if(h<=0 || m_fSpanKHz<=0.0) return;

  static const double steps[] = {5.0,10.0,25.0,50.0,100.0,200.0,500.0,1000.0};
  double pixPerKHz = h/m_fSpanKHz;
  m_freqPerDiv = steps[0];
  for (double step : steps) {
    m_freqPerDiv = step;
    if (step*pixPerKHz >= 24.0) break;
  }

  m_freq0 = qCeil(m_startFreqKHz/m_freqPerDiv)*m_freqPerDiv;
  m_hdivs = qMin(63, int((m_startFreqKHz+m_fSpanKHz-m_freq0)/m_freqPerDiv + 1.e-6));
  for(int i=0; i<=m_hdivs; i++) {
    double f = m_freq0 + i*m_freqPerDiv;
    m_HDivText[i] = QString::number(f, 'f', 0);
  }
}

void CVertPlotter::drawScale()
{
  if(m_scalePixmap.isNull()) return;
  int w = m_scalePixmap.width();

  QPainter painter(&m_scalePixmap);
  QFont font("Arial");
  font.setPointSize(8);
  painter.setFont(font);
  m_scalePixmap.fill(QColor(235,235,230));
  painter.setPen(QColor(120,120,115));
  painter.drawLine(0,0,0,m_scalePixmap.height());   // thin rule against the spectrogram edge

  makeFrequencyStrs();
  painter.setPen(Qt::black);
  for(int i=0; i<=m_hdivs; i++) {
    double f = m_freq0 + i*m_freqPerDiv;
    int y = yFromFreq(f);
    painter.drawLine(0,y,6,y);
    QRect rect0(8, y-8, w-10, 16);
    painter.drawText(rect0, Qt::AlignLeft|Qt::AlignVCenter, m_HDivText[i]);
  }
}

void CVertPlotter::setDecodeLabels(const QList<QMapDecodeLabel>& labels)
{
  m_decodeLabels = labels;
  update();
}

QFont CVertPlotter::labelFont() const
{
  QFont font("Arial");
  font.setPointSize(9);
  return font;
}

// Displace nearby labels downward in frequency order to keep them legible.
QVector<CVertPlotter::LabelLayout> CVertPlotter::computeLayout(QFontMetrics const& fm, int minSpacing) const
{
  int labelX = m_waterfallPixmap.width() + m_scaleWidth;
  int indentPx = fm.horizontalAdvance("MM");

  QVector<LabelLayout> out;
  out.reserve(m_decodeLabels.size());
  for (auto const& lab : qAsConst(m_decodeLabels)) {
    LabelLayout ll;
    ll.label = lab;
    ll.trueY = ll.dispY = yFromFreq(lab.receiveFrequencyKHz);
    ll.textX = labelX + 14 + (lab.secondHalf ? indentPx : 0);
    out.append(ll);
  }
  std::sort(out.begin(), out.end(), [](LabelLayout const& a, LabelLayout const& b) {
    return a.trueY < b.trueY;
  });
  for (int i=1; i<out.size(); ++i) {
    if (out[i].dispY < out[i-1].dispY + minSpacing)
      out[i].dispY = out[i-1].dispY + minSpacing;
  }
  for (auto& ll : out) {
    int text_w = fm.horizontalAdvance(ll.label.callsign);
    ll.rect = QRect(ll.textX - 2, ll.dispY + 4 - fm.ascent(), text_w + 4, fm.height());
  }
  return out;
}

bool CVertPlotter::hitTestDecodeLabel(QPoint const& pos, QMapDecodeLabel& label)
{
  QFontMetrics fm(labelFont());
  for (auto const& ll : computeLayout(fm, fm.height() + 2)) {
    if (ll.rect.contains(pos)) {
      label = ll.label;
      return true;
    }
  }
  return false;
}

void CVertPlotter::mousePressEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton) return;
  QMapDecodeLabel label;
  if (hitTestDecodeLabel(event->pos(), label)) {
    m_decodeClickCoalescer.press(label.callsign, label.raw);
  }
}

void CVertPlotter::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (event->button() != Qt::LeftButton) return;
  QMapDecodeLabel label;
  if (hitTestDecodeLabel(event->pos(), label)) {
    m_decodeClickCoalescer.doubleClick(label.callsign, label.raw);
  }
}

void CVertPlotter::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  int w = m_waterfallPixmap.width();
  painter.drawPixmap(0,0,m_waterfallPixmap);
  painter.drawPixmap(w,0,m_scalePixmap);

  int labelX = w + m_scaleWidth;
  painter.fillRect(labelX, 0, m_labelWidth, height(), QColor(255,255,240));

  QFont font = labelFont();
  painter.setFont(font);
  QFontMetrics fm(font);
  int minSpacing = fm.height() + 2;

  QColor dotColor(230,200,40);
  QColor leaderColor(150,150,140);
  for (auto const& ll : computeLayout(fm, minSpacing)) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(dotColor);
    painter.drawEllipse(QPoint(labelX+5,ll.trueY), 3, 3);
    if (ll.dispY != ll.trueY || ll.label.secondHalf) {
      painter.setPen(QPen(leaderColor,1));
      painter.drawLine(labelX+8, ll.trueY, ll.textX-2, ll.dispY);
    }
    painter.setPen(Qt::black);
    painter.drawText(ll.textX, ll.dispY+4, ll.label.callsign);
  }
}

void CVertPlotter::setPalette(QString palette)
{
  if(palette=="Linrad") {
    float twopi=6.2831853f;
    float r,g,b,phi,x;
    for(int i=0; i<256; i++) {
      r=0.0;
      if(i>105 and i<=198) {
        phi=(twopi/4.0f) * (i-105.0f)/(198.0f-105.0f);
        r=sin(phi);
      } else if(i>=198) {
        r=1.0;
      }

      g=0.0;
      if(i>35 and i<198) {
        phi=(twopi/4.0f) * (i-35.0f)/(122.5f-35.0f);
        g=0.625f*sin(phi);
      } else if(i>=198) {
        x=(i-186.0f);
        g=-0.014f + 0.0144f*x -0.00007f*x*x +0.000002f*x*x*x;
        if(g>1.0) g=1.0;
      }

      b=0.0;
      if(i<=117) {
        phi=(twopi/2.0f) * i/117.0f;
        b=0.4531f*sin(phi);
      } else if(i>186) {
        x=(i-186.0f);
        b=-0.014f + 0.0144f*x -0.00007f*x*x +0.000002f*x*x*x;
        if(b>1.0) b=1.0;
      }
      m_ColorTbl[i].setRgb(int(255.0*r),int(255.0*g),int(255.0*b));
    }
    m_ColorTbl[255].setRgb(255,255,100);
  }

  if(palette=="CuteSDR") {
    for(int i=0; i<256; i++) {
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
