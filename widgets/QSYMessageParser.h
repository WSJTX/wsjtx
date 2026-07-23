// -*- Mode: C++ -*-
#ifndef QSYMESSAGEPARSER_H
#define QSYMESSAGEPARSER_H

#include <QString>
// Table-driven codec for the WSJT-X QSY Message wire language. decode() is the
// single source of truth for whether a payload is valid.
//
// Payload grammar:
//   payload   = freq | general | reply-display
//   freq      = band mode-or-fmdigit kHz3           e.g. "AL125", "B5550"
//   general   = "ZA" NNN   (NNN in 001..014)        e.g. "ZA007"
//   reply     = "$ " CALL " " ("OKQSY"|"NOQSY")     synthesized locally on RX
namespace QSYMessageParser
{
  enum class Type { Invalid, Reply, General, Frequency };

  struct Message
  {
    Type type;

    // Reply
    QString call;            // station that replied
    QString response;        // "OK" | "NO"

    // General
    int code;                // 1..14
    QString text;            // canned-message text

    // Retain wire components so aliases such as J and X still round-trip.
    QChar band;              // e.g. 'A'
    QChar mode_char;         // mode letter, or FM sub-band digit
    int kHz;                 // 0..999
    QString frequency_mhz;   // e.g. "50.125"
    QString mode;            // e.g. "FT8" | "FM"

    Message () : type (Type::Invalid), code (0), kHz (0) {}
    bool isValid () const { return type != Type::Invalid; }
    explicit operator bool () const { return isValid (); }
  };

  Message decode (QString const& payload, int region);

  // Empty QString means invalid input. encode(Message) expects a Message
  // returned by decode(), or an equivalently valid table-backed Message.
  QString encode (Message const& message);
  QString encodeFrequency (QChar band, QChar mode, int kHz, int region);
  QString encodeGeneral (int code);
  QString encodeReplyWire (bool ok);

  // Cheap, high-recall pre-filter for the RX hot path. NOT authoritative: every
  // real QSY line passes, but some non-QSY lines may pass too. Use only as a
  // fast early-exit; decode() / decodeLine() is the source of truth.
  bool mightContainMessage (QString const& line);

  // Locate a "CALL.PAYLOAD" token in a decoded line and decode the payload.
  // Returns the first token whose payload decodes; message.type==Invalid if none.
  struct LineResult { QString call; QString payload; Message message; };
  LineResult decodeLine (QString const& line, int region);
}

#endif
