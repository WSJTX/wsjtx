// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyTransmitHost.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "JttyTransmit.hpp"

extern "C" {
  // lib/streaming_jtty_width.f90
  void jtty_message_width_c (int * columns, int * separator, char const frames[], int nframes,
                             int after_pause, int * valid);
}

namespace
{
  using namespace Jtty::Encoder;
  using Jtty::NativeAtomDescriptor;
  using Jtty::NativeExchangeProfile;
  using Jtty::NativeMacroStatus;

  // A receiver keeps the first 80 columns of a message and drops the rest
  // (lib/streaming_jtty_width.f90). A request that leaves its message open
  // leaves at least openReserve of them, the width plan's reserve: the next
  // request's first frame takes at most nineteen, five for a pause of one or
  // two frame periods (in place of the column a structured frame owes) and
  // fourteen for the widest frame.
  constexpr int receiverColumns = 80;
  constexpr int openReserve = 21;

  // The receiver's message in progress, as a request's "open" carries it.
  struct OpenMessage
  {
    int columns {0};
    bool separator {false};

    bool fresh () const {return columns == 0 && !separator;}
  };

  struct Segment
  {
    EncodedTransmitSegment encoded;
    std::u16string text;        // the characters it carries, trimmed
    bool substituted {false};
  };

  struct Transmission
  {
    TransmitStatus status {TransmitStatus::Empty};
    bool substituted {false};
    std::vector<Segment> segments;
    std::string key;
    std::u16string detail;
    OpenMessage open;           // the receiver's message after it
  };

  // open after frames reach the receiver, the first after a pause of one or
  // two frame periods when afterPause; false when it would drop one.
  bool widthAfter (OpenMessage& open, std::vector<std::string> const& frames, bool afterPause)
  {
    std::string bits;
    for (auto const& frame : frames) {
      if (frame.size () != static_cast<std::size_t> (Jtty::transmitFrameBits)) return false;
      bits += frame;
    }
    int columns = open.columns;
    int separator = open.separator ? 1 : 0;
    int valid = 0;
    jtty_message_width_c (&columns, &separator, bits.data (), static_cast<int> (frames.size ()),
                          afterPause ? 1 : 0, &valid);
    if (!valid) return false;
    open.columns = columns;
    open.separator = separator != 0;
    return true;
  }

  // text, at most maxTransmitLength prepared characters, encoded as
  // encodeTransmitText encodes a segment; false when it does not encode.
  // runsOn is whether its last frame is five characters of text with no
  // blank after them, so that the next frame's text follows on with no space
  // (the encoder drops a segment's trailing spaces).
  bool encodePiece (std::u16string const& text, int profile, bool isFinal, EncodedTransmitSegment& segment,
                    bool& runsOn)
  {
    auto const padded = transmitFrame (text);
    if (padded.size () != static_cast<std::size_t> (Jtty::maxTransmitLength)) return false;
    std::string frame;
    for (char16_t const c : padded) frame += c > 0xFF ? '?' : static_cast<char> (c);
    std::vector<int> tones (Jtty::maxTransmitFrames * Jtty::transmitFrameSymbols);
    char bits[Jtty::maxTransmitFrames * Jtty::transmitFrameBits];
    int frameStarts[Jtty::maxTransmitFrames] = {};
    int nsym = 0;
    int nframes = 0;
    int status = -1;
    genjtty_text_c (&frame[0], profile, isFinal ? 1 : 0, tones.data (), &nsym, bits, &nframes, frameStarts, &status);
    if (status != static_cast<int> (Jtty::NativeEncodeStatus::Ok) || nsym <= 0) return false;
    segment = EncodedTransmitSegment {};
    segment.transmit.tones.assign (tones.begin (), tones.begin () + nsym);
    std::u16string carried;
    for (char const c : frame) carried += static_cast<char16_t> (static_cast<unsigned char> (c));
    segment.transmit.text = trimmed (carried);
    for (int i = 0; i < nframes; ++i) {
      segment.transmit.frameCharStarts.push_back (frameStarts[i] - 1);
      segment.frames.emplace_back (bits + i * Jtty::transmitFrameBits, Jtty::transmitFrameBits);
    }
    segment.final = isFinal;
    // A TEXT5 frame's class bits are 11, and its fifth character is bits 25
    // to 30, 36 for a blank (lib/jtty/jtty_source_codec.f90).
    auto const& last = segment.frames.back ();
    runsOn = last[30] == '1' && last[31] == '1' && std::stoi (last.substr (24, 6), nullptr, 2) != 36;
    return true;
  }
  bool encodePiece (std::u16string const& text, int profile, bool isFinal, EncodedTransmitSegment& segment)
  {
    bool runsOn = false;
    return encodePiece (text, profile, isFinal, segment, runsOn);
  }

  // Whether text's last word is 599, which stays with the token after it.
  bool ends599 (std::u16string const& text)
  {
    auto const t = trimmed (text);
    auto const space = t.find_last_of (u' ');
    return (space == std::u16string::npos ? t : t.substr (space + 1)) == u"599";
  }

