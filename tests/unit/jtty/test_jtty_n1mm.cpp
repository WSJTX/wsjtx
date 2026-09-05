#include <QtTest>

#include "widgets/JttyN1mm.hpp"
#include "widgets/JttyN1mmOutput.hpp"

class TestJttyN1mm final : public QObject
{
  Q_OBJECT

  using Kind = Jtty::NativeAtomKind;
  using Profile = Jtty::NativeExchangeProfile;
  using Status = Jtty::N1mmCompileStatus;

private slots:
  void rejectedOutputCompletesOnceAtOff ()
  {
    Jtty::N1mmOutput output;
    output.submit (1);
    QVERIFY (output.resolve (1));
    QVERIFY (!output.takeCompletion (false));
    QVERIFY (!output.takeCompletion (false, true));
    output.start ();
    output.finish ();
    QVERIFY (output.takeCompletion (false));
    QVERIFY (!output.takeCompletion (false, true));
    output.finish ();
    QVERIFY (!output.finishRequested ());
  }

  void mixedOutputWaitsForAcceptedAudio ()
  {
    Jtty::N1mmOutput output;
    output.start ();
    output.submit (1);
    QVERIFY (output.resolve (1));
    QVERIFY (!output.takeCompletion (false));
    QVERIFY (output.startRequested ());
    output.submit (2);
    output.finish ();
    QVERIFY (output.startRequested ());
    QVERIFY (!output.takeCompletion (false));
    QVERIFY (!output.takeCompletion (true));
    QVERIFY (output.accept (2));
    output.started ();
    QVERIFY (output.resolve (2));
    QVERIFY (!output.takeCompletion (true));
    QVERIFY (output.takeCompletion (false, true));
  }

  void asynchronousRejectionAfterOffCompletesWithoutDrain ()
  {
    Jtty::N1mmOutput output;
    output.start ();
    output.submit (1);
    output.finish ();
    QVERIFY (!output.takeCompletion (true));
    QVERIFY (output.resolve (1));
    QVERIFY (!output.takeCompletion (true));
    QVERIFY (output.takeCompletion (false));
  }

  void drainBeforeOffAndAbortDoNotDuplicateCompletion ()
  {
    Jtty::N1mmOutput output;
    output.submit (1);
    output.start ();
    QVERIFY (output.accept (1));
    QVERIFY (output.resolve (1));
    QVERIFY (output.takeCompletion (false, true));
    output.finish ();
    QVERIFY (!output.takeCompletion (false));
    output.submit (2);
    output.abort ();
    QVERIFY (!output.resolve (2));
    output.finish ();
    QVERIFY (!output.takeCompletion (false, true));
  }

  void everyActionCompiles ()
  {
    struct Example
    {
      QString message;
      Profile profile;
      QString canonical;
      QVector<Kind> kinds;
    };
    QVector<Example> const examples {
      {"[[JTTY:CQ]] K1ABC", Profile::None, "CQ K1ABC CQ", {Kind::Call}},
      {"[[JTTY:CALL_EXCH]] W9XYZ 7", Profile::None, "W9XYZ 599 007",
       {Kind::Call, Kind::ExchangeNumber}},
      {"[[JTTY:CALL_TU_CQ]] W9XYZ K1ABC", Profile::None, "W9XYZ TU CQ K1ABC CQ",
       {Kind::Call, Kind::Call}},
      {"[[JTTY:MYCALL]] K1ABC", Profile::None, "K1ABC", {Kind::Call}},
      {"[[JTTY:HISCALL]] W9XYZ", Profile::None, "W9XYZ", {Kind::Call}},
      {"[[JTTY:TU_NOW_EXCH]] W9XYZ 42", Profile::None, "TU NOW W9XYZ 599 042",
       {Kind::Call, Kind::ExchangeNumber}},
      {"[[JTTY:CALL_MY]] W9XYZ K1ABC", Profile::None, "W9XYZ K1ABC",
       {Kind::Call, Kind::Call}},
      {"[[JTTY:CALL_TU_MY]] W9XYZ K1ABC", Profile::None, "W9XYZ TU K1ABC",
       {Kind::Call, Kind::Call}},
      {"[[JTTY:EXCH]] 1D EMA", Profile::FieldDay, "1D EMA", {Kind::ExchangePair}},
      {"[[JTTY:GRID]] fn42", Profile::None, "FN42", {Kind::Grid4}},
      {"[[JTTY:CONTROL]] qsl tu", Profile::None, "QSL TU", {Kind::Control}}
    };

    for (auto const& example : examples) {
      auto const result = Jtty::compileN1mmMessage (example.message, example.profile);
      QCOMPARE (result.status, Status::Native);
      QCOMPARE (result.canonicalText, example.canonical);
      QCOMPARE (result.atoms.size (), example.kinds.size ());
      for (int i = 0; i < result.atoms.size (); ++i) {
        QCOMPARE (int {result.atoms.at (i).kind}, static_cast<int> (example.kinds.at (i)));
      }
    }
  }

