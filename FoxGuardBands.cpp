#include "FoxGuardBands.hpp"

#include <QtGlobal>

namespace
{
  using Radio::Frequency;

  FoxGuardBands::Match no_match ()
  {
    return {false, FoxGuardBands::GuardKind::None, 0};
  }
}

namespace FoxGuardBands
{
  Match check (Frequency dial_frequency,
               std::initializer_list<Frequency> standard_ft8_guards,
               std::initializer_list<Frequency> wspr_guards)
  {
    for (auto guard_frequency : standard_ft8_guards)
    {
      auto const offset = static_cast<qint64> (dial_frequency) - static_cast<qint64> (guard_frequency);
      if (qAbs (offset) < 3000)
      {
        return {true, GuardKind::StandardFT8, guard_frequency};
      }
    }

    for (auto guard_frequency : wspr_guards)
    {
      auto const offset = static_cast<qint64> (dial_frequency) - static_cast<qint64> (guard_frequency);
      if (offset > -3500 && offset < 300)
      {
        return {true, GuardKind::WSPR, guard_frequency};
      }
    }

    return no_match ();
  }

  Match check (Frequency dial_frequency)
  {
    return check (dial_frequency,
                  {
                    1840000, 3573000, 7074000, 10136000, 14074000, 18100000,
                    21074000, 24915000, 28074000, 50313000, 70154000
                  },
                  {
                    1836600, 5364700, 3568600, 7038600, 10138700, 14095600,
                    18104600, 21094600, 24924600, 28124600
                  });
  }
}
