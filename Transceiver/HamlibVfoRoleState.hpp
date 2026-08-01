#ifndef HAMLIB_VFO_ROLE_STATE_HPP_
#define HAMLIB_VFO_ROLE_STATE_HPP_

#include <tuple>
#include <utility>

#include <hamlib/rig.h>

class HamlibVfoRoleState final
{
public:
  void reset () noexcept
  {
    reversed_ = false;
  }

  void observe_active_vfo (vfo_t active_vfo, bool split_ptt_active) noexcept
  {
    if (!split_ptt_active)
      {
        reversed_ = RIG_VFO_B == active_vfo;
      }
  }

  std::tuple<vfo_t, vfo_t> resolve (vfo_t rx_vfo, vfo_t tx_vfo) const noexcept
  {
    if (reversed_)
      {
        std::swap (rx_vfo, tx_vfo);
      }
    return std::make_tuple (rx_vfo, tx_vfo);
  }

  bool reversed () const noexcept
  {
    return reversed_;
  }

private:
  bool reversed_ {false};
};

#endif
