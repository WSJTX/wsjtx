#ifndef RESTRICTED_SPIN_BOX_HPP_
#define RESTRICTED_SPIN_BOX_HPP_

#include "HintedSpinBox.hpp"

class QString;

//
// RestrictedSpinBox - select only from a sequence of values
//
class RestrictedSpinBox final
  : public HintedSpinBox
{
public:
  RestrictedSpinBox (QWidget * parent = nullptr)
    : HintedSpinBox {parent}
  {
  }

protected:
  // override the base class validation
  QValidator::State validate (QString& input, int& pos) const override;
  void fixup (QString& input) const override;
};

#endif
