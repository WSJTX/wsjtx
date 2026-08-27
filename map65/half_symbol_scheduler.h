#ifndef MAP65_HALF_SYMBOL_SCHEDULER_H
#define MAP65_HALF_SYMBOL_SCHEDULER_H

#include <atomic>

class Map65HalfSymbolScheduler
{
public:
  void reset () noexcept { scheduled_half_symbol_ = 0; }

  bool claim_next (int available_half_symbol, std::atomic_bool& busy) noexcept
  {
    if (available_half_symbol <= scheduled_half_symbol_)
      {
        return false;
      }

    bool expected = false;
    if (!busy.compare_exchange_strong (expected, true,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return false;
      }

    ++scheduled_half_symbol_;
    return true;
  }

  int scheduled_half_symbol () const noexcept { return scheduled_half_symbol_; }

private:
  int scheduled_half_symbol_ {0};
};

#endif
