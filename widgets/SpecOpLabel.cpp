#include "widgets/SpecOpLabel.h"

#include <QCoreApplication>

namespace SpecOpLabel
{
  QString label (SpecialOperatingActivity specialOperation, bool ncccSprint, bool superFox)
  {
    if (SpecialOperatingActivity::FOX == specialOperation) {
      return superFox ? QCoreApplication::translate("MainWindow", "Super Fox")
                      : QCoreApplication::translate("MainWindow", "Fox");
    }
    if (SpecialOperatingActivity::HOUND == specialOperation) {
      return superFox ? QCoreApplication::translate("MainWindow", "Super Hound")
                      : QCoreApplication::translate("MainWindow", "Hound");
    }
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
