// Simple bargraph dB meter
// Implemented by Edson Pereira PY2SDR
//

#include "signalmeter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QFontMetrics>
#include <QDebug>

#include "meterwidget.h"

#include "moc_signalmeter.cpp"

#define MAXDB 90

class Scale final
  : public QWidget
{
public:
  explicit Scale (QWidget * parent = 0)
    : QWidget {parent}
  {
    setSizePolicy (QSizePolicy::Minimum, QSizePolicy::MinimumExpanding);
  }

  QSize sizeHint () const override
  {
    return minimumSizeHint ();
  }

  QSize minimumSizeHint () const override
  {
    QFontMetrics font_metrics {font (), nullptr};
    return {tick_length + text_indent + font_metrics.boundingRect ("00+").width (), (font_metrics.height () + line_spacing) * range};
  }

protected:
  void paintEvent (QPaintEvent * event) override
  {
    QWidget::paintEvent (event);

    QPainter p {this};
    auto const& target = contentsRect ();
    QFontMetrics font_metrics {p.font (), this};
    auto font_offset = font_metrics.ascent () / 2;
    p.drawLine (target.left (), target.top () + font_offset, target.left (), target.bottom () - font_offset - font_metrics.descent ());
    for (int i = 0; i <= range; ++i)
      {
        p.save ();
        p.translate (target.left ()
                     , target.top () + font_offset + i * (target.height () - font_metrics.ascent () - font_metrics.descent ()) / range);
        p.drawLine (0, 0, tick_length, 0);
	if((i%2==1)) {
	  auto text = QString::number ((range - i) * scale);
	  p.drawText (tick_length + text_indent, font_offset, text);
	}
        p.restore ();
      }
  }

private:
  static int constexpr tick_length {4};
  static int constexpr text_indent {2};
  static int constexpr line_spacing {0};
  static int constexpr range {MAXDB/10};
  static int constexpr scale {10};
};

SignalMeter::SignalMeter (QWidget * parent)
  : QFrame {parent}
{
  auto outer_layout = new QVBoxLayout;
  outer_layout->setSpacing (0);

  auto inner_layout = new QHBoxLayout;
  inner_layout->setContentsMargins (1, 0, 1, 0);
  inner_layout->setSpacing (0);

  m_meter = new MeterWidget;
  m_meter->setSizePolicy (QSizePolicy::Minimum, QSizePolicy::Minimum);
  inner_layout->addWidget (m_meter);

  m_scale = new Scale;
  inner_layout->addWidget (m_scale);

  m_reading = new QLabel(this);

  outer_layout->addLayout (inner_layout);
  outer_layout->addWidget (m_reading);
  setLayout (outer_layout);
}

void SignalMeter::setValue(float value, float valueMax)
{
  if(value<0) value=0;
  QFontMetrics font_metrics {m_scale->font (), nullptr};
  m_meter->setContentsMargins (0, font_metrics.ascent () / 2, 0, font_metrics.ascent () / 2 + font_metrics.descent ());
  m_meter->setValue(int(value));
  m_meter->set_sigPeak(valueMax);
  QString t;
  t = t.asprintf("%d dB",int(value+0.5));
  m_reading->setText(t);
}
