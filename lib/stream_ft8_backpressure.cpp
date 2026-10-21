// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's multithreaded FT8 decode scheduler for lib/streaming_io.f90:
// one per process, as the GUI has one per main window.
#include <cstdint>

#include "Ft8MtdDecodeScheduler.hpp"

namespace
{
  Ft8MtdDecodeScheduler scheduler;

  // streaming_io.f90's stage numbers: 1 early one, 2 early two, 3 final.
  Ft8MtdDecodeScheduler::Stage stage_of (int stage)
  {
    switch (stage)
      {
      case 1: return Ft8MtdDecodeScheduler::Stage::EarlyOne;
      case 2: return Ft8MtdDecodeScheduler::Stage::EarlyTwo;
      case 3: return Ft8MtdDecodeScheduler::Stage::Final;
      default: return Ft8MtdDecodeScheduler::Stage::None;
      }
  }
}

extern "C"
{
  // 0 publish, 1 skip the early stage, 2 defer the final, 3 replace the pending final.
  int stream_ft8_backpressure_request (int stage, std::int64_t period, int early_stage_count,
                                       int busy, int final_pending)
  {
    switch (scheduler.request (stage_of (stage), period, early_stage_count, busy != 0,
                               final_pending != 0).action)
      {
      case Ft8MtdDecodeScheduler::Action::SkipEarly: return 1;
      case Ft8MtdDecodeScheduler::Action::DeferFinal: return 2;
      case Ft8MtdDecodeScheduler::Action::ReplaceFinal: return 3;
      default: return 0;
      }
  }

  void stream_ft8_backpressure_published (int stage, std::int64_t period)
  {
    scheduler.published (stage_of (stage), period);
  }

  void stream_ft8_backpressure_completed (int stage, std::int64_t period)
  {
    scheduler.completed (stage_of (stage), period);
  }

  void stream_ft8_backpressure_cancel ()
  {
    scheduler.cancel ();
  }
}
