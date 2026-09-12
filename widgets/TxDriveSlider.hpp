// -*- Mode: C++ -*-
#ifndef TXDRIVESLIDER_HPP
#define TXDRIVESLIDER_HPP

#include <QSlider>

class TxDriveSlider final
  : public QSlider
{
  Q_OBJECT

public:
  explicit TxDriveSlider (QWidget * parent = nullptr);
  QSize sizeHint () const override;
  QSize minimumSizeHint () const override;

protected:
  void paintEvent (QPaintEvent * event) override;
};

#endif // TXDRIVESLIDER_HPP
