#include "decodedtext.h"
#include "DecoderAnnotations.hpp"
#include "DecoderMessage.h"

#include <QStringList>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>
#include "qt_helpers.hpp"

extern "C" {
  bool stdmsg72_(char const * msg, bool * is_jt65, fortran_charlen_t);
}

namespace
{
  QRegularExpression tokens_re {R"(
^
  (?:(?<dual>[A-Z0-9/]+|\.{3})\sRR73;\s)? # dual reply DXpedition message
  (?:
    (?<word1>
      (?:CQ|DE|QRZ)
      (?:\s?DX|\s
        (?:[A-Z]{1,4}|\d{3})  # directional CQ
      )
      | [A-Z0-9/]+            # DX call
      |\.{3}                  # unknown hash code
    )\s
  )
  (?:
    (?<word2>[A-Z0-9/]+|\.{3}) # DE call or unknown hash code
    (?:\s
      (?<word3>[-+A-Z0-9]+)   # report
      (?:\s
        (?<word4>
          (?:
            OOO               # EME
            | (?!RR73)[A-R]{2}[0-9]{2} # grid square (not RR73)
            | 5[0-9]{5}       # EU VHF Contest report & serial
          )
        )
        (?:\s
          (?<word5>[A-R]{2}[0-9]{2}[A-X]{2}) # EU VHF Contest grid locator
        )?
      )?
    )?
  )?
)"
                               , QRegularExpression::ExtendedPatternSyntaxOption};
  QRegularExpression const angle_bracket_re {"[<>]"};
  QRegularExpression const cq_qrz_re {"^(CQ|QRZ)\\s"};
  DecodedMessageFields qt_fields (decoder_message_fields const& parsed)
  {
    DecodedMessageFields fields;
    fields.destination = QString::fromLatin1 (parsed.destination);
    fields.sender = QString::fromLatin1 (parsed.sender);
    fields.exchange = QString::fromLatin1 (parsed.exchange);
    fields.grid = QString::fromLatin1 (parsed.grid);
    fields.acknowledgedRecipient = QString::fromLatin1 (parsed.acknowledged_recipient);
    fields.cq = parsed.cq != 0;
    fields.hasMalformedCalls = parsed.malformed != 0;
    fields.completeTail = parsed.complete_tail != 0;
    fields.reportPresent = parsed.report_present != 0;
    fields.report = parsed.report;
    fields.kind = parsed.kind;
    fields.exchangeKind = parsed.exchange_kind;
    fields.destinationState = static_cast<DecodedCallState> (parsed.destination_state);
    fields.senderState = static_cast<DecodedCallState> (parsed.sender_state);
    fields.acknowledgedRecipientState = static_cast<DecodedCallState> (parsed.acknowledged_recipient_state);
    for (int word = 0; word < parsed.word_count; ++word) {
      fields.payloadWords.append (QString::fromLatin1 (parsed.words[word]));
    }
    fields.exchangeWords = fields.payloadWords.mid (parsed.exchange_first_word);
    return fields;
  }

  decoder_message_fields parsed_payload (QString const& payload, int mode = 0)
  {
    decoder_message_fields parsed {};
    auto const text = payload.toLatin1 ();
    decoder_message_parse (mode, text.constData (), text.size (), &parsed);
    return parsed;
  }

  bool standard_payload (QString const& payload, QString const& marker)
  {
    if (payload.isEmpty () || payload.size () > 37) return false;
    auto text = payload.toLocal8Bit ();
    if (text.size () > 37) return false;
    text += QByteArray {37 - text.size (), ' '};
    if (marker == "$" || marker == "@" || marker == "#") {
      bool is_jt65 = marker == "#";
      return stdmsg72_ (text.constData (), &is_jt65, 37);
    }
    return parsed_payload (payload).structured != 0;
  }

  bool annotation_payload (QString const& payload, QString const& marker)
  {
    if (standard_payload (payload, marker)
        || ((marker == "$" || marker == "#")
            && (payload == "RO" || payload == "RRR" || payload == "73"))) return true;
    auto const fields = parseDecodedMessage (payload);
    static QRegularExpression const call_letter_digit {"[A-Z][0-9]|[0-9][A-Z]"};
    auto const resolved_call = [&] (QString const& call, DecodedCallState state) {
      return (state == DecodedCallState::Literal || state == DecodedCallState::ResolvedHash)
        && call.size () >= 3 && call.size () <= 11 && call.contains (call_letter_digit);
    };
    if (fields.hasMalformedCalls || fields.payloadWords.size () != 3
        || fields.exchangeWords.size () != 1
        || !resolved_call (fields.destination, fields.destinationState)
        || !resolved_call (fields.sender, fields.senderState)) return false;
    if (fields.exchange == "73" || fields.exchange == "RR73") return true;
    static QRegularExpression const report {"\\AR?[-+][0-9]{2}\\z"};
    auto value = fields.exchange;
    if (value.startsWith ('R')) value.remove (0, 1);
    return report.match (fields.exchange).hasMatch () && value.toInt () >= -50 && value.toInt () <= 49;
  }
}

