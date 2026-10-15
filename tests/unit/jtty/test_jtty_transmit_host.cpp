// SPDX-License-Identifier: GPL-3.0-or-later
// The transmit encoding's C interface (lib/jtty/JttyTransmitHost.hpp) against
// the encoder it reports (lib/jtty/JttyTransmit.hpp).
#include <QtTest>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lib/jtty/JttyTransmit.hpp"
#include "lib/jtty/JttyTransmitHost.hpp"

namespace
{
  using namespace Jtty::Encoder;
  using Status = TransmitStatus;

  // 200 characters with an em dash, which becomes '#'; cut 78 + 77 + 45.
  std::string const longLine =
    "Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts "
    "into a dipole at 30 feet; the weather is clear and warm, about 25 C. "
    "Hope to work you again on JTTY soon \xE2\x80\x94 73 and good DX!";

  struct Request
  {
    bool isTemplate {false};
    std::string text;
    std::string hisCall {"W9XYZ"};
    std::string exchange;
    std::optional<std::string> myCall {"K1ABC"};
    bool myCallOverlong {false};    // configure's value was longer than 64 characters
    std::optional<std::string> grid {"FN42"};
    std::optional<int> serial {107};
    std::optional<int> report {-7};
    int profile {0};
    bool isFinal {true};
  };

  Request text (std::string const& value)
  {
    Request request;
    request.text = value;
    return request;
  }

  Request key (std::string const& value)
  {
    Request request;
    request.isTemplate = true;
    request.text = value;
    return request;
  }

  struct Segment
  {
    std::vector<int> tones;
    std::string frames;
    std::string text;
    std::string canonical;
    bool final {false};
    bool substituted {false};
  };

  struct Result
  {
    std::int32_t handle {0};
    std::int32_t status {-1};
    std::int32_t substituted {-1};
    std::string key;
    std::string detail;
    std::vector<Segment> segments;
  };

  std::int32_t length (std::optional<std::string> const& value, bool overlong = false)
  {
    return overlong ? -2 : value ? std::int32_t (value->size ()) : -1;
  }

  Segment segment (std::int32_t handle, std::int32_t index)
  {
    std::int32_t tones[Jtty::maxTransmitFrames * Jtty::transmitFrameSymbols];
    char frames[Jtty::maxTransmitFrames * Jtty::transmitFrameBits];
    char text[Jtty::maxTransmitLength];
    char canonical[Jtty::maxTransmitLength];
    std::int32_t nframes, textLength, canonicalLength, isFinal, substituted;
    auto const nsym = jtty_tx_segment (handle, index, tones, frames, &nframes, text, &textLength, canonical,
                                       &canonicalLength, &isFinal, &substituted);
    Segment result;
    if (!nsym) return result;
    result.tones.assign (tones, tones + nsym);
    result.frames.assign (frames, frames + nframes * Jtty::transmitFrameBits);
    result.text.assign (text, textLength);
    result.canonical.assign (canonical, canonicalLength);
    result.final = isFinal != 0;
    result.substituted = substituted != 0;
    return result;
  }

  // Everything the interface reports for request, its handle destroyed.
  Result encode (Request const& r)
  {
    Result result;
    std::string const my = r.myCall.value_or (""), grid = r.grid.value_or ("");
    result.handle = jtty_tx_encode (r.isTemplate, r.text.data (), std::int32_t (r.text.size ()), r.hisCall.data (),
                                    std::int32_t (r.hisCall.size ()), r.exchange.data (),
                                    std::int32_t (r.exchange.size ()), my.data (), length (r.myCall, r.myCallOverlong), grid.data (),
                                    length (r.grid), r.serial.value_or (0), r.serial.has_value (),
                                    r.report.value_or (0), r.report.has_value (), r.profile, r.isFinal);
    if (!result.handle) return result;
    std::int32_t segments = -1;
    result.status = jtty_tx_status (result.handle, &segments, &result.substituted);
    char key[32], detail[256];
    std::int32_t keyLength = -1, detailLength = -1;
    jtty_tx_error (result.handle, key, sizeof key, &keyLength, detail, sizeof detail, &detailLength);
    result.key.assign (key, keyLength);
    result.detail.assign (detail, detailLength);
    for (std::int32_t i = 0; i < segments; ++i) result.segments.push_back (segment (result.handle, i));
    jtty_tx_destroy (result.handle);
    return result;
  }

