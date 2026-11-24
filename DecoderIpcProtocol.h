#ifndef DECODER_IPC_PROTOCOL_H
#define DECODER_IPC_PROTOCOL_H

#include <stdint.h>

#define DECODER_IPC_VERSION 5

enum decoder_ipc_state
{
  DECODER_IPC_IDLE = 0,
  DECODER_IPC_READY = 1,
  DECODER_IPC_DECODING = 2,
  DECODER_IPC_COMPLETE = 3,
  DECODER_IPC_SHUTDOWN = 999
};

typedef struct decoder_ipc_control {
  int generation;
  int state;
  int version;
  int progress;
} decoder_ipc_control_t;

typedef struct decoder_input_metadata {
  int64_t input_id;
  int64_t analysis_id;
  int32_t attempt_no;
  int32_t valid_samples;
} decoder_input_metadata_t;

#endif
