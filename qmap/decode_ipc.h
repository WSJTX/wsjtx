#ifndef QMAP_DECODE_IPC_H
#define QMAP_DECODE_IPC_H

#include <cstddef>
#include <type_traits>

namespace qmap_decode_ipc
{
  // Keep row dimensions in sync with qmap_decode_ipc.f90.
  constexpr std::size_t shared_memory_size {4096};
  constexpr std::size_t max_rows {50};
  constexpr std::size_t row_size {80};
  constexpr std::size_t live_cq_row_size {8};

  struct DecodeRows
  {
    int ndecodes;                       // Number of populated decode rows in the current cycle.
    int ncand;                          // Number of QMAP candidates found in the current cycle.
    int nQDecoderDone;                  // QMAP completion state: 1 for live data, 2 for disk data.
    int nWDecoderBusy;                  // WSJT-X decoder busy flag: 1 while busy, otherwise 0.
    int nWTransmitting;                 // WSJT-X transmit period while transmitting, otherwise 0.
    int kHzRequested;                   // QMAP dial request in integer kHz; 0 means no request.
    char result[max_rows][row_size];    // Formatted QMAP decode rows.
  };

  struct LiveCqRows
  {
    char result2[max_rows][live_cq_row_size];
  };

  template<std::size_t Size>
  std::size_t text_length (char const (&row)[Size]) noexcept
  {
    std::size_t length {0};
    while (length < Size && row[length]) ++length;
    return length;
  }

  static_assert (std::is_standard_layout<DecodeRows>::value,
    "QMAP decode IPC data must have a stable layout");
  static_assert (std::is_trivially_copyable<DecodeRows>::value,
    "QMAP decode IPC data must support shared-memory copies");
  static_assert (sizeof (int) == 4,
    "QMAP decode IPC integers must match Fortran INTEGER storage");
  static_assert (sizeof (DecodeRows) == 6 * sizeof (int) + max_rows * row_size,
    "QMAP decode IPC data must not contain padding");
  static_assert (sizeof (DecodeRows) <= shared_memory_size,
    "QMAP decode IPC data must fit in shared memory");
  static_assert (sizeof (LiveCqRows) == max_rows * live_cq_row_size,
    "QMAP LiveCQ data must not contain padding");
}

#endif
