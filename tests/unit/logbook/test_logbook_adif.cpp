#include <QtTest>
#include <QByteArray>
#include <QDateTime>
#include <QString>

#include "logbook/AdifQso.hpp"

namespace
{
  //! QSO inputs for AdifQso::to_adif with sensible defaults; tests override
  //! only the fields they exercise.
  struct Qso
  {
    QString call = "K1ABC";
    QString grid = "FN42";
    QString mode = "FT8";
    QString rptSent = "-10";
    QString rptRcvd = "-12";
    QDateTime on = QDateTime (QDate (2024, 1, 2), QTime (3, 4, 5), Qt::UTC);
    QDateTime off = QDateTime (QDate (2024, 1, 2), QTime (3, 5, 6), Qt::UTC);
    QString band = "20m";
    QString comments;
    QString name;
    QString freq = "14.074000";
    QString myCall = "W1AW";
    QString myGrid = "FN31";
    QString txPower;
    QString op;
    QString xSent;
    QString xRcvd;
    QString prop;
    QString sat;
    QString satMode;
    QString freqRx;
    AdifQso::Contest contest = AdifQso::Contest::None;
  };

  QByteArray run (Qso const& q)
  {
    return AdifQso::to_adif (q.call, q.grid, q.mode, q.rptSent, q.rptRcvd, q.on, q.off,
                             q.band, q.comments, q.name, q.freq, q.myCall, q.myGrid,
                             q.txPower, q.op, q.xSent, q.xRcvd, q.prop, q.sat, q.satMode,
                             q.freqRx, q.contest);
  }
}

#define VERIFY_HAS(rec, sub)                                                  \
  QVERIFY2 ((rec).contains (QByteArray (sub)),                               \
            qPrintable (QStringLiteral ("missing \"%1\" in: %2")            \
                        .arg (QString::fromUtf8 (QByteArray (sub)))         \
                        .arg (QString::fromUtf8 (rec))))

#define VERIFY_LACKS(rec, sub)                                               \
  QVERIFY2 (!(rec).contains (QByteArray (sub)),                             \
            qPrintable (QStringLiteral ("unexpected \"%1\" in: %2")        \
                        .arg (QString::fromUtf8 (QByteArray (sub)))        \
                        .arg (QString::fromUtf8 (rec))))

class TestLogbookAdif : public QObject
{
  Q_OBJECT

private slots:
  void record_structure ()
  {
    auto r = run (Qso {});
    QVERIFY2 (r.startsWith ("<call:5>K1ABC"), r.constData ()); // no leading space on first field
    VERIFY_HAS (r, "<gridsquare:4>FN42");
    VERIFY_HAS (r, "<mode:3>FT8");
    VERIFY_HAS (r, "<rst_sent:3>-10");
    VERIFY_HAS (r, "<rst_rcvd:3>-12");
    VERIFY_HAS (r, "<qso_date:8>20240102");
    VERIFY_HAS (r, "<time_on:6>030405");
    VERIFY_HAS (r, "<qso_date_off:8>20240102");
    VERIFY_HAS (r, "<time_off:6>030506");
    VERIFY_HAS (r, "<band:3>20m");
    VERIFY_HAS (r, "<freq:9>14.074000");
    VERIFY_HAS (r, "<station_callsign:4>W1AW");
    VERIFY_HAS (r, "<my_gridsquare:4>FN31");
    VERIFY_LACKS (r, "<eor>"); // terminator is the caller's responsibility
  }

  void mfsk_modes_use_submode ()
  {
    Qso q; q.mode = "FT4";
    VERIFY_HAS (run (q), "<mode:4>MFSK <submode:3>FT4");
    q.mode = "FST4";
    VERIFY_HAS (run (q), "<mode:4>MFSK <submode:4>FST4");
    q.mode = "Q65";
    VERIFY_HAS (run (q), "<mode:4>MFSK <submode:3>Q65");
  }

  void empty_gridsquare_still_emitted ()
  {
    Qso q; q.grid = "";
    VERIFY_HAS (run (q), "<gridsquare:0>");
  }

  void empty_optionals_omitted ()
  {
    auto r = run (Qso {}); // comments/name/op/txPower/prop/sat all empty by default
    VERIFY_LACKS (r, "<comment:");
    VERIFY_LACKS (r, "<name:");
    VERIFY_LACKS (r, "<operator:");
    VERIFY_LACKS (r, "<tx_pwr:");
    VERIFY_LACKS (r, "<prop_mode:");
    VERIFY_LACKS (r, "<sat_name:");
    VERIFY_LACKS (r, "<sat_mode:");
    VERIFY_LACKS (r, "<freq_rx:");
  }

  void adi_output_replaces_non_ascii_text ()
  {
    Qso q;
    q.name = QString {"Jos"} + QChar {0x00e9};
    q.comments = QString {"M"} + QChar {0x00fc} + "ller " + QChar {0x0141};
    auto r = run (q);
    VERIFY_HAS (r, "<comment:8>M?ller ?");
    VERIFY_HAS (r, "<name:4>Jos?");
    for (auto ch : r)
      {
        QVERIFY2 (ch >= ' ' && ch <= '~', r.constData ());
      }
  }

