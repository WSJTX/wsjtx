// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include <vector>

#include "widgets/JttyTransmitText.hpp"

namespace
{
  QString const longLine = QString::fromUtf8 (
    "Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts "
    "into a dipole at 30 feet; the weather is clear and warm, about 25 C. "
    "Hope to work you again on JTTY soon \u2014 73 and good DX!");

  QString serialPairs ()
  {
    QStringList pairs;
    for (int i = 1; i <= 20; ++i) pairs << QStringLiteral ("599 %1").arg (i);
    return pairs.join (QLatin1Char {' '});
  }

  struct Segment
  {
    int offset;
    QString source;
    QString text;
    QVector<int> frameCharStarts;
    bool final;
    int symbols;
  };

  struct Row
  {
    char const * name;
    QString message;
    Jtty::NativeExchangeProfile profile;
    bool isFinal;
    Jtty::TransmitTextStatus status;
    bool substituted;
    QVector<Segment> segments;
  };

  auto constexpr None = Jtty::NativeExchangeProfile::None;
  auto constexpr FieldDay = Jtty::NativeExchangeProfile::FieldDay;
  auto constexpr RttyRoundup = Jtty::NativeExchangeProfile::RttyRoundup;
  auto constexpr Encoded = Jtty::TransmitTextStatus::Encoded;
  auto constexpr Empty = Jtty::TransmitTextStatus::Empty;
  auto constexpr EncodingFailed = Jtty::TransmitTextStatus::EncodingFailed;

  // The tones genjtty_text_c sends for a segment's canonical text.
  QVector<int> textTones (QString const& text, Jtty::NativeExchangeProfile profile, bool final)
  {
    auto frame = Jtty::transmitFrame (text).toLatin1 ();
    QVector<int> tones (Jtty::maxTransmitFrames * Jtty::transmitFrameSymbols);
    char frames[Jtty::maxTransmitFrames * Jtty::transmitFrameBits];
    int frameStarts[Jtty::maxTransmitFrames] = {};
    int nsym = 0;
    int nframes = 0;
    int status = -1;
    genjtty_text_c (frame.data (), static_cast<int> (profile), final ? 1 : 0, tones.data (), &nsym,
                    frames, &nframes, frameStarts, &status);
    tones.resize (status == 0 ? nsym : 0);
    return tones;
  }

  QVector<int> const fullFrameStarts {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75};