  // Text as encodeTransmitText sends it (lines, segments of at most 80
  // characters cut at word boundaries, the last of a line ending the message
  // unless it is the text's last and isFinal is false), continuing open and
  // keeping every message within the 80 characters a receiver keeps of it
  // (lib/streaming_jtty_width.f90), which drops the rest without notice:
  //   - no cut falls between 599 and the token after it, when the two fit one
  //     segment;
  //   - a segment that would pass the 80 columns, or does not encode, is cut
  //     shorter at a word boundary and ends its message, and the line goes on
  //     in a new one;
  //   - when no word of it fits, a message this text left open ends at the
  //     segment before, and otherwise (a message an earlier request left
  //     open, or one word wider than a message) the word is cut where it
  //     fits;
  //   - a segment that ends a word on a full five-character frame, which the
  //     next word would follow with no space, ends its message when the line
  //     goes on;
  //   - a text that leaves its message open with fewer than openReserve
  //     columns left ends it instead.
  // open is the receiver's message before the text and after it.
  EncodedTransmitText planTransmitText (std::u16string const& message, NativeExchangeProfile profile,
                                        bool isFinal, OpenMessage& open)
  {
    EncodedTransmitText result;
    auto const prepared = prepareTransmitText (message);
    if (trimmed (prepared.text).empty ()) return result;
    result.substituted = prepared.substituted;
    int const exchangeProfile = static_cast<int> (profile);
    // The first frame of a text that continues a message may follow a pause.
    bool const continues = !open.fresh ();
    std::vector<std::u16string> encodedTexts;

    std::vector<std::u16string> lines;
    for (std::size_t start = 0;;) {
      auto const end = prepared.text.find (u'\n', start);
      lines.push_back (prepared.text.substr (start, end - start));
      if (end == std::u16string::npos) break;
      start = end + 1;
    }
    int lineOffset = 0;
    for (std::size_t lineIndex = 0; lineIndex < lines.size (); ++lineIndex) {
      auto const& line = lines[lineIndex];
      int const lineSize = static_cast<int> (line.size ());
      int lineEnd = lineSize;     // past its last character that is not a blank
      while (lineEnd > 0 && line[lineEnd - 1] == u' ') --lineEnd;
      bool const endsMessage = lineIndex + 1 < lines.size () || isFinal;
      for (int offset = 0; offset < lineSize;) {
        auto const source = nextTransmitTextSegment (line, offset);
        if (trimmed (source.text).empty ()) {
          offset += source.length;
          continue;
        }
        auto restBlank = [&] (int length) {return offset + length >= lineEnd;};
        // 599 stays with the token after it when the two fit one segment.
        auto boundary = [&] (int length) {
          if (restBlank (length) || !ends599 (line.substr (offset, length))) return true;
          int last = offset + length - 1;           // the 599's last character
          while (line[last] == u' ') --last;
          int start = offset + length;
          while (start < lineSize && line[start] == u' ') ++start;
          auto const end = line.find (u' ', static_cast<std::size_t> (start));
          int const tokenEnd = end == std::u16string::npos ? lineSize : static_cast<int> (end);
          return tokenEnd - (last - 2) > Jtty::maxTransmitLength;
        };
        // The longest prefix shorter than length that ends after a space.
        auto shorter = [&] (int length) {
          for (int n = length - 1; n > 0; --n) {
            if (line[offset + n - 1] == u' ' && !trimmed (line.substr (offset, n)).empty () && boundary (n)) return n;
          }
          return 0;
        };
        int length = source.length;
        if (!boundary (length) && shorter (length) > 0) length = shorter (length);
        int const fullLength = length;

        EncodedTransmitSegment segment;
        OpenMessage after;
        bool cutEnds = false;     // a cut made here ends the message
        bool byCharacter = false;
        bool runsOn = false;
        auto fits = [&] (int n, bool isEnd) {
          after = open;
          return encodePiece (line.substr (offset, n), exchangeProfile, isEnd, segment, runsOn)
            && widthAfter (after, segment.frames, continues && result.segments.empty ())
            && after.columns <= receiverColumns;
        };
        for (;;) {
          bool const isEnd = cutEnds || (restBlank (length) && endsMessage);
          if (fits (length, isEnd)) {
            if (isEnd || !runsOn || restBlank (length)) break;
            if (line[offset + length - 1] != u' ' && line[offset + length] != u' ') break;
            cutEnds = true;
            continue;
          }
          if (!byCharacter && shorter (length) > 0) {
            length = shorter (length);
            cutEnds = true;
            continue;
          }
          if (!byCharacter && !open.fresh () && !result.segments.empty ()) {
            // End this text's open message at the segment before.
            auto& previous = result.segments.back ();
            auto const previousSource = previous.source;
            if (!encodePiece (encodedTexts.back (), exchangeProfile, true, previous)) {
              result.status = Jtty::TransmitTextStatus::EncodingFailed;
              return result;
            }
            previous.source = previousSource;
            open = OpenMessage {};
            length = fullLength;
            cutEnds = false;
            continue;
          }
          // One word too wide for what is left: cut it where it fits.
          if (!byCharacter) {
            byCharacter = true;
            cutEnds = true;
          }
          if (--length <= 0) {
            result.status = Jtty::TransmitTextStatus::EncodingFailed;
            return result;
          }
        }
        segment.source = {lineOffset + offset, length, line.substr (offset, length)};
        result.segments.push_back (std::move (segment));
        encodedTexts.push_back (line.substr (offset, length));
        open = result.segments.back ().final ? OpenMessage {} : after;
        offset += length;
      }
      lineOffset += lineSize + 1;
    }
    if (!result.segments.empty () && !result.segments.back ().final
        && receiverColumns - open.columns < openReserve) {
      auto& last = result.segments.back ();
      auto const lastSource = last.source;
      if (!encodePiece (encodedTexts.back (), exchangeProfile, true, last)) {
        result.status = Jtty::TransmitTextStatus::EncodingFailed;
        return result;
      }
      last.source = lastSource;
      open = OpenMessage {};
    }
    result.status = result.segments.empty () ? Jtty::TransmitTextStatus::Empty : Jtty::TransmitTextStatus::Encoded;
    return result;
  }

