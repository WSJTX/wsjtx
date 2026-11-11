#ifndef WSJTX_DECODER_MESSAGE_H
#define WSJTX_DECODER_MESSAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  DECODER_CALL_MISSING = 0, DECODER_CALL_LITERAL = 1,
  DECODER_CALL_RESOLVED_HASH = 2, DECODER_CALL_UNRESOLVED_HASH = 3,
  DECODER_CALL_MALFORMED = 4
};
enum {
  DECODER_MESSAGE_NONE = 0, DECODER_MESSAGE_TEXT = 1,
  DECODER_MESSAGE_CQ = 2, DECODER_MESSAGE_DIRECTED = 3,
  DECODER_MESSAGE_DUAL = 4, DECODER_MESSAGE_CONTEST = 5,
  DECODER_MESSAGE_BEACON = 6
};
enum {
  DECODER_EXCHANGE_NONE = 0, DECODER_EXCHANGE_OTHER = 1,
  DECODER_EXCHANGE_GRID = 2, DECODER_EXCHANGE_REPORT = 3,
  DECODER_EXCHANGE_RREPORT = 4, DECODER_EXCHANGE_RO = 5,
  DECODER_EXCHANGE_RRR = 6, DECODER_EXCHANGE_RR73 = 7,
  DECODER_EXCHANGE_SIGNOFF = 8, DECODER_EXCHANGE_CONTEST = 9,
  DECODER_EXCHANGE_POWER = 10, DECODER_EXCHANGE_OOO = 11
};
enum { DECODER_MESSAGE_MAX_WORDS = 19, DECODER_MESSAGE_MAX_LENGTH = 37 };

/* Strings are owned and null-terminated. Words omit TU; and call brackets.
   exchange_first_word is zero-based, or word_count when there is no tail.
   Lexical fields remain available when structured is zero. complete_tail
   means the entire exchange has a recognized receive shape. */
typedef struct {
  int32_t kind, exchange_kind;
  int32_t cq, malformed, structured, complete_tail;
  int32_t report_present, report;
  int32_t destination_state, sender_state, acknowledged_recipient_state;
  int32_t word_count, exchange_first_word;
  char destination[38], sender[38], exchange[38], acknowledged_recipient[38];
  char grid[7];
  char words[DECODER_MESSAGE_MAX_WORDS][38];
} decoder_message_fields;

/* Parse an undecorated protocol payload. Return 0 on success, 1 for invalid
   arguments or length outside 0..37. Invalid input resets the result.
   Mode uses DecoderEngine.h mode values; zero selects modern semantics. */
int32_t decoder_message_parse(int32_t mode, const char *payload, int32_t length,
                             decoder_message_fields *result);

#ifdef __cplusplus
}
#endif
#endif