  void free_text_delimiters_stripped ()
  {
    Qso q;
    q.comments = "hello <eor> there"; // 15 chars after removing '<' and '>'
    q.name = "a>b<c";
    auto r = run (q);
    VERIFY_HAS (r, "<comment:15>hello eor there");
    VERIFY_HAS (r, "<name:3>abc");
    VERIFY_LACKS (r, "<eor>");
  }

  void structured_field_delimiters_stripped ()
  {
    Qso q;
    q.call = "<K1ABC>";
    q.grid = "<FN42>";
    auto r = run (q);
    VERIFY_HAS (r, "<call:5>K1ABC");
    VERIFY_HAS (r, "<gridsquare:4>FN42");
    VERIFY_LACKS (r, "<K1ABC>");
    VERIFY_LACKS (r, "<FN42>");
  }

  void sent_serial_exchange ()
  {
    Qso q; q.xSent = "599 001";
    VERIFY_HAS (run (q), "<stx:3>001");
  }

  void sent_non_serial_exchange_kept ()
  {
    Qso q; q.xSent = "2A NC"; q.contest = AdifQso::Contest::FieldDay;
    VERIFY_HAS (run (q), "<STX_STRING:5>2A NC");
  }

  void sent_exchange_delimiters_stripped ()
  {
    Qso q; q.xSent = "2A <eor> NC";
    auto r = run (q);
    VERIFY_HAS (r, "<STX_STRING:9>2A eor NC");
    VERIFY_LACKS (r, "<eor>");
  }

  void sent_unknown_multiword_numeric_exchange_kept_as_string ()
  {
    Qso q; q.xSent = "ABC DEF 123";
    auto r = run (q);
    VERIFY_HAS (r, "<STX_STRING:11>ABC DEF 123");
    VERIFY_LACKS (r, "<stx:");
  }

  void sent_numeric_prefixed_multiword_exchange_kept_as_string ()
  {
    Qso q; q.xSent = "59123 JN88 EXTRA";
    auto r = run (q);
    VERIFY_HAS (r, "<STX_STRING:16>59123 JN88 EXTRA");
    VERIFY_LACKS (r, "<stx:");
  }

  void received_serial_exchange ()
  {
    Qso q; q.xRcvd = "599 042";
    VERIFY_HAS (run (q), "<srx:3>042");
  }

  void received_eu_vhf_strips_report ()
  {
    Qso q; q.xRcvd = "59123 JN88"; // report 59 + serial 123
    VERIFY_HAS (run (q), "<srx:3>123");
  }

  void received_field_day_exchange ()
  {
    Qso q; q.xRcvd = "2A NC"; q.contest = AdifQso::Contest::FieldDay;
    auto r = run (q);
    VERIFY_HAS (r, "<contest_id:14>ARRL-FIELD-DAY");
    VERIFY_HAS (r, "<SRX_STRING:5>2A NC");
    VERIFY_HAS (r, "<class:2>2A");
    VERIFY_HAS (r, "<arrl_sect:2>NC");
  }

  void received_rtty_state ()
  {
    Qso q; q.xRcvd = "599 MA"; q.contest = AdifQso::Contest::Rtty;
    VERIFY_HAS (run (q), "<state:2>MA");
  }

  void received_multiword_exchange_kept ()
  {
    Qso q; q.xRcvd = "59 123 JN88";
    VERIFY_HAS (run (q), "<SRX_STRING:11>59 123 JN88");
    VERIFY_LACKS (run (q), "<srx:");
  }

  void received_exchange_delimiters_stripped ()
  {
    Qso q; q.xRcvd = "ABC <eor> DEF";
    auto r = run (q);
    VERIFY_HAS (r, "<SRX_STRING:11>ABC eor DEF");
    VERIFY_LACKS (r, "<eor>");
  }

  void received_unknown_multiword_numeric_exchange_kept_as_string ()
  {
    Qso q; q.xRcvd = "ABC DEF 123";
    auto r = run (q);
    VERIFY_HAS (r, "<SRX_STRING:11>ABC DEF 123");
    VERIFY_LACKS (r, "<srx:");
  }

  void received_numeric_prefixed_multiword_exchange_kept_as_string ()
  {
    Qso q; q.xRcvd = "59123 JN88 EXTRA";
    auto r = run (q);
    VERIFY_HAS (r, "<SRX_STRING:16>59123 JN88 EXTRA");
    VERIFY_LACKS (r, "<srx:");
  }

  void received_single_word_not_emitted ()
  {
    Qso q; q.xRcvd = "JN88"; // bare grid, handled as gridsquare elsewhere
    auto r = run (q);
    VERIFY_LACKS (r, "<srx:");
    VERIFY_LACKS (r, "<SRX_STRING:");
  }
};

QTEST_GUILESS_MAIN (TestLogbookAdif)
#include "test_logbook_adif.moc"