  std::mutex transmissionsMutex;
  std::map<std::int32_t, std::unique_ptr<Transmission>> transmissions;
  std::int32_t lastHandle {0};

  std::int32_t hold (std::unique_ptr<Transmission> transmission)
  {
    std::lock_guard<std::mutex> lock {transmissionsMutex};
    for (std::int32_t tries = 0; tries < 1 << 20; ++tries) {
      lastHandle = lastHandle == 0x7FFFFFFF ? 1 : lastHandle + 1;
      if (!transmissions.count (lastHandle)) {
        transmissions.emplace (lastHandle, std::move (transmission));
        return lastHandle;
      }
    }
    return 0;
  }

  std::unique_ptr<Transmission> refusal (TransmitStatus status, char const * key, std::u16string const& detail)
  {
    auto transmission = std::make_unique<Transmission> ();
    transmission->status = status;
    transmission->key = key;
    transmission->detail = detail;
    return transmission;
  }

  // A JSON string's UTF-8: unlike fromUtf8 alone, a leading U+FEFF is a character.
  std::u16string jsonText (char const * text, std::int32_t length)
  {
    std::string bytes {"a"};
    if (text && length > 0) bytes.append (text, static_cast<std::size_t> (length));
    return fromUtf8 (bytes).substr (1);
  }

  // No control character (C0, DEL or C1), which would reshape the message,
  // and no '%', which a later placeholder could expand.
  bool isPlain (std::u16string const& value)
  {
    return std::none_of (value.cbegin (), value.cend (), [] (char16_t c) {
      return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == u'%';
    });
  }

  // A station call or grid configure set: printable ASCII, as callsigns and
  // locators are, and no longer than a his_call.
  bool isStationValue (std::u16string const& value)
  {
    return !trimmed (value).empty () && value.size () <= static_cast<std::size_t> (maxContextText)
      && std::all_of (value.cbegin (), value.cend (), [] (char16_t c) {return c >= 0x20 && c <= 0x7E && c != u'%';});
  }

  // text's UTF-8 in out, cut to capacity at a character boundary.
  std::int32_t copyText (std::u16string const& text, char * out, std::int32_t capacity)
  {
    auto const bytes = toUtf8 (text);
    auto n = std::min<std::size_t> (bytes.size (), capacity > 0 ? capacity : 0);
    if (n < bytes.size ()) {
      while (n > 0 && (static_cast<unsigned char> (bytes[n]) & 0xC0) == 0x80) --n;
    }
    std::copy_n (bytes.data (), n, out);
    return static_cast<std::int32_t> (n);
  }

  // WSJT-X's compileN1mmMessage (widgets/JttyN1mm.hpp) on the encoder's
  // strings, through the same encoder calls and lib/jtty's QString
  // equivalents (isSpace, simplified, toUpper): a TXTEXT from N1MM Logger+ is
  // a native transmission when it starts with a tag, text to send as typed
  // when it has none, or an error.
  enum class N1mmStatus {Literal, Native, Error};

  struct N1mmCompilation
  {
    N1mmStatus status {N1mmStatus::Literal};
    std::vector<NativeAtomDescriptor> atoms;
    std::u16string literalText;
    std::u16string canonicalText;
    std::u16string error;
  };

  N1mmCompilation n1mmFailure (std::u16string const& reason)
  {
    N1mmCompilation result;
    result.status = N1mmStatus::Error;
    result.error = reason;
    return result;
  }

  // QString::compare (..., Qt::CaseInsensitive) == 0 of text.mid (first,
  // ascii.size ()) with ascii, whose characters no other one folds to.
  bool startsWithFolded (std::u16string const& text, std::size_t first, std::u16string const& ascii)
  {
    if (text.size () < first || text.size () - first < ascii.size ()) return false;
    auto const fold = [] (char16_t c) {return c >= u'a' && c <= u'z' ? char16_t (c - u'a' + u'A') : c;};
    for (std::size_t i = 0; i < ascii.size (); ++i) {
      if (fold (text[first + i]) != fold (ascii[i])) return false;
    }
    return true;
  }

  bool n1mmAppendCall (std::vector<NativeAtomDescriptor>& atoms, Jtty::CallAction action,
                       std::u16string const& call, std::u16string& error)
  {
    if (!isNativeCall (call)) {
      error = u"invalid callsign in tagged payload";
      return false;
    }
    atoms.push_back (nativeCallAtom (action, call));
    return true;
  }

