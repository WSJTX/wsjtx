// SPDX-License-Identifier: GPL-3.0-or-later
// The transmit encoding's C interface (lib/jtty/JttyTransmitHost.hpp) against
// the encoder it reports (lib/jtty/JttyTransmit.hpp), and its N1MM text
// against WSJT-X's own compiler (widgets/JttyN1mm.hpp).
#include <QtTest>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "lib/jtty/JttyTransmit.hpp"
#include "lib/jtty/JttyTransmitHost.hpp"
#include "widgets/JttyN1mm.hpp"
#include "widgets/JttyTransmitText.hpp"

extern "C" {
  // lib/streaming_jtty_width.f90
  void jtty_message_width_c (int * columns, int * separator, char const frames[], int nframes,
                             int after_pause, int * valid);
}

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
    bool isN1mm {false};            // a TXTEXT from N1MM Logger+
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
    int openColumns {0};            // the receiver's message it continues
    bool openSeparator {false};
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
    std::int32_t openColumns {-1};  // the receiver's message after it
    std::int32_t openSeparator {-1};
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
    result.handle = jtty_tx_encode (r.isN1mm ? 2 : r.isTemplate ? 1 : 0, r.text.data (), std::int32_t (r.text.size ()), r.hisCall.data (),
                                    std::int32_t (r.hisCall.size ()), r.exchange.data (),
                                    std::int32_t (r.exchange.size ()), my.data (), length (r.myCall, r.myCallOverlong), grid.data (),
                                    length (r.grid), r.serial.value_or (0), r.serial.has_value (),
                                    r.report.value_or (0), r.report.has_value (), r.profile, r.isFinal,
                                    r.openColumns, r.openSeparator);
    if (!result.handle) return result;
    std::int32_t segments = -1;
    result.status = jtty_tx_status (result.handle, &segments, &result.substituted);
    char key[32], detail[256];
    std::int32_t keyLength = -1, detailLength = -1;
    jtty_tx_error (result.handle, key, sizeof key, &keyLength, detail, sizeof detail, &detailLength);
    result.key.assign (key, keyLength);
    result.detail.assign (detail, detailLength);
    for (std::int32_t i = 0; i < segments; ++i) result.segments.push_back (segment (result.handle, i));
    jtty_tx_open (result.handle, &result.openColumns, &result.openSeparator);
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

  // The receiver's message after frames (transmitFrameBits each, consecutive)
  // reach it, from columns and separator; -1 when it would drop one.
  int width (std::string const& frames, int& columns, bool& separator, bool afterPause = false)
  {
    int c = columns, s = separator ? 1 : 0, valid = 0;
    jtty_message_width_c (&c, &s, frames.data (), int (frames.size () / Jtty::transmitFrameBits), afterPause, &valid);
    if (!valid) return -1;
    columns = c;
    separator = s != 0;
    return c;
  }

  // The characters text carries, less its spaces.
  std::string unspaced (std::string const& text)
  {
    std::string result;
    for (char const c : text) if (c != ' ' && c != '\n') result += c;
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
    // The second line leaves fewer than 21 of the receiver's 80 columns, so
    // the open request ends its message, as a final one would.
    auto const expected = encodeTransmitText (units (request.text), Jtty::NativeExchangeProfile::None, true);
    QCOMPARE (result.openColumns, 0);
    QCOMPARE (result.openSeparator, 0);
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

    auto const handle = jtty_tx_encode (0, "HI", 2, "", 0, "", 0, "", -1, "", -1, 0, 0, 0, 0, 0, 1, 0, 0);
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

  // Seven of the eight cases #684 measured on decoded audio, which showed
  // which ones a receiver cut: the library's frames for each run of
  // requests, sent back to back as one message, against the widths the
  // receiver's rules give (lib/streaming_jtty_width.f90), over 80 where
  // #684's receiver cut. fits79's text is rebuilt from what it showed; the
  // others are the measured requests. The eighth, David's 73 A then "599 05"
  // under RTTY (85 columns), no longer encodes as one segment since #671, so
  // cuttingRules sends it as two messages. Two widths differ from the first
  // estimates (each still over 80, where the receiver cut): break599 is 89,
  // not 90, since its 123 is a structured serial of three columns, which the
  // estimate counted as a five-character TEXT5; lead599one is 89, not 84,
  // since the library now cuts it after "599 005 " and sends the 76 A in
  // sixteen frames of their own.
  void measuredCasesHaveTheirWidths ()
  {
    struct Case { char const * name; std::vector<std::string> requests; Jtty::NativeExchangeProfile profile; int width; };
    using P = Jtty::NativeExchangeProfile;
    std::string const a (100, 'A');
    std::vector<Case> const cases {
      {"long121", {"THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG AND KEEPS RUNNING ACROSS THE FIELD UNTIL IT REACHES "
                   "THE OLD BARN BY THE RIVER"}, P::None, 125},
      {"break599", {a.substr (0, 76) + " 599 123 HELLO"}, P::RttyRoundup, 89},
      {"lead599two", {"599 5 " + a.substr (0, 64) + " 599 5"}, P::RttyRoundup, 81},
      {"lead599one", {"599 005 " + a.substr (0, 76)}, P::RttyRoundup, 89},
      {"fits79", {a.substr (0, 72) + " 599 005"}, P::RttyRoundup, 80},
      {"altj 4 topups", {"HELLO JOE", "<<<<< <<<<<", "<<<<< <<<<<", "<<<<< <<<<<", "<<<<< <<<<<",
                         "HOW ARE YOU TODAY", "DE K1JT K"}, P::None, 100},
      {"altj 1 topup", {"HELLO JOE", "<<<<< <<<<<", "HOW ARE YOU TODAY", "DE K1JT K"}, P::None, 55},
    };
    for (auto const& c : cases) {
      std::string frames;
      for (std::size_t i = 0; i < c.requests.size (); ++i) {
        auto const encoded = encodeTransmitText (units (c.requests[i]), c.profile, i + 1 == c.requests.size ());
        QVERIFY2 (encoded.status == Jtty::TransmitTextStatus::Encoded, c.name);
        for (std::size_t k = 0; k < encoded.segments.size (); ++k) {
          QVERIFY2 (encoded.segments[k].final == (i + 1 == c.requests.size () && k + 1 == encoded.segments.size ()),
                    c.name);
          frames += joined (encoded.segments[k].frames);
        }
      }
      int columns = 0;
      bool separator = false;
      QVERIFY2 (width (frames, columns, separator) == c.width,
                qPrintable (QString {"%1: width %2"}.arg (c.name).arg (columns)));
    }
  }

  // Runs of requests, each continuing the message the one before leaves
  // open, keep every message within the receiver's 80 columns and send every
  // character; an open request leaves at least 21 columns or ends its
  // message, and no segment but a line's last ends with 599.
  void messagesStayWithinEightyColumns ()
  {
    std::mt19937 random {20261121};
    auto pick = [&random] (int n) {return int (random () % unsigned (n));};
    // 599 comes only before a number: before a word too long to share a
    // segment with it, it cannot stay with its token.
    std::vector<std::string> const words {"CQ", "K1ABC", "W9XYZ", "DE", "005", "123", "16383", "TU",
      "NOW", "5NN", "FN42", "MA", "AGN?", "QSL", "73", "HELLO", "JOE", "<<<<<", "TEST", "RIG", "DIPOLE",
      "ANTENNA", "WEATHER", "THE", "A", "QRZ?", "1A", "EMA", "TU NOW"};
    int messages = 0, cut = 0, chained = 0;
    for (int run = 0; run < 300; ++run) {
      int const requests = 1 + pick (4);
      int columns = 0;
      bool separator = false;
      for (int r = 0; r < requests; ++r) {
        std::string body;
        int const nwords = 1 + pick (40);
        for (int w = 0; w < nwords; ++w) {
          if (!body.empty ()) body += pick (12) == 0 ? "\n" : " ";
          if (pick (25) == 0) {
            body += std::string (std::size_t (20 + pick (75)), char ('B' + pick (20)));   // a long word
          } else {
            body += words[std::size_t (pick (int (words.size ())))];
          }
          if (pick (6) == 0) body += " 599 " + std::to_string (pick (2000));
        }
        auto request = text (body);
        request.profile = pick (3);
        request.isFinal = r + 1 == requests ? pick (2) == 0 : pick (3) == 0;
        request.openColumns = columns;
        request.openSeparator = separator;
        auto const result = encode (request);
        auto const where = QString {"run %1 request %2 (%3, open %4): %5"}.arg (run).arg (r).arg (request.profile)
          .arg (columns).arg (QString::fromStdString (body));
        QVERIFY2 (result.status == std::int32_t (Status::Encoded),
                  qPrintable (where + " " + QString::fromStdString (result.detail)));
        bool const continues = columns != 0 || separator;
        if (continues) ++chained;
        std::string sent;
        std::vector<std::size_t> lineEnds;           // in characters less spaces
        for (std::size_t p = 0;; ++p) {
          p = body.find ('\n', p);
          lineEnds.push_back (unspaced (body.substr (0, p)).size ());
          if (p == std::string::npos) break;
        }
        for (std::size_t k = 0; k < result.segments.size (); ++k) {
          auto const& segment = result.segments[k];
          int const w = width (segment.frames, columns, separator, continues && k == 0);
          QVERIFY2 (w >= 0 && w <= 80, qPrintable (where + QString {": segment %1 width %2"}.arg (k).arg (w)));
          sent += segment.text + " ";
          if (segment.final) {
            columns = 0;
            separator = false;
            ++messages;
          }
          auto const& t = segment.text;
          bool const ends599 = t == "599" || (t.size () >= 4 && t.compare (t.size () - 4, 4, " 599") == 0);
          if (ends599 && std::find (lineEnds.cbegin (), lineEnds.cend (), unspaced (sent).size ()) == lineEnds.cend ()) {
            QFAIL (qPrintable (where + ": a segment within a line ends with 599"));
          }
        }
        if (result.segments.size () > std::size_t (std::count (body.begin (), body.end (), '\n') + 1)) ++cut;
        QVERIFY2 (unspaced (sent) == unspaced (body), qPrintable (where + ": sent " + QString::fromStdString (sent)));
        QCOMPARE (result.openColumns, columns);
        QCOMPARE (result.openSeparator, std::int32_t (separator ? 1 : 0));
        if (request.isFinal) QCOMPARE (columns, 0);
        if (columns != 0) QVERIFY2 (80 - columns >= 21, qPrintable (where));
      }
    }
    QVERIFY (messages > 2000);
    QVERIFY (cut > 350);
    QVERIFY (chained > 175);
  }

  // The plan's rules, each on a case of its own.
  void cuttingRules ()
  {
    std::string const a (100, 'A');
    // 599 stays with its serial: the line ends its message before them.
    auto david = text (a.substr (0, 73) + " 599 05");
    david.profile = 2;
    auto const d = encode (david);
    QCOMPARE (d.segments.size (), std::size_t (2));
    QCOMPARE (d.segments[0].text, a.substr (0, 73));
    QCOMPARE (d.segments[0].final, true);
    QCOMPARE (d.segments[1].canonical, std::string {"599 005"});

    // A text that does not encode at full length (twenty exchanges, more
    // than 16 frames) goes as complete shorter messages.
    std::string pairs;
    for (int i = 1; i <= 20; ++i) pairs += (i > 1 ? " 599 " : "599 ") + std::to_string (i);
    auto serials = text (pairs);
    serials.profile = 2;
    auto const s = encode (serials);
    QCOMPARE (s.status, std::int32_t (Status::Encoded));
    QVERIFY (s.segments.size () >= 2);
    for (auto const& segment : s.segments) QVERIFY (segment.final);

    // An open request that leaves fewer than 21 columns ends its message;
    // one that leaves more keeps it open, and the next continues it.
    auto wide = text (a.substr (0, 60));
    wide.isFinal = false;
    auto const w = encode (wide);
    QCOMPARE (w.segments.back ().final, true);
    QCOMPARE (w.openColumns, 0);
    auto narrow = text (a.substr (0, 50));
    narrow.isFinal = false;
    auto const n = encode (narrow);
    QCOMPARE (n.segments.back ().final, false);
    QCOMPARE (n.openColumns, 50);
    QCOMPARE (n.openSeparator, 0);

    // A message an earlier request left open takes what fits of the next
    // and ends; the rest starts a new message.
    auto next = text ("HELLO THERE MY FRIEND HOW ARE YOU TODAY");
    next.openColumns = 50;
    auto const x = encode (next);
    QVERIFY (x.segments.size () >= 2);
    QCOMPARE (x.segments[0].final, true);
    int columns = 50;
    bool separator = false;
    QVERIFY (width (x.segments[0].frames, columns, separator, true) <= 80);

    // The reserve is exactly 21: an open request may leave 21 columns and no
    // fewer.
    auto left21 = text (a.substr (0, 49) + " 599 16383");
    left21.profile = 2;
    left21.isFinal = false;
    auto const l21 = encode (left21);
    QCOMPARE (l21.openColumns, 59);
    QCOMPARE (l21.openSeparator, 1);
    QCOMPARE (l21.segments.back ().final, false);
    auto left20 = text (a.substr (0, 49) + " 599 131071");
    left20.profile = 2;
    left20.isFinal = false;
    auto const l20 = encode (left20);
    QCOMPARE (l20.openColumns, 0);
    QCOMPARE (l20.segments.back ().final, true);

    // A request continuing a message takes the longest run of words that fits
    // with a pause before it, and ends the message there.
    auto first = text ("THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG");
    first.isFinal = false;
    auto const one = encode (first);
    QCOMPARE (one.openColumns, 45);
    auto second = text ("AND KEEPS RUNNING ACROSS THE FIELD UNTIL IT");
    second.openColumns = one.openColumns;
    second.openSeparator = one.openSeparator;
    auto const two = encode (second);
    QCOMPARE (two.segments.size (), std::size_t (2));
    QCOMPARE (two.segments[0].text, std::string {"AND KEEPS RUNNING ACROSS THE"});
    QCOMPARE (two.segments[0].final, true);
    QCOMPARE (two.segments[1].text, std::string {"FIELD UNTIL IT"});

    // 599 goes with a token too long to share a segment with it, counting
    // the spaces between them, and stays with one that fits.
    auto token = text ("HELLO 599 " + std::string (80, 'B'));
    auto const k = encode (token);
    QCOMPARE (k.segments[0].text, std::string {"HELLO 599"});
    auto const spaced = encode (text ("HELLO 599  " + std::string (76, 'B')));
    QCOMPARE (spaced.segments[0].text, std::string {"HELLO 599"});
    auto const kept = encode (text ("HELLO 599 " + std::string (76, 'B')));
    QCOMPARE (kept.segments[0].text, std::string {"HELLO"});
    QCOMPARE (kept.segments[1].text, "599 " + std::string (76, 'B'));

    // A segment that ends a word on a full five-character frame ends its
    // message, so the next word does not run on to it.
    auto runOn = text ("ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABCDE ABC HELLO WORLD");
    auto const r = encode (runOn);
    QVERIFY (r.segments.size () >= 2);
    QCOMPARE (r.segments[0].final, true);
    QCOMPARE (r.segments[1].text.substr (0, 5), std::string {"HELLO"});
    // So does one cut hard at 80 characters with a space after it; one cut
    // inside a word keeps the message open.
    std::string blocks;
    for (int i = 0; i < 10; ++i) blocks += "ABCD  ";
    auto const hard = encode (text (blocks + "ABCDEFGHIJKLMNOPWXYQ" + " HELLO WORLD"));   // 80 then a space
    std::size_t h = 0;
    while (h < hard.segments.size () && hard.segments[h].text.find ("HELLO") == std::string::npos) ++h;
    QVERIFY (h > 0 && h < hard.segments.size ());
    QCOMPARE (hard.segments[h - 1].final, true);
    QCOMPARE (hard.segments[h].text.substr (0, 5), std::string {"HELLO"});
    auto const inside = encode (text (std::string (80, 'W') + std::string (10, ' ') + std::string (75, 'V')));
    std::string word75;
    for (auto const& segment : inside.segments) {
      if (segment.text.find ('V') == std::string::npos) continue;
      if (!word75.empty ()) QCOMPARE (segment.final, true);
      else QCOMPARE (segment.final, segment.text.size () == 75);
      word75 += segment.text;
    }
    QCOMPARE (word75, std::string (75, 'V'));

    // A word too wide for what an earlier request left is cut where it fits.
    auto word = text (a.substr (0, 40));
    word.openColumns = 59;
    auto const y = encode (word);
    QCOMPARE (y.segments.size (), std::size_t (2));
    QCOMPARE (y.segments[0].text + y.segments[1].text, a.substr (0, 40));

    // A function key that would pass the open message's 80 columns goes as
    // its text; one that fits goes as its atoms and ends the message.
    auto fits = key ("%H %E");
    fits.openColumns = 30;
    auto const f = encode (fits);
    QCOMPARE (f.segments.size (), std::size_t (1));
    QCOMPARE (f.segments[0].final, true);
    auto const atoms = encodeNativeAtoms (compileNativeMacro (u"%H %E", gui ()).atoms);
    QCOMPARE (f.segments[0].frames, joined (atoms.frames));
    auto tight = key ("%H TU CQ %M CQ");
    tight.openColumns = 59;
    auto const t = encode (tight);
    QCOMPARE (t.status, std::int32_t (Status::Encoded));
    auto const native = joined (encodeNativeAtoms (compileNativeMacro (u"%H TU CQ %M CQ", gui ()).atoms).frames);
    columns = 59;
    separator = false;
    QVERIFY (width (native, columns, separator, true) > 80);
    QVERIFY (t.segments[0].frames != native);
    columns = 59;
    separator = false;
    for (std::size_t k = 0; k < t.segments.size (); ++k) {
      QVERIFY (width (t.segments[k].frames, columns, separator, k == 0) <= 80);
      if (t.segments[k].final) columns = 0, separator = false;
    }
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

  // A TXTEXT from N1MM Logger+ is compiled as WSJT-X compiles it: MMTTYIF
  // reads its bytes as Latin-1 and upper-cases them, and handleMmttyTxString
  // refuses a tag that does not compile (the reason cut at a character to
  // the 256 bytes jtty_tx_error is given here), sends a tag's atoms with its
  // canonical text, and sends other text as typed. The codec sends that text
  // as its text requests do; where it is short enough to go whole (all the
  // texts of 60 characters or fewer here), its tones are WSJT-X's
  // (encodeTransmitText, isFinal true). The cases, in every profile, and each
  // Latin-1 character in ten places.
  void n1mmIsCompiledAsWsjtxDoes ()
  {
    std::vector<std::string> cases {
      "[[JTTY:CQ]] K1ABC", "[[jtty:cq]] k1abc", "  [[JTTY:CQ]]   K1ABC  ", "[[JTTY:CQ]] K1ABC W9XYZ",
      "[[JTTY:CQ]] K1ABC/P", "[[JTTY:CQ]] PJ4/K1ABC", "[[JTTY:CQ]] 9A1AA", "[[JTTY:CALL_EXCH]] W9XYZ 7",
      "[[JTTY:CALL_EXCH]] W9XYZ 131071", "[[JTTY:CALL_EXCH]] W9XYZ 131072", "[[JTTY:CALL_EXCH]] W9XYZ 00042",
      "[[JTTY:CALL_EXCH]] W9XYZ 99999999999", "[[JTTY:EXCH]] 4294967301", "[[JTTY:CALL_EXCH]] W9XYZ", "[[JTTY:CALL_EXCH]] W9XYZ DX",
      "[[JTTY:CALL_EXCH]] W9XYZ az", "[[JTTY:CALL_EXCH]] W9XYZ MA", "[[JTTY:CALL_EXCH]] W9XYZ ONT",
      "[[JTTY:CALL_EXCH]] W9XYZ 3A OR", "[[JTTY:CALL_EXCH]] W9XYZ 1DEMA", "[[JTTY:CALL_EXCH]] W9XYZ #",
      "[[JTTY:CALL_EXCH]] W9XYZ/P 7", "[[JTTY:TU_NOW_EXCH]] W9XYZ 42", "[[JTTY:TU_NOW_EXCH]] W9XYZ DX",
      "[[JTTY:TU_NOW_EXCH]] W9XYZ 1D EMA", "[[JTTY:CALL_TU_CQ]] W9XYZ K1ABC", "[[JTTY:CALL_TU_CQ]] W9XYZ",
      "[[JTTY:CALL_TU_CQ]] W9XYZ/P K1ABC", "[[JTTY:CALL_TU_CQ]] VE3ABC 9A1AA", "[[JTTY:MYCALL]] K1ABC",
      "[[JTTY:MYCALL]] K1ABC W9XYZ", "[[JTTY:HISCALL]] W9XYZ", "[[JTTY:HISCALL]] W9XYZ/P",
      "[[JTTY:CALL_MY]] W9XYZ K1ABC", "[[JTTY:CALL_MY]] W9XYZ/P K1ABC", "[[JTTY:CALL_MY]] PJ4/W9XYZ K1ABC",
      "[[JTTY:CALL_MY]] VE3ABC 9A1AA", "[[JTTY:CALL_MY]] W9XYZ", "[[JTTY:CALL_TU_MY]] W9XYZ K1ABC",
      "[[JTTY:CALL_TU_MY]] W9XYZ K1ABC/QRP", "[[JTTY:CALL_TU_MY]] VE3ABC 9A1AA", "[[JTTY:EXCH]] 7",
      "[[JTTY:EXCH]] 00042", "[[JTTY:EXCH]] DX", "[[JTTY:EXCH]] #", "[[JTTY:EXCH]] 1D EMA", "[[JTTY:EXCH]] 1DEMA",
      "[[JTTY:EXCH]] ON", "[[JTTY:GRID]] FN42", "[[JTTY:GRID]] fn42", "[[JTTY:GRID]] RR99", "[[JTTY:GRID]] SA00",
      "[[JTTY:GRID]] FN42AX", "[[JTTY:GRID]] FN42 EM10", "[[JTTY:CONTROL]] THANKS", "[[JTTY:CONTROL]]AGN?",
      "[[JTTY:CONTROL]] AGN  CALL", "[[JTTY:]] K1ABC", "[[JTTY:CQ K1ABC", "[[JTTY CQ]] K1ABC", "[[JTTYCQ]] K1ABC",
      "[[JTTY:C Q]] K1ABC", "[[JTTY:C\tQ]] K1ABC", "[[JTTY:C[Q]] K1ABC", "[[JTTY:FOO]] K1ABC", "[[JTTY:CQ]]",
      "[[JTTY:CQ]]   ", "[[JTTY:CQ]]]] K1ABC", "[[JTTY:CQ]]K1ABC", "CQ TEST K1ABC", "TEST %M", "tu 5nn",
      "", "   ", "HELLO [[JTTY:CQ]] K1ABC", "\xE9t\xE9 \xDF \xB5 \xFF", std::string ("\0~ TILDE", 8),
      "[JTT", "[[JT", "[[JTT", "LINE ONE\nLINE TWO", "W9XYZ 599 007", "[[JTTY:EXCH]] 1D ZZ",
      "DDDD # 599 73 1D 131071 DDDD DX A TEST 1D K1ABC K1ABC 9A1AA AGN? A CCC EEEEE",
      "AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AAAAA AA 599 1",
      "[[JTTY:" + std::string (300, 'A') + "]] K1ABC"};
    std::string accented;
    for (int i = 0; i < 118; ++i) accented += '\xE9';
    cases.push_back ("[[JTTY:" + accented + "]] K1ABC");
    std::string longLine;
    for (int i = 0; i < 12; ++i) longLine += "CQ TEST K1ABC ";
    cases.push_back (longLine);
    for (auto const& phrase : {"AGN?", "CALL?", "AGN CALL", "NR?", "AGN NR", "EXCH?", "STATE?", "SECTION?", "ZONE?",
                               "GRID?", "RPRT?", "QSL TU", "TU", "QRZ?", "QSO B4", "WAIT", "NIL?", "OK?"}) {
      cases.push_back (std::string {"[[JTTY:CONTROL]] "} + phrase);
    }
    std::pair<char const *, char const *> const places[] {
      {"[[JTTY:CQ]] K1A", "C"}, {"[[JTTY:HISCALL", "]] W9XYZ"}, {"", "[[JTTY:CQ]] K1ABC"},
      {"[[JTTY:CALL_EXCH]] W9XYZ", "5"}, {"[[JTTY:EXCH]] 1", "D EMA"}, {"[[JTTY:GRID]] FN4", ""},
      {"[[JTTY:CONTROL]] AGN", ""}, {"AB", "CD"}, {"[[JTTY:CALL_MY]] W9XYZ", "K1ABC"}, {"[[JTT", "Y:CQ]] K1ABC"}};
    for (int c = 0; c < 256; ++c) {
      for (auto const& [before, after] : places) cases.push_back (std::string {before} + char (c) + after);
    }
    auto const toVector = [] (QVector<int> const& tones) {return std::vector<int> (tones.cbegin (), tones.cend ());};
    // A reason's UTF-8, cut at a character to 256 bytes.
    auto const reason = [] (QString const& error) {
      auto bytes = error.toUtf8 ().toStdString ();
      if (bytes.size () > 256) {
        std::size_t n = 256;
        while (n > 0 && (static_cast<unsigned char> (bytes[n]) & 0xC0) == 0x80) --n;
        bytes.resize (n);
      }
      return bytes;
    };
    int native = 0, literal = 0, error = 0, unencoded = 0, cut = 0, whole = 0;
    for (auto const& bytes : cases) {
      for (int profile = 0; profile <= 2; ++profile) {
        auto const message = QString::fromLatin1 (bytes.data (), int (bytes.size ()));
        auto const compiled = Jtty::compileN1mmMessage (message.toUpper (),
                                                        static_cast<Jtty::NativeExchangeProfile> (profile));
        Request request;
        request.isN1mm = true;
        request.text = message.toUtf8 ().toStdString ();
        request.profile = profile;
        auto const result = encode (request);
        auto const what = QString {"%1 (profile %2): %3"}.arg (message).arg (profile)
          .arg (QString::fromStdString (result.detail)).toLocal8Bit ();
        QVERIFY2 (result.handle, what.constData ());
        switch (compiled.status) {
        case Jtty::N1mmCompileStatus::Error:
          ++error;
          QVERIFY2 (result.status == std::int32_t (Status::EncodingFailed) && result.key == "n1mm"
                    && result.detail == reason (compiled.error), what.constData ());
          if (compiled.error.toUtf8 ().size () > 256) ++cut;
          break;
        case Jtty::N1mmCompileStatus::Native: {
          ++native;
          auto const encoded = Jtty::encodeNativeAtoms (compiled.atoms);
          if (encoded.tones.isEmpty ()) {
            ++unencoded;
            QVERIFY2 (result.status == std::int32_t (Status::EncodingFailed) && result.key.empty (), what.constData ());
            break;
          }
          QVERIFY2 (result.status == std::int32_t (Status::Encoded) && result.segments.size () == 1, what.constData ());
          QVERIFY2 (result.segments[0].tones == toVector (encoded.tones), what.constData ());
          QVERIFY2 (result.segments[0].text == compiled.canonicalText.toStdString (), what.constData ());
          break;
        }
        case Jtty::N1mmCompileStatus::Literal: {
          ++literal;
          auto const profileValue = static_cast<Jtty::NativeExchangeProfile> (profile);
          auto const encoded = Jtty::encodeTransmitText (compiled.literalText, profileValue, true);
          // WSJT-X's Lower case box lower-cases it; JTTY sends letters in one case.
          auto const lower = Jtty::encodeTransmitText (compiled.literalText.toLower (), profileValue, true);
          QVERIFY2 (lower.status == encoded.status && lower.segments.size () == encoded.segments.size (),
                    what.constData ());
          for (int i = 0; i < encoded.segments.size () && i < lower.segments.size (); ++i) {
            QVERIFY2 (lower.segments[i].transmit.tones == encoded.segments[i].transmit.tones, what.constData ());
          }
          // As a text request of the same characters, where it has no placeholder.
          if (!compiled.literalText.contains (QLatin1Char {'%'})) {
            auto typed = text (compiled.literalText.toUtf8 ().toStdString ());
            typed.profile = profile;
            auto const asText = encode (typed);
            QVERIFY2 (result.status == asText.status && result.segments.size () == asText.segments.size ()
                      && result.openColumns == asText.openColumns && result.openSeparator == asText.openSeparator,
                      what.constData ());
            for (std::size_t i = 0; i < result.segments.size (); ++i) {
              QVERIFY2 (result.segments[i].tones == asText.segments[i].tones
                        && result.segments[i].final == asText.segments[i].final, what.constData ());
            }
          }
          if (message.size () > 60) break;
          ++whole;
          if (encoded.status == Jtty::TransmitTextStatus::Empty) {
            QVERIFY2 (result.status == std::int32_t (Status::Empty), what.constData ());
            break;
          }
          if (encoded.status == Jtty::TransmitTextStatus::EncodingFailed) {
            QVERIFY2 (result.status == std::int32_t (Status::EncodingFailed), what.constData ());
            break;
          }
          QVERIFY2 (result.status == std::int32_t (Status::Encoded)
                    && result.segments.size () == std::size_t (encoded.segments.size ()), what.constData ());
          for (int i = 0; i < encoded.segments.size (); ++i) {
            QVERIFY2 (result.segments[i].tones == toVector (encoded.segments[i].transmit.tones), what.constData ());
          }
          break;
        }
        }
      }
    }
    QVERIFY2 (native > 300 && literal > 2000 && error > 5000 && unencoded > 0 && cut > 0 && whole > 2000,
              qPrintable (QString {"native %1, literal %2, error %3, unencoded %4, cut %5, whole %6"}
                          .arg (native).arg (literal).arg (error).arg (unencoded).arg (cut).arg (whole)));
    // As many characters as WSJT-X takes bytes in a command, 64 KiB.
    Request spaces;
    spaces.isN1mm = true;
    spaces.text = std::string (65536, ' ');
    QCOMPARE (encode (spaces).status, std::int32_t (Status::Empty));
    spaces.text += ' ';
    auto const overlong = encode (spaces);
    QCOMPARE (overlong.status, std::int32_t (Status::TooLong));
    QCOMPARE (overlong.key, std::string {"n1mm"});
    // N1MM's bytes are characters up to U+00FF.
    Request wide;
    wide.isN1mm = true;
    wide.text = "[[JTTY:CQ]] K1\xC4\x80" "BC";
    auto const refused = encode (wide);
    QCOMPARE (refused.status, std::int32_t (Status::BadRequest));
    QCOMPARE (refused.key, std::string {"n1mm"});
  }
};

QTEST_GUILESS_MAIN (TestJttyTransmitHost);

#include "test_jtty_transmit_host.moc"