  std::u16string units (std::string const& ascii)
  {
    return {ascii.cbegin (), ascii.cend ()};
  }

  std::string ascii (std::u16string const& text)
  {
    return {text.cbegin (), text.cend ()};
  }

  std::string joined (std::vector<std::string> const& frames)
  {
    std::string result;
    for (auto const& frame : frames) result += frame;
    return result;
  }

  NativeMacroContext gui ()
  {
    return nativeMacroContext (u"K1ABC", u"W9XYZ", 107, u"FN42", -7, Jtty::NativeExchangeProfile::None, {});
  }
}

#define EXPECT_REFUSED(result, status_, key_)                                  \
  do {                                                                          \
    auto const r_ = (result);                                                   \
    QCOMPARE (r_.status, std::int32_t (status_));                               \
    QCOMPARE (r_.key, std::string {key_});                                      \
    QVERIFY (!r_.detail.empty ());                                              \
    QVERIFY (r_.segments.empty ());                                             \
  } while (false)

#define EXPECT_CANONICAL(result, canonical_)                                    \
  do {                                                                          \
    auto const r_ = (result);                                                   \
    QCOMPARE (r_.status, std::int32_t (Status::Encoded));                       \
    QCOMPARE (r_.segments.size (), std::size_t (1));                            \
    QCOMPARE (r_.segments[0].canonical, std::string {canonical_});              \
  } while (false)