  // isDecimal, then QString::toInt, which fails past int's range.
  bool n1mmSerial (std::u16string const& payload, int& serial)
  {
    if (!isDecimal (payload)) return false;
    long long value = 0;
    for (char16_t const c : payload) {
      value = value * 10 + (c - u'0');
      if (value > std::numeric_limits<int>::max ()) return false;
    }
    serial = static_cast<int> (value);
    return serial < (1 << 17);
  }

  NativeExchangeCompilation n1mmExchange (std::u16string const& payload, NativeExchangeProfile profile)
  {
    NativeMacroContext context;
    context.exchangeProfile = profile;
    context.configuredExchange = payload;
    int serial {0};
    if (profile == NativeExchangeProfile::None) {
      if (!n1mmSerial (payload, serial)) {
        NativeExchangeCompilation result;
        result.error = u"serial exchange must be a decimal value between 0 and 131071";
        return result;
      }
      context.serialNumber = serial;
    } else if (profile == NativeExchangeProfile::RttyRoundup) {
      if (n1mmSerial (payload, serial)) {
        context.serialNumber = serial;
      } else if (isCanonicalBase36Token (payload)) {
        NativeExchangeCompilation result;
        result.valid = true;
        result.atom = nativeLocationAtom (Jtty::ExchangeRole::Full, Jtty::LocationKind::StateProvince, payload);
        result.text = u"599 " + payload;
        return result;
      } else {
        NativeExchangeCompilation result;
        result.error = u"tagged RTTY exchange must contain a decimal serial or state/province";
        return result;
      }
    }
    return nativeExchange (context);
  }

  bool isGrid4 (std::u16string const& value)
  {
    return value.size () == 4 && value[0] >= u'A' && value[0] <= u'R' && value[1] >= u'A' && value[1] <= u'R'
      && value[2] >= u'0' && value[2] <= u'9' && value[3] >= u'0' && value[3] <= u'9';
  }

  N1mmCompilation compileN1mm (std::u16string const& message, NativeExchangeProfile profile)
  {
    using Jtty::CallAction;
    std::size_t first = 0;
    while (first < message.size () && isSpace (message[first])) ++first;
    std::u16string const marker {u"[[JTTY:"};
    if (!startsWithFolded (message, first, marker)) {
      if (startsWithFolded (message, first, u"[[JTTY")) return n1mmFailure (u"malformed JTTY action tag");
      N1mmCompilation result;
      result.literalText = message;
      return result;
    }
    auto const close = message.find (u"]]", first + marker.size ());
    if (close == std::u16string::npos) return n1mmFailure (u"malformed JTTY action tag");
    auto const action = toUpper (message.substr (first + marker.size (), close - first - marker.size ()));
    if (action.empty () || action.find_first_of (u"[] \t") != std::u16string::npos) {
      return n1mmFailure (u"malformed JTTY action tag");
    }
    static std::u16string const actions[] {u"CQ", u"CALL_EXCH", u"CALL_TU_CQ", u"MYCALL", u"HISCALL",
                                           u"TU_NOW_EXCH", u"CALL_MY", u"CALL_TU_MY", u"EXCH", u"GRID",
                                           u"CONTROL"};
    if (std::find (std::begin (actions), std::end (actions), action) == std::end (actions)) {
      return n1mmFailure (u"unknown JTTY action: " + action);
    }

    auto const payload = toUpper (simplified (message.substr (close + 2)));
    if (payload.empty ()) return n1mmFailure (u"tagged payload is empty");
    std::vector<std::u16string> fields;
    for (std::size_t at = 0; at <= payload.size ();) {
      auto const space = std::min (payload.find (u' ', at), payload.size ());
      if (space > at) fields.push_back (payload.substr (at, space - at));
      at = space + 1;
    }
    auto const field = [&fields] (std::size_t i) {return i < fields.size () ? fields[i] : std::u16string {};};
    N1mmCompilation result;
    result.status = N1mmStatus::Native;
    std::u16string error;

    if (action == u"CQ") {
      if (fields.size () != 1 || !n1mmAppendCall (result.atoms, CallAction::Cq, field (0), error)) {
        if (error.empty ()) error = u"CQ payload must contain one callsign";
      } else {
        result.canonicalText = u"CQ " + fields[0] + u" CQ";
      }
    } else if (action == u"CALL_EXCH" || action == u"TU_NOW_EXCH") {
      if (fields.size () < 2) {
        error = u"call/exchange payload is incomplete";
      } else {
        auto const& call = fields[0];
        auto const callAction = action == u"CALL_EXCH" ? CallAction::Call : CallAction::TuNowCall;
        if (n1mmAppendCall (result.atoms, callAction, call, error)) {
          std::u16string rest;
          for (std::size_t i = 1; i < fields.size (); ++i) rest += (i > 1 ? u" " : u"") + fields[i];
          auto const exchange = n1mmExchange (rest, profile);
          if (!exchange.valid) {
            error = exchange.error;
            result.atoms.clear ();
          } else {
            result.atoms.push_back (exchange.atom);
            result.canonicalText = action == u"CALL_EXCH" ? call + u" " + exchange.text
              : u"TU NOW " + call + u" " + exchange.text;
          }
        }
      }
    } else if (action == u"CALL_TU_CQ") {
      if (fields.size () != 2 || !n1mmAppendCall (result.atoms, CallAction::CallTu, field (0), error)
          || !n1mmAppendCall (result.atoms, CallAction::Cq, field (1), error)) {
        if (error.empty ()) error = u"CALL_TU_CQ payload must contain two callsigns";
      } else {
        result.canonicalText = fields[0] + u" TU CQ " + fields[1] + u" CQ";
      }
    } else if (action == u"MYCALL" || action == u"HISCALL") {
      if (fields.size () != 1 || !n1mmAppendCall (result.atoms, CallAction::Call, field (0), error)) {
        if (error.empty ()) error = u"call payload must contain one callsign";
      } else {
        result.canonicalText = fields[0];
      }
    } else if (action == u"CALL_MY" || action == u"CALL_TU_MY") {
      bool const withTu = action == u"CALL_TU_MY";
      if (fields.size () != 2
          || !n1mmAppendCall (result.atoms, withTu ? CallAction::CallTu : CallAction::Call, field (0), error)
          || !n1mmAppendCall (result.atoms, CallAction::Call, field (1), error)) {
        if (error.empty ()) error = u"two-call payload must contain two callsigns";
      } else {
        result.canonicalText = withTu ? fields[0] + u" TU " + fields[1] : fields[0] + u" " + fields[1];
      }
    } else if (action == u"EXCH") {
      auto const exchange = n1mmExchange (payload, profile);
      if (!exchange.valid) {
        error = exchange.error;
      } else {
        result.atoms.push_back (exchange.atom);
        result.canonicalText = exchange.text;
      }
    } else if (action == u"GRID") {
      if (fields.size () != 1 || !isGrid4 (fields[0])) {
        error = u"GRID payload must contain one valid four-character grid";
      } else {
        result.atoms.push_back (nativeGridAtom (Jtty::ExchangeRole::FieldOnly, fields[0]));
        result.canonicalText = fields[0];
      }
    } else if (action == u"CONTROL") {
      int const phraseId = controlPhraseId (payload);
      if (phraseId < 0) {
        error = u"unknown CONTROL phrase";
      } else {
        result.atoms.push_back (nativeControlAtom (phraseId));
        result.canonicalText = controlPhrase (phraseId);
      }
    }

    if (!error.empty ()) return n1mmFailure (error);
    if (result.atoms.empty ()) return n1mmFailure (u"tag produced no atoms");
    return result;
  }

