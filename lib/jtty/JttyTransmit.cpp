// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyTransmit.hpp"

#include <algorithm>
#include <cstdio>

namespace
{
  std::u16string fromLatin1 (char const * text, std::size_t length)
  {
    std::u16string result;
    result.reserve (length);
    for (std::size_t i = 0; i < length; ++i) result += static_cast<unsigned char> (text[i]);
    return result;
  }

  // As QString::toLatin1: a unit above U+00FF becomes '?'.
  std::string toLatin1 (std::u16string const& text)
  {
    std::string result;
    result.reserve (text.size ());
    for (char16_t const c : text) result += c > 0xFF ? '?' : static_cast<char> (c);
    return result;
  }

  std::u16string decimal (char const * format, int value)
  {
    char text[16];
    int const n = std::snprintf (text, sizeof text, format, value);
    return fromLatin1 (text, n > 0 ? static_cast<std::size_t> (n) : 0);
  }

  // Each of n frames as a string of Jtty::transmitFrameBits '0'/'1' characters.
  std::vector<std::string> frameStrings (char const bits[], int n)
  {
    std::vector<std::string> frames;
    for (int i = 0; i < n; ++i) {
      frames.emplace_back (bits + i * Jtty::transmitFrameBits, Jtty::transmitFrameBits);
    }
    return frames;
  }

  // As Qt 5's qRound (float), which differs from std::lround near halves.
  int qtRound (float d)
  {
    return d >= 0.0f ? int (d + 0.5f) : int (d - float (int (d - 1)) + 0.5f) + int (d - 1);
  }

  struct NativeFormEntry
  {
    Jtty::Encoder::NativeForm form;
    char16_t const * text;
    bool exact;
  };

  using Form = Jtty::Encoder::NativeForm;
  NativeFormEntry const nativeForms[] {
    {Form::Cq, u"CQ %M CQ", false},
    {Form::CallExchange, u"%H %E", false},
    {Form::CallSerial, u"%H 599 %N", true},
    {Form::CallGrid, u"%H %G", false},
    {Form::CallTuCq, u"%H TU CQ %M CQ", false},
    {Form::MyCall, u"%M", false},
    {Form::HisCall, u"%H", false},
    {Form::TuNowExchange, u"TU NOW %Q %E", false},
    {Form::TuNowSerial, u"TU NOW %Q 599 %N", true},
    {Form::TuNowGrid, u"TU NOW %Q %G", false},
    {Form::CallAgn, u"%H AGN?", false},
    {Form::Exchange, u"%E", false},
    {Form::Serial, u"599 %N", true},
    {Form::Grid, u"%G", false},
    {Form::ReportGrid, u"599 %G", false}
  };

  // As Qt's QRegularExpression "^[A-R]{2}[0-9]{2}(?:[A-X]{2}(?:[0-9]{2})?)?$".
  bool isLocator (std::u16string const& locator)
  {
    auto in = [&locator] (std::size_t i, char16_t low, char16_t high) {
      return locator[i] >= low && locator[i] <= high;
    };
    auto const size = locator.size ();
    if (size != 4 && size != 6 && size != 8) return false;
    if (!in (0, u'A', u'R') || !in (1, u'A', u'R') || !in (2, u'0', u'9') || !in (3, u'0', u'9')) {
      return false;
    }
    if (size >= 6 && (!in (4, u'A', u'X') || !in (5, u'A', u'X'))) return false;
    return size < 8 || (in (6, u'0', u'9') && in (7, u'0', u'9'));
  }
}

namespace Jtty::Encoder
{
  namespace
  {
    std::u16string sourceAlphabet ()
    {
      // Keep in sync with ALPHABET in lib/jtty/jtty_source_codec.f90.
      return u"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!\"#$%,&*()_'=[]{}<>|:;";
    }

    bool isSourceCharacter (char16_t c)
    {
      return sourceAlphabet ().find (c) != std::u16string::npos || (c >= u'a' && c <= u'z');
    }
  }

