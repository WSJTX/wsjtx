#ifndef HAMLIB_MODE_HPP_
#define HAMLIB_MODE_HPP_

#include <hamlib/rig.h>

#include "Transceiver.hpp"

namespace HamlibMode
{
  Transceiver::MODE from_hamlib (rmode_t);
  rmode_t to_hamlib (Transceiver::MODE);
  bool satisfies_request (rmode_t requested, rmode_t current);
  bool change_required (Transceiver::MODE requested, rmode_t current);
  int get_uncached_mode (RIG *, vfo_t, rmode_t *, pbwidth_t *);
}

#endif
