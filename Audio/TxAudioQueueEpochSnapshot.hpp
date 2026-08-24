#ifndef TX_AUDIO_QUEUE_EPOCH_SNAPSHOT_HPP_
#define TX_AUDIO_QUEUE_EPOCH_SNAPSHOT_HPP_

#include "TxAudioQueue.hpp"

namespace TxAudioQueueDetail
{
  inline bool tryMakeEpochSnapshot (quint64 sequence_before,
                                    qint64 epoch,
                                    quint64 sequence_after,
                                    TxAudioQueueEpoch& result) noexcept
  {
    if ((sequence_before & 1) || sequence_before != sequence_after)
      {
        return false;
      }

    result = TxAudioQueueEpoch {epoch};
    return true;
  }
}

#endif