  // Each message's segments as the transmitter queues them: the source span,
  // text, frame starts, end of message and tone count.
  QVector<Row> const& rows ()
  {
    static QVector<Row> const table {
      {"one segment", "cq k1abc cq", None, true, Encoded, false, {
          {0, "cq k1abc cq", "CQ K1ABC CQ", {0}, true, 59}}},
      {"one segment not final", "cq k1abc cq", None, false, Encoded, false, {
          {0, "cq k1abc cq", "CQ K1ABC CQ", {0}, false, 59}}},
      {"long line", longLine, None, true, Encoded, true, {
          {0, "Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts ",
           "THANKS FOR THE QSO, BOB. RIG HERE IS A HOME-BREW TRANSCEIVER RUNNING 5 WATTS",
           fullFrameStarts, false, 944},
          {78, "into a dipole at 30 feet; the weather is clear and warm, about 25 C. Hope to ",
           "INTO A DIPOLE AT 30 FEET; THE WEATHER IS CLEAR AND WARM, ABOUT 25 C. HOPE TO",
           fullFrameStarts, false, 944},
          {155, "work you again on JTTY soon # 73 and good DX!",
           "WORK YOU AGAIN ON JTTY SOON # 73 AND GOOD DX!",
           {0, 5, 10, 15, 20, 25, 30, 35, 40}, true, 531}}},
      {"long line not final", longLine, None, false, Encoded, true, {
          {0, "Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts ",
           "THANKS FOR THE QSO, BOB. RIG HERE IS A HOME-BREW TRANSCEIVER RUNNING 5 WATTS",
           fullFrameStarts, false, 944},
          {78, "into a dipole at 30 feet; the weather is clear and warm, about 25 C. Hope to ",
           "INTO A DIPOLE AT 30 FEET; THE WEATHER IS CLEAR AND WARM, ABOUT 25 C. HOPE TO",
           fullFrameStarts, false, 944},
          {155, "work you again on JTTY soon # 73 and good DX!",
           "WORK YOU AGAIN ON JTTY SOON # 73 AND GOOD DX!",
           {0, 5, 10, 15, 20, 25, 30, 35, 40}, false, 531}}},
      {"two lines", "HELLO\nWORLD", None, true, Encoded, false, {
          {0, "HELLO", "HELLO", {0}, true, 59},
          {6, "WORLD", "WORLD", {0}, true, 59}}},
      {"two lines not final", "HELLO\nWORLD", None, false, Encoded, false, {
          {0, "HELLO", "HELLO", {0}, true, 59},
          {6, "WORLD", "WORLD", {0}, false, 59}}},
      {"empty line between", "A\n\nB", None, false, Encoded, false, {
          {0, "A", "A", {0}, true, 59},
          {3, "B", "B", {0}, false, 59}}},
      {"trailing newline", "HELLO\n", None, false, Encoded, false, {
          {0, "HELLO", "HELLO", {0}, true, 59}}},
      {"leading newline", "\nHELLO", None, false, Encoded, false, {
          {1, "HELLO", "HELLO", {0}, false, 59}}},
      {"carriage return", "HELLO\r\nWORLD", None, false, Encoded, false, {
          {0, "HELLO", "HELLO", {0}, true, 59},
          {7, "WORLD", "WORLD", {0}, false, 59}}},
      {"two long lines", longLine + "\n" + longLine, None, false, Encoded, true, {
          {0, "Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts ",
           "THANKS FOR THE QSO, BOB. RIG HERE IS A HOME-BREW TRANSCEIVER RUNNING 5 WATTS",
           fullFrameStarts, false, 944},
          {78, "into a dipole at 30 feet; the weather is clear and warm, about 25 C. Hope to ",
           "INTO A DIPOLE AT 30 FEET; THE WEATHER IS CLEAR AND WARM, ABOUT 25 C. HOPE TO",
           fullFrameStarts, false, 944},
          {155, "work you again on JTTY soon # 73 and good DX!",
           "WORK YOU AGAIN ON JTTY SOON # 73 AND GOOD DX!",
           {0, 5, 10, 15, 20, 25, 30, 35, 40}, true, 531},
          {201, "Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts ",
           "THANKS FOR THE QSO, BOB. RIG HERE IS A HOME-BREW TRANSCEIVER RUNNING 5 WATTS",
           fullFrameStarts, false, 944},
          {279, "into a dipole at 30 feet; the weather is clear and warm, about 25 C. Hope to ",
           "INTO A DIPOLE AT 30 FEET; THE WEATHER IS CLEAR AND WARM, ABOUT 25 C. HOPE TO",
           fullFrameStarts, false, 944},
          {356, "work you again on JTTY soon # 73 and good DX!",
           "WORK YOU AGAIN ON JTTY SOON # 73 AND GOOD DX!",
           {0, 5, 10, 15, 20, 25, 30, 35, 40}, false, 531}}},
      {"word longer than a segment", QString (85, 'X') + " Y", None, true, Encoded, false, {
          {0, QString (80, 'X'), QString (80, 'X'), fullFrameStarts, false, 944},
          {80, "XXXXX Y", "XXXXX Y", {0, 5}, true, 118}}},
      {"spaces before a word longer than a segment", "  " + QString (85, 'X'), None, true, Encoded, false, {
          {0, "  " + QString (78, 'X'), QString (78, 'X'), fullFrameStarts, false, 944},
          {80, "XXXXXXX", "XXXXXXX", {0, 5}, true, 118}}},
      {"segment that does not encode", serialPairs (), RttyRoundup, true, EncodingFailed, false, {}},
      {"segment that does not encode not final", serialPairs (), RttyRoundup, false, EncodingFailed, false,
       {}},
      {"rtty serial that does not fit", QString (73, 'A') + " 599 05", RttyRoundup, true, EncodingFailed,
       false, {}},
      {"later segment that does not encode", QString (79, 'X') + " " + QString (73, 'A') + " 599 05",
       RttyRoundup, true, EncodingFailed, false, {
          {0, QString (79, 'X') + " ", QString (79, 'X'), fullFrameStarts, false, 944}}},
      {"field day", "1D EMA", FieldDay, true, Encoded, false, {
          {0, "1D EMA", "1D EMA", {0}, true, 59}}},
      {"rtty serial", "599 05", RttyRoundup, true, Encoded, false, {
          {0, "599 05", "599 005", {0}, true, 59}}},
      {"spaces", "  leading and  trailing  ", None, true, Encoded, false, {
          {0, "  leading and  trailing  ", "LEADING AND TRAILING", {0, 5, 10, 15}, true, 236}}},
      {"quotes", "HE SAID \"73\" OK", None, true, Encoded, false, {
          {0, "HE SAID \"73\" OK", "HE SAID \"73\" OK", {0, 5, 10}, true, 177}}},
      {"substituted", QString::fromUtf8 ("\u00e9t\u00e9 stra\u00dfe"), None, true, Encoded, true, {
          {0, "#t# stra#e", "#T# STRA#E", {0, 5}, true, 118}}},
      {"blank run substituted", "HELLO" + QString (75, ' ') + QString (80, '~') + " WORLD", None, true,
       Encoded, true, {
          {0, "HELLO" + QString (75, ' '), "HELLO", {0}, false, 59},
          {160, " WORLD", "WORLD", {0}, true, 59}}},
      {"blank", "   ", None, true, Empty, false, {}},
      {"tildes", "~ ~", None, true, Empty, false, {}},
      {"newlines only", "\n\n", None, true, Empty, false, {}},
      {"invalid profile", "CQ K1ABC CQ", static_cast<Jtty::NativeExchangeProfile> (3), true,
       EncodingFailed, false, {}},
    };
    return table;
  }
}

