#include "Rr73Policy.hpp"

namespace Rr73Policy
{
  bool tx4AllowsRr73 (QString const& mode, bool shortMessages)
  {
    if ("FT8" == mode || "FT4" == mode || "FST4" == mode)
      return true;

    if ("MSK144" == mode || "Q65" == mode)
      return !shortMessages;

    return false;
  }
}
