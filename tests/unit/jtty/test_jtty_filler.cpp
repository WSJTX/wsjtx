// SPDX-License-Identifier: GPL-3.0-or-later
// lib/jtty/jtty_filler.f90 implements the GUI's live-entry filler rule
// (Jtty::stripJttyFillerText) for Fortran callers; both must turn the same
// Latin-1 text into the same text.

#include <QByteArray>
#include <QtTest>

#include "widgets/JttyMessages.hpp"

extern "C" int jtty_strip_filler_text (char const * text, int length, char * stripped);

namespace
{
QByteArray fortranStrip (QByteArray const& text)
{
  QByteArray stripped (text.size () + 1, '\0');
  auto const length = jtty_strip_filler_text (text.constData (), text.size (), stripped.data ());
  return length >= 0 && length <= text.size () ? stripped.left (length) : QByteArray {"<bad length>"};
}

QByteArray guiStrip (QByteArray const& text)
{
  return Jtty::stripJttyFillerText (QString::fromLatin1 (text)).toLatin1 ();
}

// The text a decoder update reaches the rule with (Jtty::Decoder::takeUpdates).
QByteArray guiStripDecoded (QByteArray const& text)
{
  return Jtty::stripJttyFillerText (QString::fromLatin1 (text).trimmed ()).toLatin1 ();
}
}

class TestJttyFiller final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void matchesTheGui_data ()
  {
    QTest::addColumn<QByteArray> ("text");
    QTest::addColumn<QByteArray> ("stripped");
    QTest::newRow ("empty") << QByteArray {} << QByteArray {};
    QTest::newRow ("no filler") << QByteArray {"CQ CQ DE K1ABC"} << QByteArray {"CQ CQ DE K1ABC"};
    QTest::newRow ("4 <") << QByteArray {"OVER<<<<THE"} << QByteArray {"OVER<<<<THE"};
    QTest::newRow ("5 <") << QByteArray {"OVER<<<<<THE"} << QByteArray {"OVER THE"};
    QTest::newRow ("6 <") << QByteArray {"OVER<<<<<<THE"} << QByteArray {"OVER <THE"};
    QTest::newRow ("10 <") << QByteArray {"OVER<<<<<<<<<<THE"} << QByteArray {"OVER THE"};
    QTest::newRow ("12 <") << QByteArray {"OVER<<<<<<<<<<<<THE"} << QByteArray {"OVER <<THE"};
    QTest::newRow ("live entry") << QByteArray {"OVER <<<<< <<<<< THE"} << QByteArray {"OVER THE"};
    QTest::newRow ("unspaced") << QByteArray {"OVER<<<<< <<<<<THE"} << QByteArray {"OVER THE"};
    QTest::newRow ("internal spacing") << QByteArray {"OVER  <<<<<   <<<<<\t THE"} << QByteArray {"OVER THE"};
    QTest::newRow ("leading") << QByteArray {"<<<<< <<<<< OVER"} << QByteArray {"OVER"};
    QTest::newRow ("trailing") << QByteArray {"OVER <<<<< <<<<<"} << QByteArray {"OVER"};
    QTest::newRow ("first block only") << QByteArray {"OVER <<<<<"} << QByteArray {"OVER"};
    QTest::newRow ("all filler") << QByteArray {"<<<<< <<<<<"} << QByteArray {};
    QTest::newRow ("one block") << QByteArray {"<<<<<"} << QByteArray {};
    QTest::newRow ("blanks") << QByteArray {"     "} << QByteArray {};
    QTest::newRow ("typed <") << QByteArray {"A < B << C <<<< D"} << QByteArray {"A < B << C <<<< D"};
    QTest::newRow ("typed < by filler") << QByteArray {"A <<<<<<<<< B"} << QByteArray {"A <<<< B"};
    QTest::newRow ("double spaces") << QByteArray {"CQ  CQ   DE"} << QByteArray {"CQ CQ DE"};
    QTest::newRow ("edge spaces") << QByteArray {" CQ DE K1ABC "} << QByteArray {"CQ DE K1ABC"};
    QTest::newRow ("control blanks") << QByteArray {"\tCQ\nDE\rK1ABC\v\f"} << QByteArray {"CQ DE K1ABC"};
    QTest::newRow ("separate runs") << QByteArray {"A<<<<<B <<<<< C"} << QByteArray {"A B C"};
    QTest::newRow ("gap markers") << QByteArray {"HI ... THE<<<<< ... X"} << QByteArray {"HI ... THE ... X"};
    QTest::newRow ("NBSP") << QByteArray {"CQ\xa0<<<<<\xa0" "DE"} << QByteArray {"CQ DE"};
    QTest::newRow ("NEL") << QByteArray {"\x85" "CQ\x85\x85" "DE\x85"} << QByteArray {"CQ DE"};
  }

  void matchesTheGui ()
  {
    QFETCH (QByteArray, text);
    QFETCH (QByteArray, stripped);
    QCOMPARE (guiStrip (text), stripped);
    QCOMPARE (fortranStrip (text), stripped);
    QCOMPARE (guiStripDecoded (text), stripped);
  }

  // Every string of up to seven characters from a small alphabet, and every
  // sequence of up to five tokens that puts each kind of whitespace next to
  // filler blocks and to stray '<'.
  void matchesTheGuiOnSweptText ()
  {
    auto const compare = [] (QByteArray const& text) {
      return fortranStrip (text) == guiStrip (text) && fortranStrip (text) == guiStripDecoded (text);
    };
    QList<QByteArray> const characters {"<", " ", "A", ".", "\t", "\xa0"};
    QList<QByteArray> const tokens {"<<<<<", "<", " ", "A", "\t", "\n", "\v", "\f", "\r",
                                    "\xa0", "\x85"};
    int compared = 0;
    for (auto const& sweep : {qMakePair (characters, 7), qMakePair (tokens, 5)})
      {
        auto const& alphabet = sweep.first;
        for (int length = 0; length <= sweep.second; ++length)
          {
            int count = 1;
            for (int i = 0; i < length; ++i) count *= alphabet.size ();
            for (int n = 0; n < count; ++n)
              {
                QByteArray text;
                for (int i = 0, m = n; i < length; ++i, m /= alphabet.size ())
                  text += alphabet[m % alphabet.size ()];
                if (!compare (text))
                  QFAIL (qPrintable (QString {"the rules differ on \"%1\""}.arg (QString::fromLatin1 (text))));
                ++compared;
              }
          }
      }
    QCOMPARE (compared, 335923 + 177156);
  }
};

QTEST_GUILESS_MAIN (TestJttyFiller)

#include "test_jtty_filler.moc"