class TestJttyTransmitText final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void encodesAsTheTransmitter_data ()
  {
    QTest::addColumn<int> ("row");
    for (int i = 0; i < rows ().size (); ++i) QTest::newRow (rows ()[i].name) << i;
  }

  void encodesAsTheTransmitter ()
  {
    QFETCH (int, row);
    auto const& expected = rows ()[row];
    auto const encoded = Jtty::encodeTransmitText (expected.message, expected.profile, expected.isFinal);
    auto const library = Jtty::Encoder::encodeTransmitText (Jtty::toEncoder (expected.message),
                                                            expected.profile, expected.isFinal);
    QCOMPARE (encoded.status, expected.status);
    QCOMPARE (int (library.segments.size ()), encoded.segments.size ());
    if (encoded.status == Encoded) QCOMPARE (encoded.substituted, expected.substituted);
    QCOMPARE (encoded.segments.size (), expected.segments.size ());
    for (int i = 0; i < encoded.segments.size (); ++i) {
      auto const& segment = encoded.segments[i];
      auto const& want = expected.segments[i];
      QCOMPARE (segment.source.offset, want.offset);
      QCOMPARE (segment.source.length, want.source.size ());
      QCOMPARE (segment.source.text, want.source);
      QCOMPARE (segment.transmit.text, want.text);
      QCOMPARE (segment.transmit.frameCharStarts, want.frameCharStarts);
      QCOMPARE (segment.final, want.final);
      QCOMPARE (segment.transmit.tones.size (), want.symbols);
      QCOMPARE (segment.transmit.tones, textTones (want.text, expected.profile, want.final));
      auto const& frames = library.segments[i].frames;
      QCOMPARE (int (frames.size ()) * Jtty::transmitFrameSymbols, want.symbols);
      for (std::size_t j = 0; j < frames.size (); ++j) {
        bool const endsMessage = segment.final && j + 1 == frames.size ();
        QCOMPARE (int (frames[j].size ()), Jtty::transmitFrameBits);
        QCOMPARE (frames[j].back (), endsMessage ? '1' : '0');
      }
    }
  }

  // A segment's source is its characters of the prepared text.
  void sourcesIndexThePreparedText ()
  {
    QString const message = longLine + "\r\n" + longLine;
    auto const prepared = Jtty::prepareTransmitText (message).text;
    auto const encoded = Jtty::encodeTransmitText (message, None, true);
    QCOMPARE (encoded.segments.size (), 6);
    for (auto const& segment : encoded.segments) {
      QCOMPARE (prepared.mid (segment.source.offset, segment.source.length), segment.source.text);
    }
  }

  // The audio of tones as the transmitter rendered it: gen_jttywave, scaled
  // by 32767, clamped and rounded.
  void rendersAsTheTransmitter_data ()
  {
    QTest::addColumn<int> ("rate");
    QTest::addColumn<float> ("f0");
    QTest::newRow ("48 kHz") << 48000 << 1500.f;
    QTest::newRow ("12 kHz") << 12000 << 1234.5f;
  }

  void rendersAsTheTransmitter ()
  {
    QFETCH (int, rate);
    QFETCH (float, f0);
    auto const encoded = Jtty::encodeTransmitText ("cq k1abc cq 599 fn42", None, true);
    QCOMPARE (encoded.segments.size (), 1);
    auto tones = encoded.segments[0].transmit.tones;

    int nsym = tones.size ();
    int nsps = 384 * rate / 12000;
    float bt = 2.0;
    float fsample = rate;
    float frequency = f0;
    int icmplx = 0;
    int nwave = nsps * nsym;
    std::vector<float> wave (nwave);
    gen_jttywave_ (tones.data (), &nsym, &nsps, &bt, &fsample, &frequency, wave.data (), wave.data (),
                   &icmplx, &nwave);
    QVector<qint16> expected;
    for (int i = 0; i < nwave; ++i) {
      float v = wave[i] * 32767.0f;
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32768.0f) v = -32768.0f;
      expected.append (static_cast<qint16> (qRound (v)));
    }

    auto const audio = Jtty::renderTransmitTones (tones.constData (), tones.size (), rate, f0);
    QCOMPARE (audio.size (), tones.size () * nsps);
    QVERIFY (audio == expected);
  }

  void macroContextTakesTheProfilesExchange ()
  {
    auto const fieldDay = Jtty::nativeMacroContext ("K1ABC", "W9XYZ", 7, "FN42", -3, FieldDay, "1d  ema");
    QCOMPARE (fieldDay.myCall, QString {"K1ABC"});
    QCOMPARE (fieldDay.hisCall, QString {"W9XYZ"});
    QCOMPARE (fieldDay.serialNumber, 7);
    QCOMPARE (fieldDay.grid, QString {"FN42"});
    QCOMPARE (fieldDay.snr, -3);
    QCOMPARE (fieldDay.exchangeProfile, FieldDay);
    QCOMPARE (fieldDay.configuredExchange, QString {"1D EMA"});
    QCOMPARE (Jtty::nativeMacroContext ({}, {}, 1, {}, -10, FieldDay, "32a ema").configuredExchange,
              QString {"32A EMA"});
    QCOMPARE (Jtty::nativeMacroContext ({}, {}, 1, {}, -10, RttyRoundup, "ca").configuredExchange,
              QString {"ca"});
    QCOMPARE (Jtty::nativeMacroContext ({}, {}, 1, {}, -10, None, "CA").configuredExchange, QString {});
  }

  void encodesNativeAtoms ()
  {
    auto const context = Jtty::nativeMacroContext ("K1ABC", "W9XYZ", 107, "FN42", -10, None, {});
    auto const compiled = Jtty::compileNativeMacro (Jtty::nativeMacroTemplate (2), context);
    QVERIFY (compiled.isNative ());
    auto const encoded = Jtty::encodeNativeAtoms (compiled.atoms);
    QCOMPARE (encoded.tones.size (), 2 * Jtty::transmitFrameSymbols);
    QVERIFY (encoded.error.isEmpty ());

    auto const unknownSection = Jtty::Encoder::nativeClassSectionAtom (1, u'D', u"ZZZ");
    auto const unknown = Jtty::encodeNativeAtoms ({unknownSection});
    QVERIFY (unknown.tones.isEmpty ());
    QCOMPARE (unknown.error, QString {"Field Day section is not registered in the ARRL/RAC table"});
    auto const none = Jtty::encodeNativeAtoms ({});
    QVERIFY (none.tones.isEmpty ());
    QCOMPARE (none.error, QString {"native atom encoding failed"});
  }

  // A function key sends the tones genjtty_atoms_c gives its atoms, with a
  // frame per atom whose last ends the message.
  void nativeAtomsAreSentAsGenjttyAtoms_data ()
  {
    QTest::addColumn<QString> ("macro");
    QTest::addColumn<int> ("profile");
    QTest::addColumn<QString> ("exchange");
    for (int key = 1; key <= 8; ++key) {
      QTest::newRow (qPrintable (Jtty::nativeMacroTemplate (key))) << Jtty::nativeMacroTemplate (key) << 0 << QString {};
    }
    for (int key : {2, 6, 8}) {
      QTest::newRow (qPrintable (Jtty::legacyNativeMacroTemplate (key)))
        << Jtty::legacyNativeMacroTemplate (key) << 0 << QString {};
    }
    QTest::newRow ("%H %G") << "%H %G" << 0 << QString {};
    QTest::newRow ("TU NOW %Q %G") << "TU NOW %Q %G" << 0 << QString {};
    QTest::newRow ("%G") << "%G" << 0 << QString {};
    QTest::newRow ("599 %G") << "599 %G" << 0 << QString {};
    QTest::newRow ("field day") << "%H %E" << 1 << "2D EMA";
    QTest::newRow ("rtty state") << "TU NOW %Q %E" << 2 << "CA";
    QTest::newRow ("control") << "QSO B4" << 0 << QString {};
  }

  void nativeAtomsAreSentAsGenjttyAtoms ()
  {
    QFETCH (QString, macro);
    QFETCH (int, profile);
    QFETCH (QString, exchange);
    auto const context = Jtty::nativeMacroContext ("K1ABC", "W9XYZ", 107, "FN42", -10,
                                                   static_cast<Jtty::NativeExchangeProfile> (profile),
                                                   exchange);
    auto const compiled = Jtty::compileNativeMacro (macro, context);
    QVERIFY (compiled.isNative ());

    int tones[Jtty::maxTransmitFrames * Jtty::transmitFrameSymbols];
    int nsym = 0;
    int status = -1;
    genjtty_atoms_c (compiled.atoms.constData (), compiled.atoms.size (), tones, &nsym, &status);
    QCOMPARE (status, 0);
    auto const encoded = Jtty::encodeNativeAtoms (compiled.atoms);
    QCOMPARE (encoded.tones, QVector<int> (tones, tones + nsym));

    std::vector<Jtty::NativeAtomDescriptor> const atoms (compiled.atoms.cbegin (), compiled.atoms.cend ());
    auto const withFrames = Jtty::Encoder::encodeNativeAtoms (atoms);
    QCOMPARE (int (withFrames.frames.size ()), compiled.atoms.size ());
    QCOMPARE (int (withFrames.tones.size ()), nsym);
    for (std::size_t i = 0; i < withFrames.frames.size (); ++i) {
      QCOMPARE (withFrames.frames[i].back (), i + 1 == withFrames.frames.size () ? '1' : '0');
    }
  }
};

QTEST_GUILESS_MAIN (TestJttyTransmitText);

#include "test_jtty_transmit_text.moc"
