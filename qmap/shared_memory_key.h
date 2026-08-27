#ifndef QMAP_SHARED_MEMORY_KEY_H
#define QMAP_SHARED_MEMORY_KEY_H

#include <QString>

namespace qmap_decode_ipc
{
  inline QString shared_memory_key ()
  {
    auto const override_key = qEnvironmentVariable ("WSJT_QMAP_SHARED_MEMORY_KEY");
    return override_key.isEmpty () ? QStringLiteral ("mem_qmap") : override_key;
  }
}

#endif