DecodedMessageFields parseDecodedMessage (QString message)
{
  return qt_fields (parsed_payload (message.trimmed ()));
}

DecodedText::DecodedText (QString const& the_string)
  : string_ {the_string.left (the_string.indexOf (QChar::Nbsp))} // discard appended info
  , clean_string_ {string_}
  , padding_ {string_.indexOf (" ") > 4 ? 2 : 0} // allow for
                                                    // seconds
  , message_ {string_.mid (column_qsoText + padding_).trimmed ()}
  , is_standard_ {false}
{
  auto const time_width = padding_ ? 6 : 4;
  mode_marker_ = string_.mid (column_mode + padding_, 1);
  payload_offset_ = column_mode + padding_ + 1;
  if (payload_offset_ < string_.size () && (mode_marker_ == "#" || mode_marker_ == "$")
      && (string_.at (payload_offset_) == '*' || string_.at (payload_offset_) == '#')) ++payload_offset_;
  bool const marker_separator = payload_offset_ < string_.size () && string_.at (payload_offset_).isSpace ();
  while (payload_offset_ < string_.size () && string_.at (payload_offset_).isSpace ()) ++payload_offset_;
  auto const utc = string_.left (time_width);
  static QRegularExpression const utc_pattern {R"(\A[0-9]{4}(?:[0-9]{2})?\z)"};
  bool snr_ok = false;
  bool dt_ok = false;
  bool frequency_ok = false;
  snr_ = string_.mid (time_width, 4).toInt (&snr_ok);
  dt_ = string_.mid (time_width + 4, 5).toFloat (&dt_ok);
  frequency_ = string_.mid (time_width + 9, 5).toInt (&frequency_ok);
  transmitted_ = string_.mid (time_width, 4).trimmed () == "Tx";
  unsigned const hour = utc.left (2).toUInt ();
  unsigned const minute = utc.mid (2, 2).toUInt ();
  unsigned const second = padding_ ? utc.mid (4, 2).toUInt () : 0;
  time_seconds_ = 3600 * hour + 60 * minute + second;
  valid_header_ = utc_pattern.match (utc).hasMatch () && hour < 24 && minute < 60 && second < 60
    && string_.size () >= payload_offset_ && QString {"~+`:#@$&^"}.contains (mode_marker_)
    && !mode_marker_.isEmpty () && marker_separator && frequency_ok && frequency_ >= 0
    && (transmitted_ || (snr_ok && dt_ok && std::isfinite (dt_)));

  QString region = string_.mid (payload_offset_);
  auto const newline = region.indexOf (QRegularExpression {"[\\r\\n]"});
  if (newline >= 0) region.truncate (newline);
  int const source_width = mode_marker_ == "#" || mode_marker_ == "$" || mode_marker_ == "@" ? 22 : 37;
  static QRegularExpression const suffix_pattern {
    "(?:^|\\s)(?<suffix>" + DecoderAnnotations::suffix_pattern () + ")\\s*$"};
  auto suffix = suffix_pattern.match (region, source_width);
  if (!suffix.hasMatch ()) suffix = suffix_pattern.match (region);
  if (suffix.hasMatch ()) {
    auto const start = suffix.capturedStart ("suffix");
    auto const candidate = region.left (suffix.capturedStart ()).trimmed ();
    if (start >= source_width || annotation_payload (candidate, mode_marker_)) {
      annotations_ = DecoderAnnotations::parse (suffix.captured ("suffix"));
      auto qualifier = annotations_.suffix;
      if (qualifier.startsWith ('?')) qualifier = qualifier.mid (1).trimmed ();
      if (qualifier.startsWith ('a') || qualifier.startsWith ('q')) {
        clean_string_.truncate (payload_offset_ + suffix.capturedStart ());
      }
      region.truncate (suffix.capturedStart ());
    }
  }
  if (region.size () >= source_width && region.at (source_width - 1) == '?') {
    annotations_.lowConfidence = true;
    region[source_width - 1] = ' ';
  }
  auto trimmed = region.trimmed ();
  if (trimmed.endsWith ('?') && annotation_payload (trimmed.left (trimmed.size () - 1).trimmed (), mode_marker_)) {
    annotations_.lowConfidence = true;
    trimmed.chop (1);
  }
  payload_ = trimmed.trimmed ();
  auto const parsed = parsed_payload (payload_);
  fields_ = qt_fields (parsed);
  message_ = clean_string_.mid (payload_offset_).trimmed ();

//  qDebug () << "DecodedText: the_string:" << the_string << "Nbsp pos:" << the_string.indexOf (QChar::Nbsp);
  if (message_.length() >= 1)
    {
// remove appended confidence (?) and ap designators before truncating the message
       message0_ = message_.left(37);
       message_ = (payload_.size () > 37 ? payload_ : message_.left (37)).remove (angle_bracket_re);
      int i1 = message_.indexOf ('\r');
      if (i1 > 0)
        {
          message_ = message_.left (i1 - 1);
        }
      if (message_.contains (cq_qrz_re))
        {
          // TODO this magic position 16 is guaranteed to be after the
          // last space in a decoded CQ or QRZ message but before any
          // appended DXCC entity name or worked before information
          auto eom_pos = message_.indexOf (' ', 16);
          // we always want at least the characters to position 16
          if (eom_pos < 16) eom_pos = message_.size () - 1;
          // remove DXCC entity and worked B4 status. TODO need a better way to do this
          message_ = message_.left (eom_pos + 1);
        }
      bool const legacy = mode_marker_ == "$" || mode_marker_ == "@" || mode_marker_ == "#";
      is_standard_ = valid_header_ && payload_.size () <= 37 && !fields_.hasMalformedCalls
        && (legacy ? standard_payload (message0_, mode_marker_) : parsed.structured != 0);
    }
  cacheLegacyCaptures ();
};

