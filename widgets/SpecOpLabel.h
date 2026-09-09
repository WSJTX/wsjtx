#ifndef SPECOPLABEL_H
#define SPECOPLABEL_H

#include <QString>

#include "SpecialOperatingActivity.hpp"

namespace SpecOpLabel
{
  QString label (SpecialOperatingActivity specialOperation, bool ncccSprint, bool superFox = false);
}

#endif