  PreparedTransmitText prepareTransmitText (std::u16string const& message)
  {
    PreparedTransmitText result;
    result.text.reserve (message.size ());

    for (char16_t c : message) {
      if (c == u'\0' || c == u'~') {
        result.text += u' ';
        result.substituted = true;
      } else if (c == u'\n' || c == u'\r') {
        // A forced segment boundary, not a character; encodeTransmitText splits on it.
        result.text += u'\n';
      } else if (isSourceCharacter (c)) {
        result.text += c;
      } else {
        result.text += u'#';
        result.substituted = true;
      }
    }

    return result;
  }

  TransmitTextSegment nextTransmitTextSegment (std::u16string const& message, int offset,
                                               int maximumLength)
  {
    int const size = static_cast<int> (message.size ());
    if (offset < 0 || offset >= size || maximumLength <= 0) return {};

    int length = std::min ({size - offset, maximumLength, maxTransmitLength});
    if (offset + length < size && message[offset + length] != u' ') {
      for (int i = length - 1; i > 0; --i) {
        if (message[offset + i] == u' ' && !trimmed (message.substr (offset, i)).empty ()) {
          length = i + 1;
          break;
        }
      }
    }
    return {offset, length, message.substr (offset, length)};
  }

  std::u16string transmitFrame (std::u16string const& message)
  {
    if (message.size () > static_cast<std::size_t> (maxTransmitLength)) return {};
    return message + std::u16string (maxTransmitLength - message.size (), u' ');
  }