  struct Request
  {
    bool isTemplate {false};
    bool isN1mm {false};        // a TXTEXT from N1MM Logger+
    std::u16string text;
    std::u16string hisCall;
    std::u16string exchange;
    std::u16string myCall;      // empty unless usable
    std::u16string grid;        // empty unless usable
    bool callUsable {false};
    bool gridUsable {false};
    bool callSet {false};       // a configure set it, usable or not
    bool gridSet {false};
    bool callOverlong {false};  // longer than maxContextText
    bool gridOverlong {false};
    int serial {0};
    bool serialGiven {false};
    int report {0};
    bool reportGiven {false};
    NativeExchangeProfile profile {NativeExchangeProfile::None};
    bool isFinal {true};
    OpenMessage open;
  };

  // Everything a request's encoding takes from its context.
  struct Outcome
  {
    NativeMacroStatus status {NativeMacroStatus::LiteralFallback};
    std::u16string text;
    std::u16string error;
    std::vector<NativeAtomDescriptor> atoms;
  };

  bool sameAtom (NativeAtomDescriptor const& a, NativeAtomDescriptor const& b)
  {
    return a.kind == b.kind && a.subtype == b.subtype && a.role == b.role && a.reserved == b.reserved
      && a.value == b.value && std::equal (std::begin (a.text), std::end (a.text), std::begin (b.text));
  }

  bool same (Outcome const& a, Outcome const& b)
  {
    return a.status == b.status && a.text == b.text && a.error == b.error
      && std::equal (a.atoms.cbegin (), a.atoms.cend (), b.atoms.cbegin (), b.atoms.cend (), sameAtom);
  }

  Outcome outcome (Request const& request, std::u16string const& myCall, std::u16string const& grid,
                   int serial, int report)
  {
    auto const context = nativeMacroContext (myCall, request.hisCall, serial, grid, report,
                                             request.profile, request.exchange);
    Outcome result;
    if (!request.isTemplate) {
      result.text = expandLiteralMacro (request.text, context);
      return result;
    }
    auto compiled = compileNativeMacro (request.text, context);
    result.status = compiled.status;
    result.text = std::move (compiled.text);
    result.error = std::move (compiled.error);
    result.atoms = std::move (compiled.atoms);
    return result;
  }

  enum Value {Call, Grid, Serial, Report};

