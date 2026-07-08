#include <QTest>

#include "widgets/QSYMessageParser.h"

using QSYMessageParser::Type;
using QSYMessageParser::Message;

class TestQSYMessageParser final
  : public QObject
{
  Q_OBJECT

private slots:
  void decode_frequency_data ()
  {
    QTest::addColumn<QString> ("payload");
    QTest::addColumn<int> ("region");
    QTest::addColumn<bool> ("valid");
    QTest::addColumn<QString> ("frequency");
    QTest::addColumn<QString> ("mode");

    // every band prefix, exercised with a letter mode (-> FT8)
    QTest::newRow ("band-A") << "AL123" << 2 << true << "50.123"    << "FT8";
    QTest::newRow ("band-B") << "BL123" << 2 << true << "144.123"   << "FT8";
    QTest::newRow ("band-C") << "CL123" << 2 << true << "222.123"   << "FT8";
    QTest::newRow ("band-D") << "DL123" << 2 << true << "432.123"   << "FT8";
    QTest::newRow ("band-E") << "EL123" << 2 << true << "1296.123"  << "FT8";
    QTest::newRow ("band-F") << "FL123" << 2 << true << "2304.123"  << "FT8";
    QTest::newRow ("band-G") << "GL123" << 2 << true << "3400.123"  << "FT8";
    QTest::newRow ("band-H") << "HL123" << 2 << true << "5760.123"  << "FT8";
    QTest::newRow ("band-I") << "IL123" << 2 << true << "10368.123" << "FT8";
    QTest::newRow ("band-J-r2") << "JL123" << 2 << true << "24192.123" << "FT8";
    QTest::newRow ("band-J-r1") << "JL123" << 1 << true << "24048.123" << "FT8";
    QTest::newRow ("band-K") << "KL123" << 2 << true << "903.123"   << "FT8";
    QTest::newRow ("band-X") << "XL123" << 2 << true << "24048.123" << "FT8";
    QTest::newRow ("band-9") << "9L123" << 2 << true << "902.123"   << "FT8";
    QTest::newRow ("band-4") << "4L123" << 2 << true << "40.123"    << "FT8";
    QTest::newRow ("band-7") << "7L123" << 2 << true << "70.123"    << "FT8";
    QTest::newRow ("band-L") << "LL123" << 2 << true << "0.123"     << "FT8";
    QTest::newRow ("band-M") << "ML123" << 2 << true << "1.123"     << "FT8";
    QTest::newRow ("band-N") << "NL123" << 2 << true << "3.123"     << "FT8";
    QTest::newRow ("band-O") << "OL123" << 2 << true << "5.123"     << "FT8";
    QTest::newRow ("band-P") << "PL123" << 2 << true << "7.123"     << "FT8";
    QTest::newRow ("band-Q") << "QL123" << 2 << true << "10.123"    << "FT8";
    QTest::newRow ("band-R") << "RL123" << 2 << true << "14.123"    << "FT8";
    QTest::newRow ("band-S") << "SL123" << 2 << true << "18.123"    << "FT8";
    QTest::newRow ("band-T") << "TL123" << 2 << true << "21.123"    << "FT8";
    QTest::newRow ("band-U") << "UL123" << 2 << true << "24.123"    << "FT8";
    QTest::newRow ("band-V") << "VL123" << 2 << true << "28.123"    << "FT8";
    QTest::newRow ("band-W") << "WL123" << 2 << true << "29.123"    << "FT8";

    // every digital mode name, on a fixed HF band (R -> 14.)
    QTest::newRow ("mode-SSB")      << "RV074" << 2 << true << "14.074" << "SSB";
    QTest::newRow ("mode-CW")       << "RW074" << 2 << true << "14.074" << "CW";
    QTest::newRow ("mode-FT8")      << "RL074" << 2 << true << "14.074" << "FT8";
    QTest::newRow ("mode-FT4")      << "RJ074" << 2 << true << "14.074" << "FT4";
    QTest::newRow ("mode-MSK144")   << "RK074" << 2 << true << "14.074" << "MSK144";
    QTest::newRow ("mode-JT9")      << "RA074" << 2 << true << "14.074" << "JT9";
    QTest::newRow ("mode-JT65")     << "RB074" << 2 << true << "14.074" << "JT65";
    QTest::newRow ("mode-FST4")     << "RC074" << 2 << true << "14.074" << "FST4";
    QTest::newRow ("mode-Q65-30B")  << "RD074" << 2 << true << "14.074" << "Q65-30B";
    QTest::newRow ("mode-Q65-60C")  << "RE074" << 2 << true << "14.074" << "Q65-60C";
    QTest::newRow ("mode-Q65-60D")  << "RF074" << 2 << true << "14.074" << "Q65-60D";
    QTest::newRow ("mode-Q65-60E")  << "RG074" << 2 << true << "14.074" << "Q65-60E";
    QTest::newRow ("mode-Q65-120D") << "RH074" << 2 << true << "14.074" << "Q65-120D";

    // FM digit sub-bands (numeric mode -> FM, per-digit MHz)
    QTest::newRow ("fm-A0") << "A0123" << 2 << true << "50.123"  << "FM";
    QTest::newRow ("fm-A3") << "A3123" << 2 << true << "53.123"  << "FM";
    QTest::newRow ("fm-B4") << "B4123" << 2 << true << "144.123" << "FM";
    QTest::newRow ("fm-B7") << "B7123" << 2 << true << "147.123" << "FM";
    QTest::newRow ("fm-C2") << "C2123" << 2 << true << "222.123" << "FM";
    QTest::newRow ("fm-C3") << "C3123" << 2 << true << "223.123" << "FM";
    QTest::newRow ("fm-C0-r1") << "C0123" << 1 << true << "222.123" << "FM";
    QTest::newRow ("fm-D-r2") << "D5446" << 2 << true << "445.446" << "FM";
    QTest::newRow ("fm-D-r1") << "D5432" << 1 << true << "435.432" << "FM";
    QTest::newRow ("fm-E5") << "E5123" << 2 << true << "1295.123" << "FM";
    // user-guide example: QSY W3SZ to 145.550 FM  ->  "B5550"
    QTest::newRow ("userguide-B5550") << "B5550" << 2 << true << "145.550" << "FM";

    // ----- rejections (Type::Invalid) -----
    QTest::newRow ("empty")        << ""       << 2 << false << "" << "";
    QTest::newRow ("too-short")    << "AL12"   << 2 << false << "" << "";
    QTest::newRow ("too-long")     << "AL1234" << 2 << false << "" << "";
    QTest::newRow ("bad-band-Y")   << "YL123"  << 2 << false << "" << "";
    QTest::newRow ("bad-mode-Z")   << "AZ123"  << 2 << false << "" << "";
    QTest::newRow ("bad-mode-I")   << "RI123"  << 2 << false << "" << "";
    QTest::newRow ("bad-mode-R")   << "RR123"  << 2 << false << "" << "";
    QTest::newRow ("bad-fm-digit") << "A5123"  << 2 << false << "" << "";  // A only 0..3
    QTest::newRow ("bad-C0-r2")    << "C0123"  << 2 << false << "" << "";
    QTest::newRow ("bad-C2-r1")    << "C2123"  << 1 << false << "" << "";
    QTest::newRow ("nondigit-freq")<< "AL12X"  << 2 << false << "" << "";
    QTest::newRow ("bare-call")    << "KD0BTO" << 2 << false << "" << "";
  }

  void decode_frequency ()
  {
    QFETCH (QString, payload);
    QFETCH (int, region);
    QFETCH (bool, valid);
    QFETCH (QString, frequency);
    QFETCH (QString, mode);

    Message const m = QSYMessageParser::decode (payload, region);
    QCOMPARE (m.type == Type::Frequency, valid);
    if (valid)
      {
        QCOMPARE (m.frequency_mhz, frequency);
        QCOMPARE (m.mode, mode);
      }
  }

  void decode_general_data ()
  {
    QTest::addColumn<QString> ("payload");
    QTest::addColumn<bool> ("valid");
    QTest::addColumn<QString> ("text");

    QTest::newRow ("c01") << "ZA001" << true << "You are in the log";
    QTest::newRow ("c02") << "ZA002" << true << "Please call me by phone";
    QTest::newRow ("c03") << "ZA003" << true << "Check your email";
    QTest::newRow ("c04") << "ZA004" << true << "You are not in Contest Mode";
    QTest::newRow ("c05") << "ZA005" << true << "Please check your PC Clock";
    QTest::newRow ("c06") << "ZA006" << true << "You are transmitting in the wrong time slot";
    QTest::newRow ("c07") << "ZA007" << true << "Please transmit above 1000 Hz";
    QTest::newRow ("c08") << "ZA008" << true << "Your transmitter is overmodulated";
    QTest::newRow ("c09") << "ZA009" << true << "Your audio is distorted";
    QTest::newRow ("c10") << "ZA010" << true << "You are transmitting on top of a rare DX station";
    QTest::newRow ("c11") << "ZA011" << true << "Please check HB9Q Logger";
    QTest::newRow ("c12") << "ZA012" << true << "Please check Ping Jockey";
    QTest::newRow ("c13") << "ZA013" << true << "Please check ON4KST";
    QTest::newRow ("c14") << "ZA014" << true << "Please QSL via LoTW";

    QTest::newRow ("code-000")  << "ZA000" << false << "";
    QTest::newRow ("code-015")  << "ZA015" << false << "";
    QTest::newRow ("code-999")  << "ZA999" << false << "";
    QTest::newRow ("za-short")  << "ZA"    << false << "";
    QTest::newRow ("za-nondig") << "ZA0X1" << false << "";
    QTest::newRow ("non-za")    << "AA007" << false << "";
  }

  void decode_general ()
  {
    QFETCH (QString, payload);
    QFETCH (bool, valid);
    QFETCH (QString, text);

    Message const m = QSYMessageParser::decode (payload, 2);
    QCOMPARE (m.type == Type::General, valid);
    if (valid) QCOMPARE (m.text, text);
  }

  void decode_reply_data ()
  {
    QTest::addColumn<QString> ("payload");
    QTest::addColumn<bool> ("valid");
    QTest::addColumn<QString> ("call");
    QTest::addColumn<QString> ("response");

    QTest::newRow ("ok")        << "$ K1ABC OKQSY"    << true  << "K1ABC"  << "OK";
    QTest::newRow ("no")        << "$  N0CALL  NOQSY" << true  << "N0CALL" << "NO";
    QTest::newRow ("too-few")   << "$ K1ABC"          << false << ""       << "";
    QTest::newRow ("bad-reply") << "$ K1ABC MAYBE"    << false << ""       << "";
    QTest::newRow ("too-many")  << "$ K1ABC OKQSY X"  << false << ""       << "";
    QTest::newRow ("no-dollar") << "K1ABC OKQSY"      << false << ""       << "";
    QTest::newRow ("dollar-prefix-token") << "$junk K1ABC OKQSY" << false << "" << "";
  }

  void decode_reply ()
  {
    QFETCH (QString, payload);
    QFETCH (bool, valid);
    QFETCH (QString, call);
    QFETCH (QString, response);

    Message const m = QSYMessageParser::decode (payload, 2);
    QCOMPARE (m.type == Type::Reply, valid);
    if (valid)
      {
        QCOMPARE (m.call, call);
        QCOMPARE (m.response, response);
      }
  }

  void roundtrip_frequency ()
  {
    static int const kHzSamples[] = {0, 1, 74, 125, 500, 999};
    QString candidates;
    for (char b = 'A'; b <= 'Z'; ++b) candidates += QChar (b);
    for (char d = '0'; d <= '9'; ++d) candidates += QChar (d);

    int accepted = 0, rejected = 0;
    for (int r = 1; r <= 2; ++r)
      for (QChar band : candidates)
        for (QChar mode : candidates)
          for (int kHz : kHzSamples)
            {
              QString const wire = QString {band} + mode + QString {"%1"}.arg (kHz, 3, 10, QLatin1Char ('0'));
              QString const enc = QSYMessageParser::encodeFrequency (band, mode, kHz, r);
              Message const m = QSYMessageParser::decode (wire, r);

              if (enc.isEmpty ())
                {
                  QVERIFY2 (m.type != Type::Frequency, qPrintable (QString ("decode accepted as frequency but encode rejected: %1 r%2").arg (wire).arg (r)));
                  ++rejected;
                }
              else
                {
                  QCOMPARE (enc, wire);
                  QVERIFY2 (m.type == Type::Frequency, qPrintable (QString ("encode accepted but decode rejected: %1 r%2").arg (wire).arg (r)));
                  QCOMPARE (m.band, band);
                  QCOMPARE (m.mode_char, mode);
                  QCOMPARE (m.kHz, kHz);
                  QCOMPARE (QSYMessageParser::encode (m), wire);
                  ++accepted;
                }
            }
    QVERIFY (accepted > 0 && rejected > 0);
  }

  void roundtrip_general ()
  {
    for (int code = 0; code <= 20; ++code)
      {
        QString const enc = QSYMessageParser::encodeGeneral (code);
        QString const wire = QStringLiteral ("ZA") + QString {"%1"}.arg (code, 3, 10, QLatin1Char ('0'));
        Message const m = QSYMessageParser::decode (wire, 2);
        if (code >= 1 && code <= 14)
          {
            QCOMPARE (enc, wire);
            QVERIFY (m.type == Type::General);
            QCOMPARE (m.code, code);
            QCOMPARE (QSYMessageParser::encode (m), wire);
          }
        else
          {
            QVERIFY (enc.isEmpty ());
            QVERIFY (!m);
          }
      }
  }

  void encode_boundaries ()
  {
    QCOMPARE (QSYMessageParser::encodeFrequency ('A', 'L', 0, 2), QString ("AL000"));
    QCOMPARE (QSYMessageParser::encodeFrequency ('A', 'L', 999, 2), QString ("AL999"));
    QVERIFY (QSYMessageParser::encodeFrequency ('A', 'L', -1, 2).isEmpty ());
    QVERIFY (QSYMessageParser::encodeFrequency ('A', 'L', 1000, 2).isEmpty ());
    QVERIFY (QSYMessageParser::encodeFrequency ('Y', 'L', 123, 2).isEmpty ());
    QVERIFY (QSYMessageParser::encodeFrequency ('A', 'Z', 123, 2).isEmpty ());
    QVERIFY (QSYMessageParser::encodeFrequency ('A', '5', 123, 2).isEmpty ());
    QVERIFY (QSYMessageParser::encodeFrequency ('C', '0', 123, 2).isEmpty ());
    QVERIFY (QSYMessageParser::encodeFrequency ('C', '2', 123, 1).isEmpty ());

    QCOMPARE (QSYMessageParser::encodeGeneral (1), QString ("ZA001"));
    QCOMPARE (QSYMessageParser::encodeGeneral (14), QString ("ZA014"));
    QVERIFY (QSYMessageParser::encodeGeneral (0).isEmpty ());
    QVERIFY (QSYMessageParser::encodeGeneral (15).isEmpty ());
  }

  void reply_wire ()
  {
    QCOMPARE (QSYMessageParser::encodeReplyWire (true),  QString ("OKQSY"));
    QCOMPARE (QSYMessageParser::encodeReplyWire (false), QString ("NOQSY"));
  }

  void decode_line_data ()
  {
    QTest::addColumn<QString> ("line");
    QTest::addColumn<bool> ("found");
    QTest::addColumn<QString> ("call");
    QTest::addColumn<QString> ("payload");
    QTest::addColumn<int> ("type");  // Type as int

    QTest::newRow ("freq")
      << "120000  -10  0.2 1234 ~  W3SZ.B5550" << true << "W3SZ" << "B5550" << int (Type::Frequency);
    QTest::newRow ("general")
      << "120000  -10  0.2 1234 ~  KD0BTO.ZA007" << true << "KD0BTO" << "ZA007" << int (Type::General);
    QTest::newRow ("dt-dot-then-msg")
      << "120000  -10  0.2 1234 ~  K1ABC.AL125" << true << "K1ABC" << "AL125" << int (Type::Frequency);
    QTest::newRow ("no-qsy")
      << "120000  -10  0.2 1234 ~  CQ W3SZ FN20" << false << "" << "" << int (Type::Invalid);
    QTest::newRow ("garbage-dot")
      << "120000  -10  0.2 1234 ~  KD0BTO.NOTAMSG" << false << "" << "" << int (Type::Invalid);
  }

  void decode_line ()
  {
    QFETCH (QString, line);
    QFETCH (bool, found);
    QFETCH (QString, call);
    QFETCH (QString, payload);
    QFETCH (int, type);

    QSYMessageParser::LineResult const r = QSYMessageParser::decodeLine (line, 2);
    QCOMPARE (bool (r.message), found);
    if (found)
      {
        QCOMPARE (r.call, call);
        QCOMPARE (r.payload, payload);
        QCOMPARE (int (r.message.type), type);
      }
  }

  void prefilter_recall ()
  {
    QVERIFY (QSYMessageParser::mightContainMessage ("xx W3SZ.B5550"));
    QVERIFY (QSYMessageParser::mightContainMessage ("xx KD0BTO.ZA007"));
    QVERIFY (QSYMessageParser::mightContainMessage ("xx N0CALL.OKQSY"));
    QVERIFY (!QSYMessageParser::mightContainMessage ("CQ W3SZ FN20"));
  }
};

QTEST_APPLESS_MAIN (TestQSYMessageParser)

#include "test_qsy_message_parser.moc"