void DecodedText::cacheLegacyCaptures ()
{
  auto const capture_message = fields_.payloadWords.join (' ');
  message_captures_ = tokens_re.match (capture_message).capturedTexts ();
  auto exchange_captures = message_captures_;
  auto const separator = capture_message.indexOf ("; ");
  if (separator >= 0 && !capture_message.contains (" RR73; ")) {
    exchange_captures = tokens_re.match (capture_message.mid (separator + 2)).capturedTexts ();
  }
  de_call_ = exchange_captures.value (3);
  de_exchange_ = exchange_captures.value (4);
  if (de_exchange_ == "R") de_exchange_ = exchange_captures.value (5);
}

QStringList DecodedText::messageWords () const
{
  if(is_standard_) {
    return message_captures_;
  }
  // simple word split for free text messages
  auto words = message_.split (' ', SkipEmptyParts);
  // add whole message and two empty strings as item 0 & 1 to mimic RE
  // capture list
  words.prepend (QString {});
  words.prepend (message_);
  return words;
}

QString DecodedText::CQersCall() const
{
  auto const callType = fields_.destination.section (' ', 0, 0);
  return callType == "CQ" || callType == "CQDX" || callType == "DE" || callType == "QRZ"
    || fields_.kind == DECODER_MESSAGE_BEACON
    ? fields_.sender : QString {};
}


bool DecodedText::isJT65() const
{
    return mode_marker_ == "#";
}

bool DecodedText::isJT9() const
{
    return mode_marker_ == "@";
}

bool DecodedText::isTX() const
{
    return transmitted_;
}

bool DecodedText::isLowConfidence () const
{
  return annotations_.lowConfidence;
}

int DecodedText::frequencyOffset() const
{
    return frequency_;
}

int DecodedText::snr() const
{
  return snr_;
}

float DecodedText::dt() const
{
  return dt_;
}

/*
2343 -11  0.8 1259 # YV6BFE F6GUU R-08
2343 -19  0.3  718 # VE6WQ SQ2NIJ -14
2343  -7  0.3  815 # KK4DSD W7VP -16
2343 -13  0.1 3627 @ CT1FBK IK5YZT R+02

0605  Tx      1259 # CQ VK3ACF QF22
*/

// find and extract any report. Returns true if this is a standard message
bool DecodedText::report(QString const& myBaseCall, QString const& dxBaseCall, /*mod*/QString& report) const
{
  if (message_.isEmpty ()) return false;
  auto const matches = [] (QString const& call, QString const& base) {
    return call == base || call.endsWith ('/' + base) || call.startsWith (base + '/');
  };
  bool const addressed = matches (fields_.destination, myBaseCall)
    || (!dxBaseCall.isEmpty () && matches (fields_.sender, dxBaseCall));
  if (is_standard_ && addressed && !fields_.exchangeWords.isEmpty ()) {
    auto text = fields_.exchangeWords.front ();
    if (text.startsWith ('R')) text.remove (0, 1);
    bool ok = false;
    auto const value = text.toInt (&ok);
    if (ok && value >= -50 && value < 50) report = text;
  }
  return is_standard_;
}

// get the first text word, usually the call
QString DecodedText::call() const
{
  return fields_.destination;
}

// get the second word, most likely the de call and the third word, most likely grid
void DecodedText::deCallAndGrid(/*out*/QString& call, QString& grid) const
{
  call = de_call_;
  grid = de_exchange_;
}

unsigned DecodedText::timeInSeconds() const
{
  return time_seconds_;
}

QString DecodedText::withoutAnnotations() const
{
  return string_.left (payload_offset_) + payload_;
}

DecodedText DecodedText::withDisplayString (QString const& displayLine) const
{
  auto result = *this;
  result.string_ = displayLine;
  result.clean_string_ = displayLine;
  return result;
}

QString DecodedText::report() const // returns a string of the SNR field with a leading + or - followed by two digits
{
    int sr = snr();
    if (sr<-50)
        sr = -50;
    else
        if (sr > 49)
            sr = 49;

    QString rpt;
    rpt = rpt.asprintf("%d",abs(sr));
    if (sr > 9)
        rpt = "+" + rpt;
    else
        if (sr >= 0)
            rpt = "+0" + rpt;
        else
            if (sr >= -9)
                rpt = "-0" + rpt;
            else
                rpt = "-" + rpt;
    return rpt;
}
