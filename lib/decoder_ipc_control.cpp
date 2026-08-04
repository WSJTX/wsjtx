#include "decoder_ipc_control.h"

#include <cstdint>

#if defined (_MSC_VER)
#include <intrin.h>
#endif

static_assert (sizeof (int) == sizeof (std::int32_t),
               "decoder IPC requires 32-bit control words");

namespace
{
#if defined (_MSC_VER)
  static_assert (sizeof (long) == sizeof (int),
                 "decoder IPC requires 32-bit MSVC interlocked words");

  int atomicLoad (int const * value)
  {
    return static_cast<int> (_InterlockedCompareExchange (
        reinterpret_cast<volatile long *> (const_cast<int *> (value)), 0, 0));
  }

  void atomicStore (int * value, int desired)
  {
    _InterlockedExchange (reinterpret_cast<volatile long *> (value), desired);
  }

  bool compareExchange (int * value, int expected, int desired)
  {
    return expected == _InterlockedCompareExchange (
        reinterpret_cast<volatile long *> (value), desired, expected);
  }
#else
  static_assert (__atomic_always_lock_free (sizeof (int), nullptr),
                 "decoder IPC control words must be lock-free");

  int atomicLoad (int const * value)
  {
    return __atomic_load_n (value, __ATOMIC_ACQUIRE);
  }

  void atomicStore (int * value, int desired)
  {
    __atomic_store_n (value, desired, __ATOMIC_RELEASE);
  }

  bool compareExchange (int * value, int expected, int desired)
  {
    return __atomic_compare_exchange_n (value, &expected, desired, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
  }
#endif

  bool hasCurrentVersion (int const * version)
  {
    return DECODER_IPC_VERSION == atomicLoad (version);
  }
}

extern "C" int decoder_ipc_atomic_load (int const * value)
{
  return atomicLoad (value);
}

extern "C" void decoder_ipc_control_initialize (int * generation, int * state,
                                                   int * version)
{
  atomicStore (generation, 0);
  atomicStore (version, DECODER_IPC_VERSION);
  atomicStore (state, DECODER_IPC_IDLE);
}

extern "C" void decoder_ipc_control_shutdown (int * state,
                                                int * legacy_acknowledgment)
{
  atomicStore (state, DECODER_IPC_SHUTDOWN);
  atomicStore (legacy_acknowledgment, 1);
}

extern "C" int decoder_ipc_control_publish (int * generation, int * state,
                                             int const * version,
                                             int request_generation)
{
  if (request_generation <= 0 || !hasCurrentVersion (version)
      || DECODER_IPC_IDLE != atomicLoad (state))
    {
      return 0;
    }

  atomicStore (generation, request_generation);
  return compareExchange (state, DECODER_IPC_IDLE, DECODER_IPC_READY);
}

extern "C" int decoder_ipc_control_try_claim (int * generation, int * state,
                                               int const * version,
                                               int * claimed_generation)
{
  if (!hasCurrentVersion (version)) return DECODER_IPC_CLAIM_INCOMPATIBLE;

  auto const currentState = atomicLoad (state);
  if (DECODER_IPC_SHUTDOWN == currentState) return DECODER_IPC_CLAIM_SHUTDOWN;
  if (DECODER_IPC_READY != currentState) return DECODER_IPC_CLAIM_NONE;

  auto const currentGeneration = atomicLoad (generation);
  if (currentGeneration <= 0) return DECODER_IPC_CLAIM_INVALID;

  if (!compareExchange (state, DECODER_IPC_READY, DECODER_IPC_DECODING))
    {
      return DECODER_IPC_CLAIM_NONE;
    }

  *claimed_generation = currentGeneration;
  return DECODER_IPC_CLAIMED;
}

extern "C" int decoder_ipc_control_finish (int const * generation, int * state,
                                            int const * version,
                                            int request_generation)
{
  return request_generation > 0
    && hasCurrentVersion (version)
    && request_generation == atomicLoad (generation)
    && compareExchange (state, DECODER_IPC_DECODING, DECODER_IPC_COMPLETE);
}

extern "C" int decoder_ipc_control_consume (int const * generation, int * state,
                                             int const * version,
                                             int request_generation)
{
  if (request_generation <= 0 || !hasCurrentVersion (version)
      || request_generation != atomicLoad (generation))
    {
      return 0;
    }

  return compareExchange (state, DECODER_IPC_COMPLETE, DECODER_IPC_IDLE);
}