  // Whether the request's encoding reads value: exactly when two probes of it
  // give two outcomes. No probe starts with a placeholder letter or holds a
  // '%', so no later replacement consumes the characters that tell a pair
  // apart; each pair is valid wherever the compiler checks the value.
  bool reads (Request const& request, Value value)
  {
    static std::u16string const calls[] {u"K0AA", u"K1BB"};
    static std::u16string const grids[] {u"AA00", u"BB11"};
    static int const serials[] {1, 2};
    static int const reports[] {-10, -11};
    auto probe = [&request, value] (int i) {
      auto at = [value, i] (Value v) {return v == value ? i : 0;};
      return outcome (request, request.callUsable ? request.myCall : calls[at (Call)],
                      request.gridUsable ? request.grid : grids[at (Grid)],
                      request.serialGiven ? request.serial : serials[at (Serial)],
                      request.reportGiven ? request.report : reports[at (Report)]);
    };
    return !same (probe (0), probe (1));
  }

  std::unique_ptr<Transmission> textTransmission (std::u16string const& message, NativeExchangeProfile profile,
                                                  bool isFinal, char const * key, OpenMessage open)
  {
    if (message.size () > static_cast<std::size_t> (maxExpandedText)) {
      return refusal (TransmitStatus::TooLong, key, u"the expanded text is longer than 1048576 UTF-16 code units");
    }
    auto const encoded = planTransmitText (message, profile, isFinal, open);
    switch (encoded.status) {
    case Jtty::TransmitTextStatus::Empty:
      return refusal (TransmitStatus::Empty, "", u"the text has nothing to send");
    case Jtty::TransmitTextStatus::EncodingFailed:
      return refusal (TransmitStatus::EncodingFailed, "", u"the text cannot be encoded");
    case Jtty::TransmitTextStatus::Encoded:
      break;
    }
    auto transmission = std::make_unique<Transmission> ();
    transmission->status = TransmitStatus::Encoded;
    transmission->substituted = encoded.substituted;
    transmission->open = open;
    for (auto const& encodedSegment : encoded.segments) {
      auto const& source = encodedSegment.source;
      Segment segment;
      segment.encoded = encodedSegment;
      segment.text = trimmed (source.text);
      // prepareTransmitText replaces characters one for one.
      segment.substituted = message.compare (source.offset, source.length, source.text) != 0;
      transmission->segments.push_back (std::move (segment));
    }
    return transmission;
  }

  // Atoms sent natively as one segment carrying text. They end the message
  // they continue; when they would pass its 80 columns they go as their text
  // does.
  std::unique_ptr<Transmission> nativeTransmission (std::vector<NativeAtomDescriptor> const& atoms,
                                                    std::u16string const& text, Request const& request,
                                                    char const * key)
  {
    auto const encoded = encodeNativeAtoms (atoms);
    if (encoded.tones.empty ()) return refusal (TransmitStatus::EncodingFailed, "", encoded.error);
    auto after = request.open;
    if (!widthAfter (after, encoded.frames, !request.open.fresh ()) || after.columns > receiverColumns) {
      return textTransmission (text, request.profile, true, key, request.open);
    }
    Segment segment;
    segment.encoded.transmit.text = text;
    segment.encoded.transmit.tones = encoded.tones;
    segment.encoded.source = {0, static_cast<int> (text.size ()), text};
    segment.encoded.frames = encoded.frames;
    segment.encoded.final = true;
    segment.text = text;
    auto transmission = std::make_unique<Transmission> ();
    transmission->status = TransmitStatus::Encoded;
    transmission->segments.push_back (std::move (segment));
    return transmission;
  }

  // An N1MM TXTEXT compiled as WSJT-X compiles it (MMTTYIF::dispatchCommand
  // upper-cases it, MainWindow::handleMmttyTxString compiles it): a tag's
  // atoms with its canonical text, sent as a native template's are; other
  // text as typed text is, with no placeholder expanded. WSJT-X sends each
  // line of that text as one message and a tag's atoms whatever the width;
  // the codec keeps both within the receiver's 80 columns, as it does for
  // text and templates.
  std::unique_ptr<Transmission> n1mmTransmission (Request const& request)
  {
    if (std::any_of (request.text.cbegin (), request.text.cend (), [] (char16_t c) {return c > 0xFF;})) {
      return refusal (TransmitStatus::BadRequest, "n1mm",
                      u"n1mm holds N1MM's bytes, each written in the JSON's UTF-8 as a character from U+0000 to U+00FF");
    }
    auto const compiled = compileN1mm (toUpper (request.text), request.profile);
    switch (compiled.status) {
    case N1mmStatus::Error:
      return refusal (TransmitStatus::EncodingFailed, "n1mm", compiled.error);
    case N1mmStatus::Literal:
      return textTransmission (compiled.literalText, request.profile, request.isFinal, "n1mm", request.open);
    case N1mmStatus::Native:
      break;
    }
    return nativeTransmission (compiled.atoms, compiled.canonicalText, request, "n1mm");
  }

