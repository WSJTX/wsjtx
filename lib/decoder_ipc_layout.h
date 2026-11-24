#ifndef DECODER_IPC_LAYOUT_H
#define DECODER_IPC_LAYOUT_H

#include <stddef.h>

#define DECODER_IPC_LAYOUT_MAGIC 0x57534a54
#define DECODER_IPC_ENDIAN_MARKER 0x01020304
#define DECODER_IPC_CAP_GENERATIONS 1
#define DECODER_IPC_CAP_PROGRESS 2
#define DECODER_IPC_CAP_HANDSHAKE 4
#define DECODER_IPC_CAPABILITIES 7
#define DECODER_IPC_LAYOUT_FIELDS 92

typedef struct decoder_ipc_field_layout {
  int offset;
  int bytes;
} decoder_ipc_field_layout_t;

typedef struct decoder_ipc_layout {
  int magic;
  int protocol_version;
  int header_bytes;
  int payload_bytes;
  int logical_bytes;
  int payload_offset;
  int capabilities;
  int int_bytes;
  int short_bytes;
  int float_bytes;
  int bool_bytes;
  int char_bytes;
  int endian_marker;
  int field_count;
  decoder_ipc_field_layout_t fields[DECODER_IPC_LAYOUT_FIELDS];
} decoder_ipc_layout_t;

enum decoder_ipc_layout_status {
  DECODER_IPC_LAYOUT_OK = 0,
  DECODER_IPC_LAYOUT_NULL = 1,
  DECODER_IPC_LAYOUT_TRUNCATED = 2,
  DECODER_IPC_LAYOUT_ALIGNMENT = 3,
  DECODER_IPC_LAYOUT_VERSION = 4,
  DECODER_IPC_LAYOUT_HEADER_SIZE = 5,
  DECODER_IPC_LAYOUT_PAYLOAD_SIZE = 6,
  DECODER_IPC_LAYOUT_MISMATCH = 7,
  DECODER_IPC_LAYOUT_CAPABILITIES = 8,
  DECODER_IPC_LAYOUT_STATE = 9,
  DECODER_IPC_LAYOUT_LOGICAL_SIZE = 10,
  DECODER_IPC_LAYOUT_ATTACH = 11
};

#ifdef __cplusplus
extern "C" {
#endif

void decoder_ipc_expected_layout (decoder_ipc_layout_t * layout);
int decoder_ipc_validate_layout (void const * storage, size_t available_bytes,
                                decoder_ipc_layout_t const * peer_layout);
char const * decoder_ipc_layout_status_name (int status);
void decoder_ipc_report_layout_error (int status, void const * storage,
                                     size_t available_bytes);
void decoder_ipc_fortran_layout (decoder_ipc_layout_t * layout);
int decoder_ipc_fortran_validate (void const * storage, size_t available_bytes);

#ifdef __cplusplus
}
#endif

#endif
