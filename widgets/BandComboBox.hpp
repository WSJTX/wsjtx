#ifndef BAND_COMBO_BOX_HPP__
#define BAND_COMBO_BOX_HPP__

#include <QComboBox>

class BandComboBox
  : public QComboBox
{
public:
  explicit BandComboBox (QWidget * = nullptr);

private:
  void showPopup () override;
};

#endif