  void tagMatchingIsCaseAndWhitespaceNormalized ()
  {
    auto const result = Jtty::compileN1mmMessage (
        QStringLiteral (" \t[[jtty:call_tu_cq]]  w9xyz\n k1abc  "));
    QCOMPARE (result.status, Status::Native);
    QCOMPARE (result.canonicalText, QStringLiteral ("W9XYZ TU CQ K1ABC CQ"));
  }

  void tagClassification ()
  {
    QString const literal = QStringLiteral ("  W9XYZ 599 007");
    auto const untagged = Jtty::compileN1mmMessage (literal);
    QCOMPARE (untagged.status, Status::Literal);
    QCOMPARE (untagged.literalText, literal);
    QVERIFY (untagged.atoms.isEmpty ());

    auto const notFirst = Jtty::compileN1mmMessage (
        QStringLiteral ("CQ [[JTTY:CQ]] K1ABC"));
    QCOMPARE (notFirst.status, Status::Literal);

    auto const malformed = Jtty::compileN1mmMessage (
        QStringLiteral (" [[JTTY:CQ K1ABC"));
    QCOMPARE (malformed.status, Status::Error);
    QVERIFY (!malformed.error.isEmpty ());

    auto const malformedStem = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY]] K1ABC"));
    QCOMPARE (malformedStem.status, Status::Error);
    QVERIFY (!malformedStem.error.isEmpty ());

    auto const unknown = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY:MAGIC]] K1ABC"));
    QCOMPARE (unknown.status, Status::Error);
    QVERIFY (unknown.error.contains (QStringLiteral ("unknown")));
  }

  void profileDrivenExchange ()
  {
    auto const serial = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY:EXCH]] 00042"), Profile::RttyRoundup);
    QCOMPARE (serial.status, Status::Native);
    QCOMPARE (serial.canonicalText, QStringLiteral ("599 042"));
    QCOMPARE (int {serial.atoms.first ().kind}, static_cast<int> (Kind::ExchangeNumber));
    QCOMPARE (serial.atoms.first ().value, qint32 {42});

    auto const state = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY:CALL_EXCH]] W9XYZ az"), Profile::RttyRoundup);
    QCOMPARE (state.status, Status::Native);
    QCOMPARE (state.canonicalText, QStringLiteral ("W9XYZ 599 AZ"));
    QCOMPARE (int {state.atoms.last ().kind}, static_cast<int> (Kind::ExchangeLocation));
    QCOMPARE (int {state.atoms.last ().subtype},
              static_cast<int> (Jtty::LocationKind::StateProvince));

    auto const dx = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY:EXCH]] DX"), Profile::RttyRoundup);
    QCOMPARE (dx.status, Status::Native);
    QCOMPARE (dx.canonicalText, QStringLiteral ("599 DX"));
    QCOMPARE (int {dx.atoms.first ().kind}, static_cast<int> (Kind::ExchangeLocation));

    auto const fieldDay = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY:EXCH]] 32f ema"), Profile::FieldDay);
    QCOMPARE (fieldDay.status, Status::Native);
    QCOMPARE (fieldDay.canonicalText, QStringLiteral ("32F EMA"));
    QCOMPARE (fieldDay.atoms.first ().value, qint32 {32});
    QCOMPARE (int {fieldDay.atoms.first ().role}, 5);
    QCOMPARE (QString::fromLatin1 (fieldDay.atoms.first ().text), QStringLiteral ("EMA"));
  }

  void payloadValidation_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::addColumn<int> ("profile");

    QTest::newRow ("empty") << QStringLiteral ("[[JTTY:CQ]]") << int (Profile::None);
    QTest::newRow ("cq-extra") << QStringLiteral ("[[JTTY:CQ]] K1ABC CQ")
                                << int (Profile::None);
    QTest::newRow ("bad-call") << QStringLiteral ("[[JTTY:HISCALL]] NOTACALL")
                               << int (Profile::None);
    QTest::newRow ("call-extra") << QStringLiteral ("[[JTTY:MYCALL]] K1ABC EXTRA")
                                 << int (Profile::None);
    QTest::newRow ("missing-exchange") << QStringLiteral ("[[JTTY:CALL_EXCH]] W9XYZ")
                                       << int (Profile::None);
    QTest::newRow ("serial-junk") << QStringLiteral ("[[JTTY:EXCH]] 12A")
                                  << int (Profile::None);
    QTest::newRow ("serial-overflow") << QStringLiteral ("[[JTTY:EXCH]] 131072")
                                      << int (Profile::RttyRoundup);
    QTest::newRow ("unexpanded-hash") << QStringLiteral ("[[JTTY:EXCH]] #")
                                    << int (Profile::RttyRoundup);
    QTest::newRow ("field-day-shape") << QStringLiteral ("[[JTTY:EXCH]] 1D")
                                      << int (Profile::FieldDay);
    QTest::newRow ("field-day-compact") << QStringLiteral ("[[JTTY:EXCH]] 3AOR")
                                        << int (Profile::FieldDay);
    QTest::newRow ("field-day-count") << QStringLiteral ("[[JTTY:EXCH]] 33A EMA")
                                      << int (Profile::FieldDay);
    QTest::newRow ("grid-extra") << QStringLiteral ("[[JTTY:GRID]] 599 FN42")
                                 << int (Profile::None);
    QTest::newRow ("grid-field") << QStringLiteral ("[[JTTY:GRID]] SA00")
                                 << int (Profile::None);
    QTest::newRow ("grid-digit") << QStringLiteral ("[[JTTY:GRID]] FN4A")
                                 << int (Profile::None);
    QTest::newRow ("control") << QStringLiteral ("[[JTTY:CONTROL]] THANKS")
                              << int (Profile::None);
  }

  void payloadValidation ()
  {
    QFETCH (QString, message);
    QFETCH (int, profile);
    auto const result = Jtty::compileN1mmMessage (
        message, static_cast<Profile> (profile));
    QCOMPARE (result.status, Status::Error);
    QVERIFY (!result.error.isEmpty ());
    QVERIFY (result.atoms.isEmpty ());
  }

  void contextOverloadUsesSelectedProfile ()
  {
    Jtty::NativeMacroContext context;
    context.exchangeProfile = Profile::FieldDay;
    auto const result = Jtty::compileN1mmMessage (
        QStringLiteral ("[[JTTY:EXCH]] 1D EMA"), context);
    QCOMPARE (result.status, Status::Native);
    QCOMPARE (int {result.atoms.first ().kind}, static_cast<int> (Kind::ExchangePair));
  }
};

QTEST_GUILESS_MAIN (TestJttyN1mm)
#include "test_jtty_n1mm.moc"
