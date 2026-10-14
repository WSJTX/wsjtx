// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

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
}

// The encoder handles characters as Qt does.
class TestJttyTransmitEncoder final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void spacesAreQtsForEveryUnit ()
  {
    for (uint code = 0; code <= 0xFFFF; ++code) {
      QCOMPARE (isSpace (char16_t (code)), QChar (char16_t (code)).isSpace ());
    }
    for (uint code = 0; code <= 0xFFFF; ++code) {
      QChar const c {char16_t (code)};
      for (QString const& text : {QString (c), QString ("a") + c + "b", QString (c) + "ab" + c}) {
        if (trimmed (units (text)) != units (text.trimmed ())) {
          QFAIL (qPrintable (QString::number (code, 16)));
        }
      }
    }
    QString const text = QString::fromUtf8 ("\u00a0 cq\t\u2003%m\u3000\u3000cq \u0085\n");
    QCOMPARE (qt (trimmed (units (text))), text.trimmed ());
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