  std::unique_ptr<Transmission> encode (Request const& request)
  {
    if (request.isN1mm) return n1mmTransmission (request);
    char const * const textKey = request.isTemplate ? "template" : "text";
    if (request.isTemplate && simplified (request.text).empty ()) {
      return refusal (TransmitStatus::Empty, "", u"the template has nothing to send");
    }
    if (!request.callUsable && reads (request, Call)) {
      return refusal (TransmitStatus::NotConfigured, "mycall", request.callOverlong
                      ? u"the request uses mycall, and the configured mycall is longer than 64 characters"
                      : request.callSet
                      ? u"the request uses mycall, and the configured mycall is not a usable callsign"
                      : u"the request uses mycall, which no configure has set");
    }
    if (!request.gridUsable && reads (request, Grid)) {
      return refusal (TransmitStatus::NotConfigured, "mygrid", request.gridOverlong
                      ? u"the request uses mygrid, and the configured mygrid is longer than 64 characters"
                      : request.gridSet
                      ? u"the request uses mygrid, and the configured mygrid is not a usable grid"
                      : u"the request uses mygrid, which no configure has set");
    }
    if (!request.serialGiven && reads (request, Serial)) {
      return refusal (TransmitStatus::Missing, "serial", u"the request uses serial and does not give it");
    }
    if (!request.reportGiven && reads (request, Report)) {
      return refusal (TransmitStatus::Missing, "report", u"the request uses report and does not give it");
    }

    auto const context = nativeMacroContext (request.myCall, request.hisCall, request.serial, request.grid,
                                             request.report, request.profile, request.exchange);
    if (!request.isTemplate) {
      return textTransmission (expandLiteralMacro (request.text, context), request.profile, request.isFinal, textKey,
                               request.open);
    }
    auto const compiled = compileNativeMacro (request.text, context);
    switch (compiled.status) {
    case Jtty::NativeMacroStatus::LiteralFallback:
      return textTransmission (compiled.text, request.profile, true, textKey, request.open);
    case Jtty::NativeMacroStatus::InvalidRuntime:
      return refusal (TransmitStatus::InvalidRuntime, "", compiled.error);
    case Jtty::NativeMacroStatus::Native:
      break;
    }
    return nativeTransmission (compiled.atoms, compiled.text, request, textKey);
  }

  std::unique_ptr<Transmission> transmission (std::int32_t kind, char const * text, std::int32_t textLength,
                                              char const * hisCall, std::int32_t hisCallLength,
                                              char const * exchange, std::int32_t exchangeLength,
                                              char const * myCall, std::int32_t myCallLength,
                                              char const * grid, std::int32_t gridLength,
                                              std::int32_t serial, std::int32_t serialGiven,
                                              std::int32_t report, std::int32_t reportGiven,
                                              std::int32_t profile, std::int32_t isFinal,
                                              std::int32_t openColumns, std::int32_t openSeparator)
  {
    Request request;
    request.open = {openColumns, openSeparator != 0};
    request.isTemplate = kind == 1;
    request.isN1mm = kind == 2;
    request.text = jsonText (text, textLength);
    if (request.isN1mm && request.text.size () > static_cast<std::size_t> (maxN1mmText)) {
      return refusal (TransmitStatus::TooLong, "n1mm", u"n1mm is longer than 65536 characters");
    }
    if (!request.isN1mm && request.text.size () > static_cast<std::size_t> (maxRequestText)) {
      return refusal (TransmitStatus::TooLong, request.isTemplate ? "template" : "text",
                      u"the text is longer than 32767 UTF-16 code units");
    }
    request.hisCall = jsonText (hisCall, hisCallLength);
    request.exchange = jsonText (exchange, exchangeLength);
    struct ContextField { std::u16string const * value; char const * key; };
    ContextField const contextFields[] {{&request.hisCall, "his_call"}, {&request.exchange, "exchange"}};
    for (auto const& [value, key] : contextFields) {
      if (value->size () > static_cast<std::size_t> (maxContextText) || !isPlain (*value)) {
        return refusal (TransmitStatus::BadRequest, key,
                        u"his_call and exchange hold at most 64 UTF-16 code units, with no control character or '%'");
      }
    }
    if (profile < 0 || profile > static_cast<int> (NativeExchangeProfile::RttyRoundup)) {
      return refusal (TransmitStatus::BadRequest, "profile", u"profile must be none, field_day or rtty");
    }
    request.profile = static_cast<NativeExchangeProfile> (profile);
    auto const station = jsonText (myCall, myCallLength);
    request.callSet = myCallLength != -1;
    request.callOverlong = myCallLength == -2;
    request.callUsable = myCallLength >= 0 && isStationValue (station);
    if (request.callUsable) request.myCall = station;
    auto const locator = jsonText (grid, gridLength);
    request.gridSet = gridLength != -1;
    request.gridOverlong = gridLength == -2;
    request.gridUsable = gridLength >= 0 && isStationValue (locator);
    if (request.gridUsable) request.grid = locator;
    request.serialGiven = serialGiven != 0;
    if (request.serialGiven) request.serial = serial;
    request.reportGiven = reportGiven != 0;
    if (request.reportGiven) request.report = report;
    request.isFinal = isFinal != 0;
    return encode (request);
  }
}

