// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyTransmitHost.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "JttyTransmit.hpp"

namespace
{
  using namespace Jtty::Encoder;
  using Jtty::NativeAtomDescriptor;
  using Jtty::NativeExchangeProfile;
  using Jtty::NativeMacroStatus;

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
  };

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

  std::int32_t copyText (std::u16string const& text, char * out, std::int32_t capacity)
  {
    auto const bytes = toUtf8 (text);
    auto const n = std::min<std::size_t> (bytes.size (), capacity > 0 ? capacity : 0);
    std::copy_n (bytes.data (), n, out);
    return static_cast<std::int32_t> (n);
  }

  struct Request
  {
    bool isTemplate {false};
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
                                                  bool isFinal, char const * key)
  {
    if (message.size () > static_cast<std::size_t> (maxExpandedText)) {
      return refusal (TransmitStatus::TooLong, key, u"the expanded text is longer than 1048576 UTF-16 code units");
    }
    auto const encoded = encodeTransmitText (message, profile, isFinal);
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

  std::unique_ptr<Transmission> encode (Request const& request)
  {
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
      return textTransmission (expandLiteralMacro (request.text, context), request.profile, request.isFinal, textKey);
    }
    auto const compiled = compileNativeMacro (request.text, context);
    switch (compiled.status) {
    case Jtty::NativeMacroStatus::LiteralFallback:
      return textTransmission (compiled.text, request.profile, true, textKey);
    case Jtty::NativeMacroStatus::InvalidRuntime:
      return refusal (TransmitStatus::InvalidRuntime, "", compiled.error);
    case Jtty::NativeMacroStatus::Native:
      break;
    }
    auto const encoded = encodeNativeAtoms (compiled.atoms);
    if (encoded.tones.empty ()) return refusal (TransmitStatus::EncodingFailed, "", encoded.error);
    Segment segment;
    segment.encoded.transmit.text = compiled.text;
    segment.encoded.transmit.tones = encoded.tones;
    segment.encoded.source = {0, static_cast<int> (compiled.text.size ()), compiled.text};
    segment.encoded.frames = encoded.frames;
    segment.encoded.final = true;
    segment.text = compiled.text;
    auto transmission = std::make_unique<Transmission> ();
    transmission->status = TransmitStatus::Encoded;
    transmission->segments.push_back (std::move (segment));
    return transmission;
  }

  std::unique_ptr<Transmission> transmission (std::int32_t isTemplate, char const * text, std::int32_t textLength,
                                              char const * hisCall, std::int32_t hisCallLength,
                                              char const * exchange, std::int32_t exchangeLength,
                                              char const * myCall, std::int32_t myCallLength,
                                              char const * grid, std::int32_t gridLength,
                                              std::int32_t serial, std::int32_t serialGiven,
                                              std::int32_t report, std::int32_t reportGiven,
                                              std::int32_t profile, std::int32_t isFinal)
  {
    Request request;
    request.isTemplate = isTemplate != 0;
    request.text = jsonText (text, textLength);
    if (request.text.size () > static_cast<std::size_t> (maxRequestText)) {
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
  std::int32_t jtty_tx_encode (std::int32_t is_template, char const * text, std::int32_t text_length,
                               char const * his_call, std::int32_t his_call_length,
                               char const * exchange, std::int32_t exchange_length,
                               char const * my_call, std::int32_t my_call_length,
                               char const * grid, std::int32_t grid_length,
                               std::int32_t serial, std::int32_t serial_given,
                               std::int32_t report, std::int32_t report_given,
                               std::int32_t profile, std::int32_t is_final) noexcept
  {
    try {
      return hold (transmission (is_template, text, text_length, his_call, his_call_length, exchange,
                                 exchange_length, my_call, my_call_length, grid, grid_length, serial,
                                 serial_given, report, report_given, profile, is_final));
    } catch (...) {
      return 0;
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
}
