#include "widgets/SpecOpLabel.h"

namespace SpecOpLabel
{
  QString label (SpecialOperatingActivity specialOperation, bool ncccSprint)
  {
    if (SpecialOperatingActivity::NA_VHF == specialOperation && ncccSprint) return "NCCC Sprint";
    if (SpecialOperatingActivity::NA_VHF == specialOperation) return "NA VHF";
    if (SpecialOperatingActivity::EU_VHF == specialOperation) return "EU VHF";
    if (SpecialOperatingActivity::FIELD_DAY == specialOperation) return "Field Day";
    if (SpecialOperatingActivity::RTTY == specialOperation) return "FT RU";
    if (SpecialOperatingActivity::WW_DIGI == specialOperation) return "WW Digi";
    if (SpecialOperatingActivity::ARRL_DIGI == specialOperation) return "ARRL Digi";
    if (SpecialOperatingActivity::Q65_PILEUP == specialOperation) return "Q65 Pileup";
    return {};
  }
}
