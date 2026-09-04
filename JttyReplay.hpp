#ifndef JTTY_REPLAY_HPP
#define JTTY_REPLAY_HPP

#include "DecDataMutex.hpp"
#include <QMutexLocker>

inline int snapshotJttyFrames (int const& liveFrames)
{
  QMutexLocker lock {&dec_data_mutex ()};
  return liveFrames;
}

// Shared traversal used by manual replay and waterfall-picked replay.
// stopAfterFrame returns true for EOM or the picked replay's stop position.
// Snapshot once: newly arriving audio belongs to later live processing.
template<typename StopAfterFrame>
void replayJttyFrames (int const& liveFrames, StopAfterFrame stopAfterFrame)
{
  int const end = snapshotJttyFrames (liveFrames);
  for (int k = 3456; k < end; k += 3456)
    {
      if (stopAfterFrame (k)) break;
    }
}

#endif