  EncodedTransmitText encodeTransmitText (std::u16string const& message,
                                          NativeExchangeProfile profile, bool isFinal)
  {
    EncodedTransmitText result;
    auto const prepared = prepareTransmitText (message);
    if (trimmed (prepared.text).empty ()) return result;
    result.substituted = prepared.substituted;

    int const exchangeProfile = static_cast<int> (profile);
    // Fills segment's transmit and frames; false when the text does not encode.
    auto encodeOnce = [exchangeProfile] (std::u16string const& text, bool segmentIsFinal,
                                         EncodedTransmitSegment& segment) {
      auto frame = toLatin1 (transmitFrame (text));
      if (frame.size () != static_cast<std::size_t> (maxTransmitLength)) return false;
      std::vector<int> tones (maxTransmitFrames * transmitFrameSymbols);
      char bits[maxTransmitFrames * transmitFrameBits];
      int frameStarts[maxTransmitFrames] = {};
      int nsym = 0;
      int nframes = 0;
      int status = -1;
      genjtty_text_c (&frame[0], exchangeProfile, segmentIsFinal ? 1 : 0, tones.data (), &nsym,
                      bits, &nframes, frameStarts, &status);
      if (status != static_cast<int> (NativeEncodeStatus::Ok) || nsym <= 0) return false;
      segment.transmit = TransmitSegment {};
      segment.transmit.tones.assign (tones.begin (), tones.begin () + nsym);
      segment.transmit.text = trimmed (fromLatin1 (frame.data (), frame.size ()));
      for (int i = 0; i < nframes; ++i) {
        segment.transmit.frameCharStarts.push_back (frameStarts[i] - 1);
      }
      segment.frames = frameStrings (bits, nframes);
      return true;
    };

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
      auto const lineStart = result.segments.size ();
      for (int offset = 0; offset < lineSize;) {
        auto const source = nextTransmitTextSegment (line, offset);
        if (trimmed (source.text).empty ()) {
          offset += source.length;
          continue;
        }
        EncodedTransmitSegment segment;
        if (!encodeOnce (source.text, /*segmentIsFinal=*/false, segment)) {
          result.status = TransmitTextStatus::EncodingFailed;
          return result;
        }
        segment.source = {lineOffset + source.offset, source.length, source.text};
        result.segments.push_back (std::move (segment));
        offset += source.length;
      }
      lineOffset += lineSize + 1;
      if (result.segments.size () == lineStart) continue;
      bool const lineIsFinal = lineIndex + 1 < lines.size () || isFinal;
      if (!lineIsFinal) continue;
      auto& last = result.segments.back ();
      if (!encodeOnce (last.transmit.text, /*segmentIsFinal=*/true, last)) {
        result.status = TransmitTextStatus::EncodingFailed;
        return result;
      }
      last.final = true;
    }
    result.status = TransmitTextStatus::Encoded;
    return result;
  }

  namespace
  {
    std::u16string nativeEncodeError (int status)
    {
      if (status == static_cast<int> (NativeEncodeStatus::UnknownSection)) {
        return u"Field Day section is not registered in the ARRL/RAC table";
      }
      return u"native atom encoding failed";
    }
  }

  NativeAtomEncoding encodeNativeAtoms (std::vector<NativeAtomDescriptor> const& atoms)
  {
    NativeAtomEncoding result;
    std::vector<int> tones (maxTransmitFrames * transmitFrameSymbols);
    char bits[maxTransmitFrames * transmitFrameBits];
    int nsym = 0;
    int nframes = 0;
    int status = static_cast<int> (NativeEncodeStatus::InvalidDescriptor);
    if (!atoms.empty ()) {
      genjtty_atoms_frames_c (atoms.data (), static_cast<int> (atoms.size ()), tones.data (), &nsym,
                              bits, &nframes, &status);
    }
    if (nsym > 0) {
      result.tones.assign (tones.begin (), tones.begin () + nsym);
      result.frames = frameStrings (bits, nframes);
    } else {
      result.error = nativeEncodeError (status);
    }
    return result;
  }

  std::vector<std::int16_t> renderTransmitTones (int const tones[], int nsym, int sampleRate,
                                                 float f0)
  {
    int nsps = 384 * sampleRate / 12000;
    float bt = 2.0;
    float fsample = float (sampleRate);
    int icmplx = 0;
    int nwave = nsps * nsym;

    std::vector<float> wave (nwave > 0 ? nwave : 1);
    gen_jttywave_ (tones, &nsym, &nsps, &bt, &fsample, &f0,
                   wave.data (), wave.data (), &icmplx, &nwave);

    std::vector<std::int16_t> samples;
    samples.reserve (nwave > 0 ? nwave : 0);
    for (int i = 0; i < nwave; ++i) {
      float v = wave[i] * 32767.0f;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32768.0f) v = -32768.0f;
      samples.push_back (static_cast<std::int16_t> (qtRound (v)));
    }
    return samples;
  }

  NativeMacroContext nativeMacroContext (std::u16string const& myCall,
                                         std::u16string const& hisCall, int serialNumber,
                                         std::u16string const& grid, int snr,
                                         NativeExchangeProfile profile,
                                         std::u16string const& exchange)
  {
    NativeMacroContext context;
    context.myCall = myCall;
    context.hisCall = hisCall;
    context.serialNumber = serialNumber;
    context.grid = grid;
    context.snr = snr;
    context.exchangeProfile = profile;

    switch (profile) {
    case NativeExchangeProfile::FieldDay:
      context.configuredExchange = normalizedFieldDayExchange (exchange);
      break;
    case NativeExchangeProfile::RttyRoundup:
      context.configuredExchange = exchange;
      break;
    default:
      break;
    }
    return context;
  }

  std::u16string nativeFormTemplate (NativeForm form)
  {
    for (auto const& entry : nativeForms) {
      if (entry.form == form) return entry.text;
    }
    return {};
  }

  namespace
  {
    std::u16string normalizedMacroTemplate (std::u16string const& macroTemplate)
    {
      return toUpper (simplified (macroTemplate));
    }

    NativeForm nativeForm (std::u16string const& macroTemplate)
    {
      std::u16string const normalized = normalizedMacroTemplate (macroTemplate);
      for (auto const& entry : nativeForms) {
        if ((entry.exact ? macroTemplate : normalized) == entry.text) return entry.form;
      }
      return NativeForm::None;
    }
  }

  std::u16string formatSerialNumber (int serialNumber)
  {
    return decimal ("%03d", serialNumber);
  }

  std::u16string formatSnr (int snr)
  {
    return decimal ("%+03d", snr);
  }

  namespace
  {
    std::u16string nativeExchangeFieldText (NativeMacroContext const& context)
    {
      std::u16string const configured = toUpper (simplified (context.configuredExchange));
      if (context.exchangeProfile == NativeExchangeProfile::None
          || (context.exchangeProfile == NativeExchangeProfile::RttyRoundup
              && (configured == u"DX" || configured == u"#"))) {
        return formatSerialNumber (context.serialNumber);
      }
      return configured;
    }
  }

  std::u16string expandLiteralMacro (std::u16string macroTemplate,
                                     NativeMacroContext const& context)
  {
    // As QString::replace: left to right, the inserted text not searched again.
    auto replace = [&macroTemplate] (std::u16string const& placeholder,
                                     std::u16string const& value) {
      for (auto at = macroTemplate.find (placeholder); at != std::u16string::npos;
           at = macroTemplate.find (placeholder, at + value.size ())) {
        macroTemplate.replace (at, placeholder.size (), value);
      }
    };
    replace (u"%M", context.myCall);
    replace (u"%H", context.hisCall);
    replace (u"%Q", context.hisCall);
    replace (u"%N", formatSerialNumber (context.serialNumber));
    replace (u"%E", nativeExchangeFieldText (context));
    replace (u"%G", toUpper (trimmed (context.grid)).substr (0, 4));
    replace (u"%R", formatSnr (context.snr));
    return macroTemplate;
  }

  namespace
  {
    std::u16string normalizedNativeCall (std::u16string const& call)
    {
      return toUpper (trimmed (call));
    }
  }

  bool isNativeCall (std::u16string const& rawCall)
  {
    std::u16string const call = normalizedNativeCall (rawCall);
    int const size = static_cast<int> (call.size ());
    if (size < 3 || size > 6 || call[0] == u'Q') {
      return false;
    }

    int area = -1;
    for (int i = size - 1; i >= 1; --i) {
      char16_t const c = call[i];
      if (c >= u'0' && c <= u'9') {
        area = i;
        break;
      }
    }
    if (area != 1 && area != 2) return false;

    bool prefixHasLetter {false};
    for (int i = 0; i < area; ++i) {
      char16_t const c = call[i];
      bool const isLetter = c >= u'A' && c <= u'Z';
      bool const isDigit = c >= u'0' && c <= u'9';
      if (!isLetter && !isDigit) return false;
      prefixHasLetter = prefixHasLetter || isLetter;
    }
    if (!prefixHasLetter || area == size - 1) return false;

    for (int i = area + 1; i < size; ++i) {
      char16_t const c = call[i];
      if (c < u'A' || c > u'Z') return false;
    }
    return size - area - 1 <= 3;
  }

  namespace
  {
    NativeAtomDescriptor nativeSerialAtom (int serialNumber)
    {
      NativeAtomDescriptor atom;
      atom.kind = static_cast<std::int8_t> (NativeAtomKind::ExchangeNumber);
      atom.subtype = static_cast<std::int8_t> (NumberKind::Serial);
      atom.role = static_cast<std::int8_t> (ExchangeRole::Full);
      atom.value = serialNumber;
      return atom;
    }

    void setNativeAtomText (NativeAtomDescriptor& atom, std::u16string const& value)
    {
      std::string const text = toLatin1 (value);
      std::copy_n (text.cbegin (), std::min<std::size_t> (text.size (), 8), atom.text);
    }
  }

  NativeAtomDescriptor nativeCallAtom (CallAction action, std::u16string const& rawCall)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<std::int8_t> (NativeAtomKind::Call);
    atom.subtype = static_cast<std::int8_t> (action);
    setNativeAtomText (atom, normalizedNativeCall (rawCall));
    return atom;
  }

  NativeAtomDescriptor nativeLocationAtom (ExchangeRole role, LocationKind kind,
                                           std::u16string const& token)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<std::int8_t> (NativeAtomKind::ExchangeLocation);
    atom.subtype = static_cast<std::int8_t> (kind);
    atom.role = static_cast<std::int8_t> (role);
    setNativeAtomText (atom, token);
    return atom;
  }

  NativeAtomDescriptor nativeClassSectionAtom (int count, char16_t classLetter,
                                               std::u16string const& section)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<std::int8_t> (NativeAtomKind::ExchangePair);
    atom.subtype = static_cast<std::int8_t> (PairSchema::ClassSection);
    atom.role = static_cast<std::int8_t> (classLetter - u'A');
    atom.value = count;
    setNativeAtomText (atom, section);
    return atom;
  }

  NativeAtomDescriptor nativeControlAtom (int phraseId)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<std::int8_t> (NativeAtomKind::Control);
    atom.subtype = static_cast<std::int8_t> (phraseId);
    return atom;
  }

  NativeAtomDescriptor nativeGridAtom (ExchangeRole role, std::u16string const& grid)
  {
    NativeAtomDescriptor atom;
    atom.kind = static_cast<std::int8_t> (NativeAtomKind::Grid4);
    atom.role = static_cast<std::int8_t> (role);
    setNativeAtomText (atom, grid);
    return atom;
  }

  bool isDecimal (std::u16string const& value)
  {
    if (value.empty ()) return false;
    for (char16_t const c : value) {
      if (c < u'0' || c > u'9') return false;
    }
    return true;
  }

  std::u16string normalizedFieldDayExchange (std::u16string const& value)
  {
    std::u16string exchange = toUpper (simplified (value));
    if (exchange.find (u' ') != std::u16string::npos) return exchange;

    std::size_t classPosition {0};
    while (classPosition < exchange.size ()
           && exchange[classPosition] >= u'0'
           && exchange[classPosition] <= u'9') {
      ++classPosition;
    }
    if (classPosition > 0 && classPosition + 1 < exchange.size ()
        && exchange[classPosition] >= u'A'
        && exchange[classPosition] <= u'F') {
      exchange.insert (classPosition + 1, 1, u' ');
    }
    return exchange;
  }

  bool isCanonicalBase36Token (std::u16string const& value)
  {
    if (value.size () != 2 && value.size () != 3) return false;
    if (value.size () == 3 && value[0] == u'0') return false;
    for (char16_t const c : value) {
      bool const digit = c >= u'0' && c <= u'9';
      bool const letter = c >= u'A' && c <= u'Z';
      if (!digit && !letter) return false;
    }
    return true;
  }

  std::u16string controlPhrase (int phraseId)
  {
    static char16_t const * const phrases[] {
      u"AGN?",
      u"CALL?",
      u"AGN CALL",
      u"NR?",
      u"AGN NR",
      u"EXCH?",
      u"STATE?",
      u"SECTION?",
      u"ZONE?",
      u"GRID?",
      u"RPRT?",
      u"QSL TU",
      u"TU",
      u"QRZ?",
      u"QSO B4",
      u"WAIT",
      u"NIL?",
      u"OK?"
    };
    if (phraseId < 0 || phraseId >= 18) return {};
    return phrases[phraseId];
  }

  int controlPhraseId (std::u16string const& macroTemplate)
  {
    std::u16string const normalized = normalizedMacroTemplate (macroTemplate);
    for (int phraseId = 0; phraseId < 18; ++phraseId) {
      if (normalized == controlPhrase (phraseId)) return phraseId;
    }
    return -1;
  }

  NativeExchangeCompilation nativeExchange (NativeMacroContext const& context)
  {
    NativeExchangeCompilation result;
    auto invalid = [&result] (std::u16string const& error) {
      result.error = error;
      return result;
    };
    auto serial = [&result] (int value) {
      if (value < 0 || value >= (1 << 17)) return false;
      result.valid = true;
      result.atom = nativeSerialAtom (value);
      result.text = u"599 " + formatSerialNumber (value);
      return true;
    };
    // QString::toInt of decimal digits: false when it overflows int.
    auto toInt = [] (std::u16string const& digits, int& value) {
      long long parsed = 0;
      for (char16_t const c : digits) {
        parsed = parsed * 10 + (c - u'0');
        if (parsed > 2147483647LL) return false;
      }
      value = static_cast<int> (parsed);
      return true;
    };

    std::u16string const configured = toUpper (simplified (context.configuredExchange));
    switch (context.exchangeProfile) {
    case NativeExchangeProfile::None:
      if (!serial (context.serialNumber)) {
        return invalid (u"Serial number must be between 0 and 131071");
      }
      return result;

    case NativeExchangeProfile::FieldDay: {
      auto const space = configured.find (u' ');
      if (space == std::u16string::npos || configured.find (u' ', space + 1) != std::u16string::npos
          || space < 2) {
        return invalid (u"Field Day exchange must be COUNTCLASS SECTION");
      }
      std::u16string const countClass = configured.substr (0, space);
      char16_t const classLetter = countClass.back ();
      std::u16string const countText = countClass.substr (0, countClass.size () - 1);
      int count {0};
      if (!isDecimal (countText) || !toInt (countText, count) || count < 1 || count > 32) {
        return invalid (u"Field Day transmitter count must be between 1 and 32");
      }
      if (classLetter < u'A' || classLetter > u'F') {
        return invalid (u"Field Day class must be A through F");
      }
      std::u16string const section = configured.substr (space + 1);
      if (!isCanonicalBase36Token (section)) {
        return invalid (u"Field Day section must be a canonical 2-3 character code");
      }
      result.valid = true;
      result.atom = nativeClassSectionAtom (count, classLetter, section);
      result.text = decimal ("%d", count) + classLetter + u' ' + section;
      return result;
    }

    case NativeExchangeProfile::RttyRoundup:
      if (configured == u"DX" || configured == u"#") {
        if (!serial (context.serialNumber)) {
          return invalid (u"Serial number must be between 0 and 131071");
        }
        return result;
      }
      if (isDecimal (configured)) {
        int value {0};
        if (!toInt (configured, value) || !serial (value)) {
          return invalid (u"Configured serial must be between 0 and 131071");
        }
        return result;
      }
      if (!isCanonicalBase36Token (configured)) {
        return invalid (
          u"RTTY exchange must be DX, #, a decimal serial, or a canonical 2-3 character state/province");
      }
      result.valid = true;
      result.atom = nativeLocationAtom (
        ExchangeRole::Full, LocationKind::StateProvince, configured);
      result.text = u"599 " + configured;
      return result;
    }

    return invalid (u"Unsupported exchange profile");
  }

  NativeExchangeCompilation nativeGridExchange (NativeMacroContext const& context,
                                                ExchangeRole role)
  {
    NativeExchangeCompilation result;
    std::u16string const locator = toUpper (trimmed (context.grid));
    if (!isLocator (locator)) {
      result.error = u"Grid must be a valid four-, six-, or eight-character Maidenhead locator";
      return result;
    }
    std::u16string const grid = locator.substr (0, 4);
    result.valid = true;
    result.atom = nativeGridAtom (role, grid);
    result.text = role == ExchangeRole::Full ? u"599 " + grid : grid;
    return result;
  }

  NativeMacroCompilation compileNativeMacro (std::u16string const& macroTemplate,
                                             NativeMacroContext const& context)
  {
    NativeMacroCompilation result;
    std::u16string const normalized = normalizedMacroTemplate (macroTemplate);
    int const phraseId = controlPhraseId (normalized);
    if (phraseId >= 0) {
      result.status = NativeMacroStatus::Native;
      result.atoms.push_back (nativeControlAtom (phraseId));
      result.text = controlPhrase (phraseId);
      return result;
    }

    NativeForm const form = nativeForm (macroTemplate);
    if (form == NativeForm::None) {
      result.text = expandLiteralMacro (macroTemplate, context);
      return result;
    }

    std::u16string const myCall = normalizedNativeCall (context.myCall);
    std::u16string const hisCall = normalizedNativeCall (context.hisCall);
    auto invalid = [&result] (std::u16string const& error) {
      result.status = NativeMacroStatus::InvalidRuntime;
      result.atoms.clear ();
      result.text.clear ();
      result.error = error;
      return result;
    };
    auto appendCall = [&result] (CallAction action, std::u16string const& call) {
      result.atoms.push_back (nativeCallAtom (action, call));
    };
    auto appendSerial = [&result, &context] {
      result.atoms.push_back (nativeSerialAtom (context.serialNumber));
    };
    auto appendExchange = [&result, &context, &normalized] {
      NativeExchangeCompilation exchange;
      if (normalized.find (u"%G") != std::u16string::npos) {
        ExchangeRole const role = normalized == u"599 %G"
          ? ExchangeRole::Full : ExchangeRole::FieldOnly;
        exchange = nativeGridExchange (context, role);
      } else {
        exchange = nativeExchange (context);
      }
      if (exchange.valid) result.atoms.push_back (exchange.atom);
      return exchange;
    };

    switch (form) {
    case NativeForm::Cq:
      if (!isNativeCall (myCall)) return invalid (u"Configured callsign is not a native Call8 callsign");
      appendCall (CallAction::Cq, myCall);
      result.text = u"CQ " + myCall + u" CQ";
      break;
    case NativeForm::CallExchange:
    case NativeForm::CallSerial:
    case NativeForm::CallGrid:
      if (!isNativeCall (hisCall)) return invalid (u"DX callsign is not a native Call8 callsign");
      appendCall (CallAction::Call, hisCall);
      if (form == NativeForm::CallSerial) {
        if (context.serialNumber < 0 || context.serialNumber >= (1 << 17)) {
          return invalid (u"Serial number must be between 0 and 131071");
        }
        appendSerial ();
        result.text = hisCall + u" 599 " + formatSerialNumber (context.serialNumber);
      } else {
        auto const exchange = appendExchange ();
        if (!exchange.valid) return invalid (exchange.error);
        result.text = hisCall + u' ' + exchange.text;
      }
      break;
    case NativeForm::CallTuCq:
      if (!isNativeCall (hisCall)) return invalid (u"DX callsign is not a native Call8 callsign");
      if (!isNativeCall (myCall)) return invalid (u"Configured callsign is not a native Call8 callsign");
      appendCall (CallAction::CallTu, hisCall);
      appendCall (CallAction::Cq, myCall);
      result.text = hisCall + u" TU CQ " + myCall + u" CQ";
      break;
    case NativeForm::MyCall:
      if (!isNativeCall (myCall)) return invalid (u"Configured callsign is not a native Call8 callsign");
      appendCall (CallAction::Call, myCall);
      result.text = myCall;
      break;
    case NativeForm::HisCall:
      if (!isNativeCall (hisCall)) return invalid (u"DX callsign is not a native Call8 callsign");
      appendCall (CallAction::Call, hisCall);
      result.text = hisCall;
      break;
    case NativeForm::TuNowExchange:
    case NativeForm::TuNowSerial:
    case NativeForm::TuNowGrid:
      if (!isNativeCall (hisCall)) return invalid (u"Queued callsign is not a native Call8 callsign");
      appendCall (CallAction::TuNowCall, hisCall);
      if (form == NativeForm::TuNowSerial) {
        if (context.serialNumber < 0 || context.serialNumber >= (1 << 17)) {
          return invalid (u"Serial number must be between 0 and 131071");
        }
        appendSerial ();
        result.text = u"TU NOW " + hisCall + u" 599 " + formatSerialNumber (context.serialNumber);
      } else {
        auto const exchange = appendExchange ();
        if (!exchange.valid) return invalid (exchange.error);
        result.text = u"TU NOW " + hisCall + u' ' + exchange.text;
      }
      break;
    case NativeForm::CallAgn:
      if (!isNativeCall (hisCall)) return invalid (u"DX callsign is not a native Call8 callsign");
      appendCall (CallAction::CallAgn, hisCall);
      result.text = hisCall + u" AGN?";
      break;
    case NativeForm::Exchange:
    case NativeForm::Serial:
    case NativeForm::Grid:
    case NativeForm::ReportGrid:
      if (form == NativeForm::Serial) {
        if (context.serialNumber < 0 || context.serialNumber >= (1 << 17)) {
          return invalid (u"Serial number must be between 0 and 131071");
        }
        appendSerial ();
        result.text = u"599 " + formatSerialNumber (context.serialNumber);
      } else {
        auto const exchange = appendExchange ();
        if (!exchange.valid) return invalid (exchange.error);
        result.text = exchange.text;
      }
      break;
    default:
      return invalid (u"Unsupported native macro");
    }

    result.status = NativeMacroStatus::Native;
    return result;
  }
}
