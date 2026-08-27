#ifndef QMAP_IPC_H
#define QMAP_IPC_H

#include "decode_ipc.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

constexpr std::size_t QMapDecodeCapacity = qmap_decode_ipc::max_rows;
constexpr std::size_t QMapDecodeRowSize = qmap_decode_ipc::row_size;

static_assert (QMapDecodeRowSize == 80,
               "QMAP IPC must use the fixed 80-byte decoder row");

enum class QMapClickAction : std::int32_t
{
  None = 0,
  Disarm = 1,
  Select = 2,
  SelectAndEnableTx = 3,
};

using QMapDecodeBlock = qmap_decode_ipc::DecodeRows;

struct QMapClickMailbox
{
  char selectedDecode[QMapDecodeRowSize];
  QMapClickAction action;
};

struct QMapSharedMemory
{
  QMapDecodeBlock decodes;
  QMapClickMailbox click;
};

static_assert (sizeof (std::int32_t) == 4, "QMAP IPC requires 32-bit integers");
static_assert (std::is_standard_layout<QMapClickMailbox>::value,
               "QMAP click mailbox must have a stable field layout");
static_assert (std::is_trivially_copyable<QMapClickMailbox>::value,
               "QMAP click mailbox must support byte-for-byte copying");
static_assert (offsetof (QMapClickMailbox, action) == QMapDecodeRowSize,
               "QMAP click request offset changed");
static_assert (sizeof (QMapClickMailbox) == QMapDecodeRowSize + sizeof (std::int32_t),
               "QMAP click mailbox size changed");
static_assert (std::is_standard_layout<QMapSharedMemory>::value,
               "QMAP shared memory must have a stable field layout");
static_assert (std::is_trivially_copyable<QMapSharedMemory>::value,
               "QMAP shared memory must support byte-for-byte copying");
static_assert (offsetof (QMapSharedMemory, decodes) == 0,
               "QMAP decode block must start the shared segment");
static_assert (offsetof (QMapSharedMemory, click) == sizeof (QMapDecodeBlock),
               "QMAP click mailbox offset changed");
static_assert (sizeof (QMapSharedMemory) == sizeof (QMapDecodeBlock) + sizeof (QMapClickMailbox),
               "QMAP IPC wire layout changed");

constexpr std::size_t QMapSharedMemorySize {
  ((sizeof (QMapSharedMemory) + 15) / 16) * 16
};

static_assert (QMapSharedMemorySize >= qmap_decode_ipc::shared_memory_size,
               "QMAP shared memory must preserve the decoder segment size");
static_assert (sizeof (QMapSharedMemory) <= QMapSharedMemorySize,
               "QMAP IPC exceeds the shared-memory segment");

constexpr bool qmapDecoderRegionAvailable (std::size_t mappedSize) noexcept
{
  return mappedSize >= qmap_decode_ipc::shared_memory_size;
}

constexpr bool qmapClickMailboxAvailable (std::size_t mappedSize) noexcept
{
  return mappedSize >= sizeof (QMapSharedMemory);
}

constexpr std::size_t qmapClearLength (std::size_t mappedSize) noexcept
{
  return std::min (mappedSize, QMapSharedMemorySize);
}

inline void clearQMapSharedMemory (void * mappedData,
                                   std::size_t mappedSize) noexcept
{
  if (mappedData) std::memset (mappedData, 0, qmapClearLength (mappedSize));
}

inline void publishQMapDecodeBlock (QMapSharedMemory& shared,
                                    QMapDecodeBlock& publication) noexcept
{
  publication.nWDecoderBusy = shared.decodes.nWDecoderBusy;
  publication.nWTransmitting = shared.decodes.nWTransmitting;
  publication.kHzRequested = shared.decodes.kHzRequested;
  shared.decodes = publication;
}

inline bool acknowledgeQMapDecodeBatch (QMapDecodeBlock& shared) noexcept
{
  if (shared.nQDecoderDone == 0) return false;
  shared.ndecodes = 0;
  shared.nQDecoderDone = 0;
  return true;
}

#endif // QMAP_IPC_H
