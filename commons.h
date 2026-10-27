#ifndef COMMONS_H
#define COMMONS_H

#define NSMAX 6827
#define NTMAX 30*60
#define RX_SAMPLE_RATE 12000

#include "DecoderIpcProtocol.h"
#include "lib/decoder_ipc_layout.h"

#ifdef __cplusplus
#include <cstddef>
#else
#include <stdbool.h>
#endif

#include "DecoderParams.h"

typedef struct dec_data {
  float ss[184*NSMAX];
  float savg[NSMAX];
  float sred[5760];
  short int d2[NTMAX*RX_SAMPLE_RATE];
  decoder_params_t params;
} dec_data_t;

typedef struct shared_dec_data {
  decoder_ipc_control_t control;
  decoder_ipc_layout_t layout;
  dec_data_t payload;
  decoder_input_metadata_t metadata;
} shared_dec_data_t;

#ifdef __cplusplus
static_assert (sizeof (int) == 4, "decoder IPC requires 32-bit C integers");
static_assert (sizeof (decoder_ipc_control_t) == 16,
               "decoder IPC control header must remain 16 bytes");
static_assert (offsetof (decoder_ipc_control_t, state) == 4,
               "decoder IPC state must remain at the legacy shutdown offset");
static_assert (offsetof (shared_dec_data_t, layout) == sizeof (decoder_ipc_control_t),
               "decoder layout descriptor must immediately follow the control header");
static_assert (offsetof (shared_dec_data_t, payload) ==
               sizeof (decoder_ipc_control_t) + sizeof (decoder_ipc_layout_t),
               "decoder payload must immediately follow the layout descriptor");
static_assert (sizeof (decoder_input_metadata_t) == 24,
               "decoder input metadata layout must remain stable");
static_assert (offsetof (shared_dec_data_t, metadata) >=
               offsetof (shared_dec_data_t, payload) + sizeof (dec_data_t),
               "decoder input metadata must follow the payload");
static_assert (sizeof (shared_dec_data_t) ==
               offsetof (shared_dec_data_t, metadata) + sizeof (decoder_input_metadata_t),
               "decoder shared-memory layout must not contain trailing padding");
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern struct {
  float syellow[NSMAX];
  float ref[3457];
  float filter[3457];
} spectra_;

extern struct {
  int   nclearave;
  int   nsum;
  float blue[4096];
  float red[4096];
} echocom_;

extern struct {
  float wave[(160+2)*134400*4]; /* (nsym+2)*nsps scaled up to 48kHz */
  int   nslots;
  int   nfreq;
  int   i3bit[5];
  char  cmsg[5][40];
  char  mycall[12];
  char  textMsg[26];
  bool  bMoreCQs;
  bool  bSendMsg;
} foxcom_;

#ifdef __cplusplus
}
#endif

#endif // COMMONS_H