extern "C" {
  std::int32_t jtty_tx_encode (std::int32_t kind, char const * text, std::int32_t text_length,
                               char const * his_call, std::int32_t his_call_length,
                               char const * exchange, std::int32_t exchange_length,
                               char const * my_call, std::int32_t my_call_length,
                               char const * grid, std::int32_t grid_length,
                               std::int32_t serial, std::int32_t serial_given,
                               std::int32_t report, std::int32_t report_given,
                               std::int32_t profile, std::int32_t is_final,
                               std::int32_t open_columns, std::int32_t open_separator) noexcept
  {
    try {
      return hold (transmission (kind, text, text_length, his_call, his_call_length, exchange,
                                 exchange_length, my_call, my_call_length, grid, grid_length, serial,
                                 serial_given, report, report_given, profile, is_final, open_columns,
                                 open_separator));
    } catch (...) {
      return 0;
    }
  }

  void jtty_tx_open (std::int32_t handle, std::int32_t * columns, std::int32_t * separator) noexcept
  {
    *columns = 0;
    *separator = 0;
    try {
      std::lock_guard<std::mutex> lock {transmissionsMutex};
      auto const found = transmissions.find (handle);
      if (found == transmissions.end ()) return;
      *columns = found->second->open.columns;
      *separator = found->second->open.separator ? 1 : 0;
    } catch (...) {
    }
  }

  std::int32_t jtty_tx_status (std::int32_t handle, std::int32_t * segments, std::int32_t * substituted) noexcept
  {
    *segments = 0;
    *substituted = 0;
    try {
      std::lock_guard<std::mutex> lock {transmissionsMutex};
      auto const found = transmissions.find (handle);
      if (found == transmissions.end ()) return static_cast<std::int32_t> (TransmitStatus::EncodingFailed);
      auto const& transmission = *found->second;
      *segments = static_cast<std::int32_t> (transmission.segments.size ());
      *substituted = transmission.substituted ? 1 : 0;
      return static_cast<std::int32_t> (transmission.status);
    } catch (...) {
      return static_cast<std::int32_t> (TransmitStatus::EncodingFailed);
    }
  }

  void jtty_tx_error (std::int32_t handle, char * key, std::int32_t key_capacity, std::int32_t * key_length,
                      char * detail, std::int32_t detail_capacity, std::int32_t * detail_length) noexcept
  {
    *key_length = 0;
    *detail_length = 0;
    try {
      std::lock_guard<std::mutex> lock {transmissionsMutex};
      auto const found = transmissions.find (handle);
      if (found == transmissions.end ()) return;
      auto const& transmission = *found->second;
      auto const n = std::min<std::size_t> (transmission.key.size (), key_capacity > 0 ? key_capacity : 0);
      std::copy_n (transmission.key.data (), n, key);
      *key_length = static_cast<std::int32_t> (n);
      *detail_length = copyText (transmission.detail, detail, detail_capacity);
    } catch (...) {
    }
  }

  std::int32_t jtty_tx_segment (std::int32_t handle, std::int32_t index, std::int32_t * tones,
                                char * frames, std::int32_t * nframes,
                                char * text, std::int32_t * text_length,
                                char * canonical, std::int32_t * canonical_length,
                                std::int32_t * is_final, std::int32_t * substituted) noexcept
  {
    try {
      std::lock_guard<std::mutex> lock {transmissionsMutex};
      auto const found = transmissions.find (handle);
      if (found == transmissions.end () || index < 0
          || static_cast<std::size_t> (index) >= found->second->segments.size ()) {
        return 0;
      }
      auto const& segment = found->second->segments[index];
      auto const& encoded = segment.encoded;
      if (encoded.transmit.tones.size () > static_cast<std::size_t> (Jtty::maxTransmitFrames * Jtty::transmitFrameSymbols)
          || encoded.frames.size () > static_cast<std::size_t> (Jtty::maxTransmitFrames)) {
        return 0;
      }
      std::copy (encoded.transmit.tones.cbegin (), encoded.transmit.tones.cend (), tones);
      for (std::size_t i = 0; i < encoded.frames.size (); ++i) {
        std::copy_n (encoded.frames[i].data (), std::min<std::size_t> (encoded.frames[i].size (), Jtty::transmitFrameBits),
                     frames + i * Jtty::transmitFrameBits);
      }
      *nframes = static_cast<std::int32_t> (encoded.frames.size ());
      *text_length = copyText (segment.text, text, Jtty::maxTransmitLength);
      *canonical_length = copyText (encoded.transmit.text, canonical, Jtty::maxTransmitLength);
      *is_final = encoded.final ? 1 : 0;
      *substituted = segment.substituted ? 1 : 0;
      return static_cast<std::int32_t> (encoded.transmit.tones.size ());
    } catch (...) {
      return 0;
    }
  }

  void jtty_tx_destroy (std::int32_t handle) noexcept
  {
    try {
      std::lock_guard<std::mutex> lock {transmissionsMutex};
      transmissions.erase (handle);
    } catch (...) {
    }
  }

  std::int32_t jtty_tx_render (std::int32_t const * tones, std::int32_t nsym, std::int32_t sample_rate,
                               float f0, std::int16_t * samples, std::int32_t capacity) noexcept
  {
    try {
      if (nsym <= 0 || nsym > Jtty::maxTransmitFrames * Jtty::transmitFrameSymbols) return 0;
      std::vector<int> symbols (tones, tones + nsym);
      auto const audio = renderTransmitTones (symbols.data (), nsym, sample_rate, f0);
      if (capacity < 0 || audio.empty () || audio.size () > static_cast<std::size_t> (capacity)) return 0;
      std::copy (audio.cbegin (), audio.cend (), samples);
      return static_cast<std::int32_t> (audio.size ());
    } catch (...) {
      return 0;
    }
  }
}
