// -*- Mode: C++ -*-
/*
 * Class to handle the formatted string as returned from the fortran decoder
 *
 * VK3ACF August 2013
 */


#ifndef DECODEDTEXT_H
#define DECODEDTEXT_H

#include <QString>
#include <QStringList>

#include "DecoderAnnotations.hpp"
#include "DecoderMessage.h"

enum class DecodedCallState
{
  Missing,
  Literal,
  ResolvedHash,
  UnresolvedHash,
  Malformed
};

struct DecodedMessageFields
{
  QString destination;
  QString sender;
  QString exchange;
  QString grid;
  bool cq {};
  QString acknowledgedRecipient;
  QStringList exchangeWords;
  QStringList payloadWords;
  DecodedCallState destinationState {DecodedCallState::Missing};
  DecodedCallState senderState {DecodedCallState::Missing};
  DecodedCallState acknowledgedRecipientState {DecodedCallState::Missing};
  bool hasMalformedCalls {false};
  bool completeTail {false};
  bool reportPresent {false};
  int report {0};
  int kind {DECODER_MESSAGE_NONE};
  int exchangeKind {DECODER_EXCHANGE_NONE};
};

DecodedMessageFields parseDecodedMessage (QString message);

/*
012345678901234567890123456789012345678901
^    ^    ^   ^    ^  ^
2343 -11  0.8 1259 #  CQ VP2X/GM4WJS GL33
2343 -11  0.8 1259 #  CQ 999 VP2V/GM4WJS
2343 -11  0.8 1259 #  YV6BFE F6GUU R-08
2343 -19  0.3  718 #  VE6WQ SQ2NIJ -14
2343  -7  0.3  815 #  KK4DSD W7VP -16
2343 -13  0.1 3627 @  CT1FBK IK5YZT R+02

0605  Tx      1259 #  CQ VK3ACF QF22
*/

class DecodedText
{
public:
  explicit DecodedText (QString const& message);

  QString string() const { return string_; };
  QString clean_string() const { return clean_string_; };
  QString message() const { return message_; };
  QString const& payload() const { return payload_; }
  QStringList const& payloadWords() const { return fields_.payloadWords; }
  DecodedMessageFields const& fields() const { return fields_; }
  bool hasValidHeader() const { return valid_header_; }
  QString const& modeMarker() const { return mode_marker_; }
  DecoderAnnotations::Metadata const& annotations() const { return annotations_; }
  QString withoutAnnotations() const;
  DecodedText withDisplayString (QString const& displayLine) const;
  QStringList messageWords () const;
  int indexOf(QString s) const { return string_.indexOf(s); };
  int indexOf(QString s, int i) const { return string_.indexOf(s,i); };
  QString mid(int f, int t) const { return string_.mid(f,t); };
  QString left(int i) const { return string_.left(i); };

  void clear() { *this = DecodedText {QString {}}; }

  QString CQersCall() const;

  bool isJT65() const;
  bool isJT9() const;
  bool isTX() const;
  bool isStandardMessage () const {return is_standard_;}
  bool isLowConfidence () const;
  int frequencyOffset() const;  // hertz offset from the tuned dial or rx frequency, aka audio frequency
  int snr() const;
  float dt() const;

  // find and extract any report. Returns true if this is a standard message
  bool report(QString const& myBaseCall, QString const& dxBaseCall, /*mod*/QString& report) const;

  // get the first message text word, usually the call
  QString call() const;

  // get the second word, most likely the de call and the third word, most likely grid
  void deCallAndGrid(/*out*/QString& call, QString& grid) const;

  unsigned timeInSeconds() const;

  // returns a string of the SNR field with a leading + or - followed by two digits
  QString report() const;

private:
  // These define the columns in the decoded text where fields are to be found.
  // We rely on these columns being the same in the fortran code (lib/decoder.f90) that formats the decoded text
  enum Columns {column_time    = 0,
      column_snr     = 5,
      column_dt      = 9,
      column_freq    = 14,
      column_mode    = 19,
      column_qsoText = 22 };

  QString string_;
  QString clean_string_;
  int padding_ {0};
  QString message_;
  QString message0_;
  bool is_standard_ {false};
  QString payload_;
  DecodedMessageFields fields_;
  DecoderAnnotations::Metadata annotations_;
  QString mode_marker_;
  int payload_offset_ {0};
  unsigned time_seconds_ {0};
  int frequency_ {0};
  int snr_ {0};
  float dt_ {0.f};
  bool valid_header_ {false};
  bool transmitted_ {false};
  QStringList message_captures_;
  QString de_call_;
  QString de_exchange_;
  void cacheLegacyCaptures ();
};

#endif // DECODEDTEXT_H
