#ifndef DECODER_IPC_CONTROL_H
#define DECODER_IPC_CONTROL_H

#include "../DecoderIpcProtocol.h"

enum decoder_ipc_claim_result
{
  DECODER_IPC_CLAIM_INVALID = -2,
  DECODER_IPC_CLAIM_INCOMPATIBLE = -1,
  DECODER_IPC_CLAIM_NONE = 0,
  DECODER_IPC_CLAIMED = 1,
  DECODER_IPC_CLAIM_SHUTDOWN = 2
};

#ifdef __cplusplus
extern "C" {
#endif

int decoder_ipc_atomic_load (int const * value);
void decoder_ipc_control_initialize (int * generation, int * state, int * version,
                                     int * progress);
void decoder_ipc_control_shutdown (int * state, int * legacy_acknowledgment);
int decoder_ipc_control_publish (int * generation, int * state,
                                 int const * version, int * progress,
                                 int request_generation);
int decoder_ipc_control_try_claim (int * generation, int * state, int const * version,
                                   int * claimed_generation);
int decoder_ipc_control_finish (int const * generation, int * state,
                                int const * version, int request_generation);
int decoder_ipc_control_consume (int const * generation, int * state,
                                 int const * version, int request_generation);
void decoder_ipc_progress_bind (int const * generation, int const * state,
                                int const * version, int * progress);
void decoder_ipc_progress_unbind (void);
void decoder_ipc_progress_report (int request_generation);
#ifdef __cplusplus
}
#endif

#endif
