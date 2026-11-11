#ifndef MESSAGEFILTERRULES_HPP
#define MESSAGEFILTERRULES_HPP

#include "MessageFilterLogic.hpp"

#include <QString>

namespace MessageFilterRules
{
  struct Evaluation
  {
    MessageFilterLogic::FilterResult result;
    bool filtersApplied {false};
  };

  Evaluation evaluateMSK144Text(QString const& payload, QString const& sender,
                               MessageFilterLogic::FilterContext const& ctx);
}

#endif // MESSAGEFILTERRULES_HPP
