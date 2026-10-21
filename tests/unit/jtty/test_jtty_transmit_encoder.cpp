// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QRegularExpression>

#include <cstdint>
#include <string>

#include "widgets/JttyMessages.hpp"

namespace
{
  using namespace Jtty::Encoder;

  std::u16string units (QString const& text)
  {
    return Jtty::toEncoder (text);
  }

  QString qt (std::u16string const& text)
  {
    return Jtty::fromEncoder (text);
  }

  // QString::fromUcs4 reads U+FEFF as a byte order mark.
  QString character (uint code)
  {
    return code < 0x10000 ? QString (QChar (char16_t (code))) : QString::fromUcs4 (&code, 1);
  }
}

// The encoder handles characters as Qt does.
class TestJttyTransmitEncoder final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void upperCaseIsQtsForEveryCodePoint ()
  {
    for (uint code = 0; code <= 0x10FFFF; ++code) {
      if (code >= 0xD800 && code <= 0xDFFF) continue;
      QString const text = character (code);
      if (toUpper (units (text)) != units (text.toUpper ())) {
        QFAIL (qPrintable (QString::number (code, 16)));
      }
    }
    QString const mixed = QString::fromUtf8 ("stra\u00dfe \U00010428\u0149 \ufb03x \u1f80");
    QCOMPARE (qt (toUpper (units (mixed))), mixed.toUpper ());
  }

  void spacesAreQtsForEveryUnit ()
  {
    for (uint code = 0; code <= 0xFFFF; ++code) {
      QCOMPARE (isSpace (char16_t (code)), QChar (char16_t (code)).isSpace ());
    }
    for (uint code = 0; code <= 0xFFFF; ++code) {
      QChar const c {char16_t (code)};
      for (QString const& text : {QString (c), QString ("a") + c + "b", QString (c) + "ab" + c,
                                  QString ("a") + c + c + "b"}) {
        if (trimmed (units (text)) != units (text.trimmed ())
            || simplified (units (text)) != units (text.simplified ())) {
          QFAIL (qPrintable (QString::number (code, 16)));
        }
      }
    }
    QString const text = QString::fromUtf8 ("\u00a0 cq\t\u2003%m\u3000\u3000cq \u0085\n");
    QCOMPARE (qt (simplified (units (text))), text.simplified ());
    QCOMPARE (qt (trimmed (units (text))), text.trimmed ());
  }

  // nativeGridExchange's locator check is the GUI's QRegularExpression, for
  // every string of up to six characters, and of up to nine after FN42, over
  // characters at and beyond each class's edges: 2,125,872 strings.
  void locatorsAreTheGuisPattern ()
  {
    QRegularExpression const pattern {QStringLiteral ("^[A-R]{2}[0-9]{2}(?:[A-X]{2}(?:[0-9]{2})?)?$")};
    std::u16string const alphabet {u"ARSXY09ax \n"};
    NativeMacroContext context;
    auto check = [&] (std::u16string const& prefix, int length) {
      long long combinations = 1;
      for (int i = 0; i < length; ++i) combinations *= alphabet.size ();
      for (long long k = 0; k < combinations; ++k) {
        context.grid = prefix;
        for (long long rest = k; context.grid.size () < prefix.size () + length; rest /= alphabet.size ()) {
          context.grid += alphabet[rest % alphabet.size ()];
        }
        bool const valid = pattern.match (qt (context.grid).trimmed ().toUpper ()).hasMatch ();
        if (nativeGridExchange (context, Jtty::ExchangeRole::FieldOnly).valid != valid) {
          QFAIL (qPrintable (qt (context.grid)));
        }
      }
    };
    for (int length = 0; length <= 6; ++length) check ({}, length);
    for (int length = 1; length <= 5; ++length) check (u"FN42", length);
  }

  void templatesNormalizeUnicodeAsQt ()
  {
    auto const context = Jtty::Encoder::nativeMacroContext (u"K1ABC", u"W9XYZ", 7, {}, -10,
                                                            Jtty::NativeExchangeProfile::None, {});
    auto const cq = compileNativeMacro (u"cq\u00a0%m\u2003cq", context);
    QVERIFY (cq.isNative ());
    QCOMPARE (qt (cq.text), QString {"CQ K1ABC CQ"});
    auto const state = compileNativeMacro (u"\ufb06ate?", context);
    QVERIFY (state.isNative ());
    QCOMPARE (qt (state.text), QString {"STATE?"});
    QCOMPARE (qt (compileNativeMacro (u"%h 599 %n", context).text), QString {"%h 599 %n"});
    QVERIFY (!compileNativeMacro (u"%h 599 %n", context).isNative ());
    QString const marked = QString (QChar (0xFFFE)) + QChar (0xFEFF) + "%M";
    QCOMPARE (Jtty::expandLiteralMacro (marked, Jtty::NativeMacroContext {"K1ABC", {}, 0}),
              QString (QChar (0xFFFE)) + QChar (0xFEFF) + "K1ABC");
  }

  void compilerKeepsItsEdges ()
  {
    auto const fieldDay = Jtty::Encoder::nativeMacroContext (
      u"K1ABC", u"W9XYZ", 107, u"FN42", -10, Jtty::NativeExchangeProfile::FieldDay, u"2D EMA");
    auto const legacy = compileNativeMacro (u"%H 599 %N", fieldDay);
    QVERIFY (legacy.isNative ());
    QCOMPARE (qt (legacy.text), QString {"W9XYZ 599 107"});
    QCOMPARE (legacy.atoms.at (1).kind, std::int8_t (Jtty::NativeAtomKind::ExchangeNumber));
    QCOMPARE (qt (compileNativeMacro (u"%H %E", fieldDay).text), QString {"W9XYZ 2D EMA"});

    auto const oneCharacter = Jtty::Encoder::nativeMacroContext (
      {}, {}, 1, {}, -10, Jtty::NativeExchangeProfile::FieldDay, u"D EMA");
    QCOMPARE (qt (nativeExchange (oneCharacter).error),
              QString {"Field Day exchange must be COUNTCLASS SECTION"});

    auto const empty = Jtty::Encoder::nativeMacroContext (
      {}, {}, 1, {}, -10, Jtty::NativeExchangeProfile::None, {});
    QCOMPARE (qt (expandLiteralMacro (u"%M%M", empty)), QString {});
    QCOMPARE (qt (expandLiteralMacro (u"%%MM", empty)), QString {"%M"});
  }

  void boundsAndOverflowAreTheGuis ()
  {
    using Jtty::NativeExchangeProfile;
    auto context = [] (int serial, NativeExchangeProfile profile, std::u16string const& exchange) {
      return Jtty::Encoder::nativeMacroContext (u"K1ABC", u"W9XYZ", serial, u"FN42", -10, profile,
                                                exchange);
    };
    // As QString::toInt, a configured number past int fails rather than wrapping.
    QCOMPARE (qt (nativeExchange (context (1, NativeExchangeProfile::RttyRoundup, u"4294967297")).error),
              QString {"Configured serial must be between 0 and 131071"});
    QCOMPARE (qt (nativeExchange (context (1, NativeExchangeProfile::FieldDay, u"4294967297D EMA")).error),
              QString {"Field Day transmitter count must be between 1 and 32"});
    for (auto const form : {u"%H 599 %N", u"TU NOW %Q 599 %N", u"599 %N"}) {
      for (int const serial : {-1, 1 << 17}) {
        QCOMPARE (compileNativeMacro (form, context (serial, NativeExchangeProfile::None, {})).status,
                  Jtty::NativeMacroStatus::InvalidRuntime);
      }
    }
    auto const grid = compileNativeMacro (u"599  %g", context (1, NativeExchangeProfile::None, {}));
    QVERIFY (grid.isNative ());
    QCOMPARE (qt (grid.text), QString {"599 FN42"});
  }

  void textIsPreparedOneUnitAtATime ()
  {
    QString const text = QString::fromUtf8 ("\u00e9t\u00e9 \U0001F600 a\u00a0b\r\n~");
    auto const prepared = prepareTransmitText (units (text));
    QCOMPARE (qt (prepared.text), QString {"#t# ## a#b\n\n "});
    QVERIFY (prepared.substituted);
  }

  void adaptersKeepEveryUnit ()
  {
    QString const marked = QString (QChar (0xFFFE)) + QChar (0xFEFF) + "AB";
    QCOMPARE (Jtty::nextTransmitTextSegment (marked, 0).text, marked);
    QCOMPARE (Jtty::transmitFrame (marked).left (4), marked);
  }
};

QTEST_GUILESS_MAIN (TestJttyTransmitEncoder);

#include "test_jtty_transmit_encoder.moc"
