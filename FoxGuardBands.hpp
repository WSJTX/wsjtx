#ifndef FOX_GUARD_BANDS_HPP__
#define FOX_GUARD_BANDS_HPP__

#include <initializer_list>

#include "Radio.hpp"

namespace FoxGuardBands
{
  enum class GuardKind
  {
    None,
    StandardFT8,
    WSPR
  };

  struct Match
  {
    bool blocked;
    GuardKind kind;
    Radio::Frequency guard_frequency;
  };

  Match check (Radio::Frequency dial_frequency,
               std::initializer_list<Radio::Frequency> standard_ft8_guards,
               std::initializer_list<Radio::Frequency> wspr_guards);
  Match check (Radio::Frequency dial_frequency);
}

#endif