class TestJttyTransmitHost final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void textIsReportedAsEncoded ()
  {
    auto request = text ("cq k1abc cq\nthanks for the qso, bob. rig here is a home-brew transceiver running 5 watts");
    request.isFinal = false;
    auto const result = encode (request);
    QCOMPARE (result.status, std::int32_t (Status::Encoded));
    QCOMPARE (result.substituted, 0);
    auto const expected = encodeTransmitText (units (request.text), Jtty::NativeExchangeProfile::None, false);
    QCOMPARE (result.segments.size (), expected.segments.size ());
    QVERIFY (result.segments.size () >= 2);
    for (std::size_t i = 0; i < expected.segments.size (); ++i) {
      auto const& want = expected.segments[i];
      auto const& got = result.segments[i];
      QCOMPARE (got.tones, want.transmit.tones);
      QCOMPARE (got.frames, joined (want.frames));
      QCOMPARE (got.text, ascii (trimmed (want.source.text)));
      QCOMPARE (got.canonical, ascii (want.transmit.text));
      QCOMPARE (got.final, want.final);
      QCOMPARE (got.substituted, false);
    }

    auto const handle = jtty_tx_encode (0, "HI", 2, "", 0, "", 0, "", -1, "", -1, 0, 0, 0, 0, 0, 1);
    QVERIFY (handle > 0);
    QCOMPARE (segment (handle, 0).canonical, std::string {"HI"});
    QVERIFY (segment (handle, 1).tones.empty ());
    QVERIFY (segment (handle, -1).tones.empty ());
    jtty_tx_destroy (handle);
    QVERIFY (segment (handle, 0).tones.empty ());
    std::int32_t segments = -1, substituted = -1;
    QCOMPARE (jtty_tx_status (handle, &segments, &substituted), std::int32_t (Status::EncodingFailed));

    EXPECT_REFUSED (encode (text ("~ ~")), Status::Empty, "");
    EXPECT_REFUSED (encode (key ("  ")), Status::Empty, "");
    auto const spaces = encode (key ("\t\xC2\xA0\xE3\x80\x80"));     // tab, U+00A0, U+3000
    EXPECT_REFUSED (spaces, Status::Empty, "");
    QCOMPARE (spaces.detail, std::string {"the template has nothing to send"});
  }

  void templatesAreSentAsFunctionKeys ()
  {
    auto const native = encode (key ("%H %E"));
    EXPECT_CANONICAL (native, "W9XYZ 599 107");
    auto const compiled = compileNativeMacro (u"%H %E", gui ());
    QVERIFY (compiled.isNative ());
    auto const atoms = encodeNativeAtoms (compiled.atoms);
    QCOMPARE (native.segments[0].tones, atoms.tones);
    QCOMPARE (native.segments[0].frames, joined (atoms.frames));
    QCOMPARE (native.segments[0].text, std::string {"W9XYZ 599 107"});
    QCOMPARE (native.segments[0].final, true);

    auto literal = key ("%H de %M");
    literal.isFinal = false;
    auto const sent = encode (literal);
    EXPECT_CANONICAL (sent, "W9XYZ DE K1ABC");
    QCOMPARE (sent.segments[0].final, true);
    QCOMPARE (sent.segments[0].tones,
              encodeTransmitText (u"W9XYZ de K1ABC", Jtty::NativeExchangeProfile::None, true).segments[0].transmit.tones);

    auto invalid = key ("%H");
    invalid.hisCall = "Q1";
    auto const refused = encode (invalid);
    EXPECT_REFUSED (refused, Status::InvalidRuntime, "");
    QCOMPARE (refused.detail, std::string {"DX callsign is not a native Call8 callsign"});
  }

  void renderIsTheEncodersAudio ()
  {
    auto const encoded = encodeTransmitText (u"CQ K1ABC CQ", Jtty::NativeExchangeProfile::None, true);
    auto const& tones = encoded.segments.at (0).transmit.tones;
    std::vector<std::int32_t> symbols (tones.cbegin (), tones.cend ());
    for (int const rate : {12000, 48000}) {
      auto const expected = renderTransmitTones (tones.data (), int (tones.size ()), rate, 1234.5f);
      std::vector<std::int16_t> samples (expected.size ());
      QCOMPARE (jtty_tx_render (symbols.data (), std::int32_t (symbols.size ()), rate, 1234.5f, samples.data (),
                                std::int32_t (samples.size ())),
                std::int32_t (expected.size ()));
      QVERIFY (samples == expected);
      QCOMPARE (jtty_tx_render (symbols.data (), std::int32_t (symbols.size ()), rate, 1234.5f, samples.data (),
                                std::int32_t (samples.size ()) - 1), 0);
      QCOMPARE (jtty_tx_render (symbols.data (), std::int32_t (symbols.size ()), rate, 1234.5f, samples.data (), -1), 0);
    }
    QCOMPARE (jtty_tx_render (symbols.data (), 0, 12000, 1500.f, nullptr, 0), 0);
  }

  void lengthsAreBounded ()
  {
    QCOMPARE (encode (text (std::string (32767, 'A'))).segments.size (), std::size_t (410));
    EXPECT_REFUSED (encode (text (std::string (32768, 'A'))), Status::TooLong, "text");
    QCOMPARE (encode (key (std::string (32767, 'A'))).segments.size (), std::size_t (410));
    EXPECT_REFUSED (encode (key (std::string (32768, 'A'))), Status::TooLong, "template");

    auto expanded = text ("");
    for (int i = 0; i < 16383; ++i) expanded.text += "%H";
    expanded.text += "X";
    expanded.hisCall = std::string (64, 'W');
    auto const longest = encode (expanded);          // 1,048,513 units expanded
    QCOMPARE (longest.status, std::int32_t (Status::Encoded));
    QCOMPARE (longest.segments.size (), std::size_t (13107));
    // Each %%%M expands three times: to %%H..., %Q..., then 190 units.
    for (bool const isTemplate : {false, true}) {
      auto chain = text ("");
      chain.isTemplate = isTemplate;
      for (int i = 0; i < 5519; ++i) chain.text += "%%%M";
      chain.myCall = "H" + std::string (63, 'C');
      chain.hisCall = "Q" + std::string (63, 'B');
      EXPECT_REFUSED (encode (chain), Status::TooLong, isTemplate ? "template" : "text");
    }

    auto call = text ("%H");
    call.hisCall = std::string (64, 'W');
    EXPECT_CANONICAL (encode (call), std::string (64, 'W'));
    call.hisCall = std::string (65, 'W');
    EXPECT_REFUSED (encode (call), Status::BadRequest, "his_call");
    for (auto const& exchange : std::vector<std::string> {"C%A", "CA\n", "C\tA", "C\x1F" "A", "C\x7F" "A",
                                                          "C\xC2\x85" "A", "C\xC2\x9F" "A", "CA\r",
                                                          std::string {"CA"} + '\0'}) {
      auto request = text ("%E");
      request.profile = 2;
      request.exchange = exchange;
      EXPECT_REFUSED (encode (request), Status::BadRequest, "exchange");
    }
    auto percent = text ("HI");
    percent.hisCall = "W9%H";
    EXPECT_REFUSED (encode (percent), Status::BadRequest, "his_call");
    auto del = text ("HI");
    del.hisCall = "W9\x7F";                          // DEL, a control character
    EXPECT_REFUSED (encode (del), Status::BadRequest, "his_call");
    del.hisCall = "W9\xC2\xA0";                      // U+00A0 follows the C1 controls
    EXPECT_CANONICAL (encode (del), "HI");
    auto profile = text ("HI");
    profile.profile = 3;
    EXPECT_REFUSED (encode (profile), Status::BadRequest, "profile");
  }

  // A request needs the station's call or grid only where its encoding
  // reads one.
  void stationValuesAreNeededWhereRead ()
  {
    auto unset = [] (Request request) {
      request.myCall.reset ();
      request.grid.reset ();
      return request;
    };
    EXPECT_REFUSED (encode (unset (text ("%M"))), Status::NotConfigured, "mycall");
    EXPECT_REFUSED (encode (unset (text ("%%M"))), Status::NotConfigured, "mycall");
    EXPECT_REFUSED (encode (unset (text ("cq %M %G"))), Status::NotConfigured, "mycall");
    EXPECT_REFUSED (encode (unset (text ("%G"))), Status::NotConfigured, "mygrid");
    auto chained = unset (text ("%%H"));
    chained.hisCall = "G4ABC";                       // %%H becomes %G4ABC
    EXPECT_REFUSED (encode (chained), Status::NotConfigured, "mygrid");
    chained.hisCall = "W4ABC";
    EXPECT_CANONICAL (encode (chained), "%W4ABC");
    EXPECT_CANONICAL (encode (unset (text ("cq %m"))), "CQ %M");
    EXPECT_CANONICAL (encode (unset (text ("CQ CQ"))), "CQ CQ");
    EXPECT_REFUSED (encode (unset (key ("CQ %M CQ"))), Status::NotConfigured, "mycall");
    EXPECT_REFUSED (encode (unset (key ("cq  %m cq"))), Status::NotConfigured, "mycall");
    EXPECT_REFUSED (encode (unset (key ("%H TU CQ %M CQ"))), Status::NotConfigured, "mycall");
    EXPECT_REFUSED (encode (unset (key ("%H %G"))), Status::NotConfigured, "mygrid");
    EXPECT_REFUSED (encode (unset (key ("599 %G"))), Status::NotConfigured, "mygrid");
    EXPECT_REFUSED (encode (unset (key ("%H %M %G"))), Status::NotConfigured, "mycall");
    EXPECT_CANONICAL (encode (unset (key ("GRID?"))), "GRID?");
    EXPECT_CANONICAL (encode (unset (key ("%H %E"))), "W9XYZ 599 107");
    auto grid = text ("%M %G");
    grid.grid.reset ();
    EXPECT_REFUSED (encode (grid), Status::NotConfigured, "mygrid");
  }

  // Usable: printable ASCII, not blank, no '%'. Configure cuts a value to
  // its first 12 or 6 bytes, which can split a UTF-8 sequence.
  void stationValuesAreUsableOrNot ()
  {
    for (std::string const call : {"", "  ", "K1%H", "K1\tA", "K1ABCDEFGHI\xC3", "K1\x7F", "K1\xC3\xA9"}) {
      auto request = text ("%M");
      request.myCall = call;
      auto const refused = encode (request);
      EXPECT_REFUSED (refused, Status::NotConfigured, "mycall");
      QCOMPARE (refused.detail, std::string {"the request uses mycall, and the configured mycall is not a usable callsign"});
      request.text = "HI";
      EXPECT_CANONICAL (encode (request), "HI");
    }
    auto request = key ("%G");
    request.grid = "  ";
    EXPECT_REFUSED (encode (request), Status::NotConfigured, "mygrid");
    request.grid = "FN42\xC3";
    auto const grid = encode (request);
    EXPECT_REFUSED (grid, Status::NotConfigured, "mygrid");
    QCOMPARE (grid.detail, std::string {"the request uses mygrid, and the configured mygrid is not a usable grid"});
    request.grid.reset ();
    QCOMPARE (encode (request).detail, std::string {"the request uses mygrid, which no configure has set"});
    EXPECT_CANONICAL (encode (text ("%M")), "K1ABC");
    // The transmitter takes the whole configured call, up to 64 characters.
    auto compound = text ("DE %M");
    compound.myCall = "VE3/KJ5HST/MM";
    EXPECT_CANONICAL (encode (compound), "DE VE3/KJ5HST/MM");
    compound.myCall = std::string (64, 'W');
    EXPECT_CANONICAL (encode (compound), "DE " + std::string (64, 'W'));
    compound.myCall = std::string (65, 'W');
    EXPECT_REFUSED (encode (compound), Status::NotConfigured, "mycall");
    auto overlong = text ("%M");
    overlong.myCallOverlong = true;
    auto const refused = encode (overlong);
    EXPECT_REFUSED (refused, Status::NotConfigured, "mycall");
    QCOMPARE (refused.detail, std::string {"the request uses mycall, and the configured mycall is longer than 64 characters"});
    overlong.text = "CQ CQ";
    EXPECT_CANONICAL (encode (overlong), "CQ CQ");
    auto spaced = text ("%M");
    spaced.myCall = "K1ABC  ";
    EXPECT_CANONICAL (encode (spaced), "K1ABC");
  }

  // serial and report are read or refused, never defaulted.
  void serialAndReportAreNeededWhereRead ()
  {
    auto omitted = [] (Request request) {
      request.serial.reset ();
      request.report.reset ();
      return request;
    };
    EXPECT_REFUSED (encode (omitted (text ("%N"))), Status::Missing, "serial");
    EXPECT_REFUSED (encode (omitted (text ("%R"))), Status::Missing, "report");
    EXPECT_REFUSED (encode (omitted (text ("599 %E"))), Status::Missing, "serial");
    EXPECT_REFUSED (encode (omitted (text ("%%N"))), Status::Missing, "serial");
    EXPECT_REFUSED (encode (omitted (text ("%N %R"))), Status::Missing, "serial");
    auto report = omitted (text ("%N %R"));
    report.serial = 5;
    EXPECT_REFUSED (encode (report), Status::Missing, "report");
    EXPECT_CANONICAL (encode (omitted (text ("cq %n"))), "CQ %N");
    EXPECT_CANONICAL (encode (omitted (text ("CQ CQ"))), "CQ CQ");
    EXPECT_REFUSED (encode (omitted (key ("%H 599 %N"))), Status::Missing, "serial");
    EXPECT_REFUSED (encode (omitted (key ("%H %E"))), Status::Missing, "serial");
    EXPECT_REFUSED (encode (omitted (key ("TU NOW %Q 599 %N"))), Status::Missing, "serial");
    auto rtty = omitted (key ("%H %E"));
    rtty.profile = 2;
    rtty.exchange = "DX";
    EXPECT_REFUSED (encode (rtty), Status::Missing, "serial");
    rtty.exchange = "MA";
    EXPECT_CANONICAL (encode (rtty), "W9XYZ 599 MA");
    auto fieldDay = omitted (key ("%H %E"));
    fieldDay.profile = 1;
    fieldDay.exchange = "1D EMA";
    EXPECT_CANONICAL (encode (fieldDay), "W9XYZ 1D EMA");
    auto invalid = omitted (key ("%H %E"));
    invalid.hisCall = "Q1";
    EXPECT_REFUSED (encode (invalid), Status::InvalidRuntime, "");
  }

  void segmentsReportTheirReplacements ()
  {
    auto const result = encode (text (longLine));
    QCOMPARE (result.status, std::int32_t (Status::Encoded));
    QCOMPARE (result.substituted, 1);
    QCOMPARE (result.segments.size (), std::size_t (3));
    QCOMPARE (result.segments[0].substituted, false);
    QCOMPARE (result.segments[1].substituted, false);
    QCOMPARE (result.segments[2].substituted, true);
    QCOMPARE (result.segments[0].text,
              std::string {"Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts"});
    QCOMPARE (result.segments[2].text, std::string {"work you again on JTTY soon # 73 and good DX!"});
    QCOMPARE (result.segments[2].canonical, std::string {"WORK YOU AGAIN ON JTTY SOON # 73 AND GOOD DX!"});
  }

  // A JSON string's leading U+FEFF is a character.
  void leadingByteOrderMarkIsACharacter ()
  {
    auto const result = encode (text ("\xEF\xBB\xBFHI"));
    EXPECT_CANONICAL (result, "#HI");
    QCOMPARE (result.substituted, 1);
    QCOMPARE (result.segments[0].substituted, true);
  }
};

QTEST_GUILESS_MAIN (TestJttyTransmitHost);

#include "test_jtty_transmit_host.moc"
