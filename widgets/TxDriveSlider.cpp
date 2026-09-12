#include "TxDriveSlider.hpp"

#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

namespace
{
  constexpr int minor_tick_interval = 50;
}

TxDriveSlider::TxDriveSlider (QWidget * parent)
  : QSlider {parent}
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  // One-sided ticks preserve the native ticked handle and matching hit geometry.
  setTickPosition (QSlider::TicksRight);
  setTickInterval (minor_tick_interval);
#endif
}

QSize TxDriveSlider::sizeHint () const
{
  auto size = QSlider::sizeHint ();
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  size.rwidth () += 12;
#endif
  return size;
}

QSize TxDriveSlider::minimumSizeHint () const
{
  auto size = QSlider::minimumSizeHint ();
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  size.rwidth () += 12;
#endif
  return size;
}

void TxDriveSlider::paintEvent (QPaintEvent * event)
{
#ifdef Q_OS_WIN
  if (!property ("wsjtxDarkStyle").toBool ())
    {
      QSlider::paintEvent (event);
      return;
    }
#endif
  constexpr int major_tick_interval = 100;
  if (orientation () != Qt::Vertical || maximum () <= minimum ())
    {
      QSlider::paintEvent (event);
      return;
    }

  QStyleOptionSlider option;
  initStyleOption (&option);

  auto const groove = style ()->subControlRect (QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  // Style-reported handle endpoints keep painting aligned with interaction geometry.
  auto endpoint = option;
  endpoint.sliderPosition = endpoint.sliderValue = minimum ();
  auto const minimum_y = style ()->subControlRect (QStyle::CC_Slider, &endpoint, QStyle::SC_SliderHandle, this).center ().y ();
  endpoint.sliderPosition = endpoint.sliderValue = maximum ();
  auto const maximum_y = style ()->subControlRect (QStyle::CC_Slider, &endpoint, QStyle::SC_SliderHandle, this).center ().y ();
  auto const groove_top = qMin (minimum_y, maximum_y);
  auto const span = qAbs (maximum_y - minimum_y);
#else
  auto const handle = style ()->subControlRect (QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
  auto const span = qMax (0, groove.height () - handle.height ());
  auto const groove_top = groove.top () + handle.height () / 2;
#endif
  if (span == 0)
    {
      QSlider::paintEvent (event);
      return;
    }

  auto const groove_x = groove.center ().x ();
  auto const groove_bottom = groove_top + span;
  auto const tick_gap = 4;

  QPainter painter {this};

  auto const palette_mid = palette ().color (QPalette::Mid);
  // Some application styles use an accent color for Mid. This scale conveys
  // attenuation positions, not a filled level, so retain its lightness while
  // removing hue and saturation.
  auto const neutral_mid = QColor::fromHsl (0, 0, palette_mid.lightness ());
  auto groove_color = neutral_mid;
  groove_color.setAlpha (120);
  painter.setRenderHint (QPainter::Antialiasing, true);
  painter.setPen (QPen {groove_color, 3, Qt::SolidLine, Qt::RoundCap});
  painter.drawLine (groove_x, groove_top, groove_x, groove_bottom);

  auto tick_color = neutral_mid;
  tick_color.setAlpha (190);
  painter.setRenderHint (QPainter::Antialiasing, false);
  painter.setPen (QPen {tick_color, 1});

  for (auto value = minimum (); value <= maximum (); value += minor_tick_interval)
    {
      auto const position = QStyle::sliderPositionFromValue (minimum (), maximum (), value, span, option.upsideDown);
      auto const y = groove_top + position;
      auto const is_major_tick = value % major_tick_interval == 0;
      auto const tick_length = is_major_tick ? 5 : 3;
      painter.drawLine (groove_x - tick_gap - tick_length, y, groove_x - tick_gap, y);
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
      painter.drawLine (groove_x + tick_gap, y, groove_x + tick_gap + tick_length, y);
#endif
    }

  option.subControls = QStyle::SC_SliderHandle;
#if !defined(Q_OS_MACOS)
  // QStyleSheetStyle may paint its highlighted add/sub-page even when only the
  // handle subcontrol is requested. Keep that painting inside the handle so it
  // cannot cover the neutral attenuation scale.
  auto const handle_rect = style ()->subControlRect (
    QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
  painter.setClipRect (handle_rect, Qt::IntersectClip);
#else
  if (property ("wsjtxDarkStyle").toBool ())
    {
      auto const handle_rect = style ()->subControlRect (
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
      painter.setClipRect (handle_rect, Qt::IntersectClip);
    }
#endif
  style ()->drawComplexControl (QStyle::CC_Slider, &option, &painter, this);
}
