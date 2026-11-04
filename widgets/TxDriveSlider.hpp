// -*- Mode: C++ -*-
#ifndef TXDRIVESLIDER_HPP
#define TXDRIVESLIDER_HPP

#include <QColor>
#include <QPalette>
#include <QSlider>

class TxDriveSlider final
  : public QSlider
{
  Q_OBJECT

public:
  explicit TxDriveSlider (QWidget * parent = nullptr);
  QSize sizeHint () const override;
  QSize minimumSizeHint () const override;

  // Hue-free colour for the attenuation scale: the palette's Mid lightness,
  // or WindowText when Mid is too close to the Window background to be seen.
  static QColor scaleColor (QPalette const&);

protected:
  void paintEvent (QPaintEvent * event) override;
};

#endif // TXDRIVESLIDER_HPP
