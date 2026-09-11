#include <QtTest>

#include "widgets/JttyMessages.hpp"

class TestJttyMessages final
  : public QObject
{
  Q_OBJECT

private slots:
  void nativeMacroMappings ()
  {
    Jtty::NativeMacroContext const context {QString {"k1abc"}, QString {"w9xyz"}, 7};

    struct Expected
    {
      QString text;
      QVector<int> kinds;
      QVector<int> subtypes;
      QVector<QString> calls;
    };
    QVector<Expected> const expected {
      {QString {"CQ K1ABC CQ"}, {0}, {0}, {QString {"K1ABC"}}},
      {QString {"W9XYZ 599 007"}, {0, 1}, {1, 0}, {QString {"W9XYZ"}, QString {}}},
      {QString {"W9XYZ TU CQ K1ABC CQ"}, {0, 0}, {3, 0},
       {QString {"W9XYZ"}, QString {"K1ABC"}}},
      {QString {"K1ABC"}, {0}, {1}, {QString {"K1ABC"}}},
      {QString {"W9XYZ"}, {0}, {1}, {QString {"W9XYZ"}}},
      {QString {"TU NOW W9XYZ 599 007"}, {0, 1}, {5, 0},
       {QString {"W9XYZ"}, QString {}}},
      {QString {"W9XYZ AGN?"}, {0}, {4}, {QString {"W9XYZ"}}},
      {QString {"599 007"}, {1}, {0}, {QString {}}}
    };

    for (int functionKey = 1; functionKey <= 8; ++functionKey) {
      auto const compiled = Jtty::compileNativeMacro (
          functionKey, Jtty::nativeMacroTemplate (functionKey), context);
      auto const& wanted = expected.at (functionKey - 1);
      QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
      QCOMPARE (compiled.text, wanted.text);
      QCOMPARE (compiled.atoms.size (), wanted.kinds.size ());
      for (int i = 0; i < compiled.atoms.size (); ++i) {
        auto const& atom = compiled.atoms.at (i);
        QCOMPARE (int {atom.kind}, wanted.kinds.at (i));
        QCOMPARE (int {atom.subtype}, wanted.subtypes.at (i));
        QCOMPARE (int {atom.reserved}, 0);
        QCOMPARE (QString::fromLatin1 (atom.text), wanted.calls.at (i));
        if (atom.kind == static_cast<qint8> (Jtty::NativeAtomKind::ExchangeNumber)) {
          QCOMPARE (int {atom.role}, static_cast<int> (Jtty::ExchangeRole::Full));
          QCOMPARE (atom.value, qint32 {7});
        } else {
          QCOMPARE (int {atom.role}, 0);
          QCOMPARE (atom.value, qint32 {0});
        }
      }
    }
  }

  void nativeMacroMatchesCaseAndWhitespaceBeforeExpansion ()
  {
    Jtty::NativeMacroContext const context {QString {" k1abc "}, QString {" w9xyz "}, 42};
    auto const compiled = Jtty::compileNativeMacro (
        3, QString {" \t%h   tu\n cq  %m cq  "}, context);

    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (compiled.text, QString {"W9XYZ TU CQ K1ABC CQ"});
    QCOMPARE (compiled.atoms.size (), 2);
    QCOMPARE (QString::fromLatin1 (compiled.atoms.at (0).text), QString {"W9XYZ"});
    QCOMPARE (QString::fromLatin1 (compiled.atoms.at (1).text), QString {"K1ABC"});
  }

  void customizedMacroFallsBackToExpandedLiteral ()
  {
    Jtty::NativeMacroContext const context {QString {"K1ABC"}, QString {"W9XYZ"}, 7};
    auto const customized = Jtty::compileNativeMacro (
        2, QString {"TEST %H 599 %N"}, context);
    QCOMPARE (customized.status, Jtty::NativeMacroStatus::LiteralFallback);
    QCOMPARE (customized.text, QString {"TEST W9XYZ 599 007"});
    QVERIFY (customized.atoms.isEmpty ());

    auto const movedDefault = Jtty::compileNativeMacro (
        4, QString {"%H"}, context);
    QCOMPARE (movedDefault.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (movedDefault.text, QString {"W9XYZ"});
    QCOMPARE (movedDefault.atoms.size (), 1);
    QCOMPARE (int {movedDefault.atoms.at (0).subtype},
              static_cast<int> (Jtty::CallAction::Call));

    auto const unknownPlaceholder = Jtty::compileNativeMacro (
        8, QString {"599 %R"}, context);
    QCOMPARE (unknownPlaceholder.status, Jtty::NativeMacroStatus::LiteralFallback);
    QCOMPARE (unknownPlaceholder.text, QString {"599 %R"});

    auto const editedLegacyCase = Jtty::compileNativeMacro (
        2, QString {"%h 599 %n"}, context);
    QCOMPARE (editedLegacyCase.status, Jtty::NativeMacroStatus::LiteralFallback);
    QCOMPARE (editedLegacyCase.text, QString {"%h 599 %n"});

    auto const editedLegacySpacing = Jtty::compileNativeMacro (
        8, QString {" 599 %N "}, context);
    QCOMPARE (editedLegacySpacing.status, Jtty::NativeMacroStatus::LiteralFallback);
    QCOMPARE (editedLegacySpacing.text, QString {" 599 007 "});
  }

  void blankMacroDoesNotSelectNativeCq ()
  {
    Jtty::NativeMacroContext const context {QString {"K1ABC"}, QString {"W9XYZ"}, 7};
    for (int functionKey = 1; functionKey <= 8; ++functionKey) {
      for (QString const& macro : {QString {}, QString {" \t "}}) {
        auto const compiled = Jtty::compileNativeMacro (functionKey, macro, context);
        QCOMPARE (compiled.status, Jtty::NativeMacroStatus::LiteralFallback);
        QVERIFY (compiled.atoms.isEmpty ());
        QVERIFY (compiled.text.trimmed ().isEmpty ());
      }
    }
  }

  void nativeCallDescriptorTextIsBounded ()
  {
    auto const atom = Jtty::nativeCallAtom (
      Jtty::CallAction::Call, QStringLiteral ("K1ABCDEFGHIJK"));
    QCOMPARE (QString::fromLatin1 (atom.text), QStringLiteral ("K1ABCDEF"));
  }

  void recognizedMacroRejectsInvalidRuntime_data ()
  {
    QTest::addColumn<int> ("functionKey");
    QTest::addColumn<QString> ("myCall");
    QTest::addColumn<QString> ("hisCall");
    QTest::addColumn<int> ("serialNumber");

    QTest::newRow ("missing-my-call") << 1 << QString {} << QString {"W9XYZ"} << 1;
    QTest::newRow ("missing-his-call") << 2 << QString {"K1ABC"} << QString {} << 1;
    QTest::newRow ("call-without-area") << 4 << QString {"KABC"} << QString {"W9XYZ"} << 1;
    QTest::newRow ("call-without-suffix") << 5 << QString {"K1ABC"} << QString {"W9"} << 1;
    QTest::newRow ("q-prefix") << 7 << QString {"K1ABC"} << QString {"Q1ABC"} << 1;
    QTest::newRow ("portable-call") << 3 << QString {"K1ABC/P"} << QString {"W9XYZ"} << 1;
    QTest::newRow ("negative-serial") << 8 << QString {"K1ABC"} << QString {"W9XYZ"} << -1;
    QTest::newRow ("serial-overflow") << 6 << QString {"K1ABC"} << QString {"W9XYZ"}
                                              << (1 << 17);
  }

  void recognizedMacroRejectsInvalidRuntime ()
  {
    QFETCH (int, functionKey);
    QFETCH (QString, myCall);
    QFETCH (QString, hisCall);
    QFETCH (int, serialNumber);
    Jtty::NativeMacroContext const context {myCall, hisCall, serialNumber};

    auto const compiled = Jtty::compileNativeMacro (
        functionKey, Jtty::nativeMacroTemplate (functionKey), context);

    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::InvalidRuntime);
    QVERIFY (compiled.atoms.isEmpty ());
    QVERIFY (compiled.text.isEmpty ());
  }

  void nativeRuntimeBoundariesAndUnusedFields ()
  {
    auto const minimum = Jtty::compileNativeMacro (
        8, Jtty::nativeMacroTemplate (8), {QString {}, QString {}, 0});
    QCOMPARE (minimum.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (minimum.text, QString {"599 000"});
    QCOMPARE (minimum.atoms.at (0).value, qint32 {0});

    auto const maximum = Jtty::compileNativeMacro (
        8, Jtty::nativeMacroTemplate (8), {QString {}, QString {}, (1 << 17) - 1});
    QCOMPARE (maximum.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (maximum.text, QString {"599 131071"});
    QCOMPARE (maximum.atoms.at (0).value, qint32 {(1 << 17) - 1});

    auto const unusedInvalidFields = Jtty::compileNativeMacro (
        1, Jtty::nativeMacroTemplate (1), {QString {"K1ABC"}, QString {}, -1});
    QCOMPARE (unusedInvalidFields.status, Jtty::NativeMacroStatus::Native);
  }

  void functionKeyApiIsParityFriendly ()
  {
    Jtty::NativeMacroContext const context {QString {"K1ABC"}, QString {"W9XYZ"}, 123};
    for (int functionKey = 1; functionKey <= 8; ++functionKey) {
      QString const configuredText = Jtty::nativeMacroTemplate (functionKey);
      auto const keyboard = Jtty::compileNativeMacro (functionKey, configuredText, context);
      auto const button = Jtty::compileNativeMacro (functionKey, configuredText, context);
      QCOMPARE (keyboard.status, button.status);
      QCOMPARE (keyboard.text, button.text);
      QCOMPARE (keyboard.atoms.size (), button.atoms.size ());
      for (int i = 0; i < keyboard.atoms.size (); ++i) {
        QCOMPARE (keyboard.atoms.at (i).kind, button.atoms.at (i).kind);
        QCOMPARE (keyboard.atoms.at (i).subtype, button.atoms.at (i).subtype);
        QCOMPARE (keyboard.atoms.at (i).role, button.atoms.at (i).role);
        QCOMPARE (keyboard.atoms.at (i).value, button.atoms.at (i).value);
        QCOMPARE (QString::fromLatin1 (keyboard.atoms.at (i).text),
                  QString::fromLatin1 (button.atoms.at (i).text));
      }
    }
  }

  void legacyAndProfileAwareTemplatesRemainNative ()
  {
    Jtty::NativeMacroContext const context {QString {"K1ABC"}, QString {"W9XYZ"}, 12};
    for (int functionKey = 1; functionKey <= 8; ++functionKey) {
      auto const current = Jtty::compileNativeMacro (
        functionKey, Jtty::nativeMacroTemplate (functionKey), context);
      auto const legacy = Jtty::compileNativeMacro (
        functionKey, Jtty::legacyNativeMacroTemplate (functionKey), context);
      QCOMPARE (current.status, Jtty::NativeMacroStatus::Native);
      QCOMPARE (legacy.status, Jtty::NativeMacroStatus::Native);
      QCOMPARE (current.text, legacy.text);
      QCOMPARE (current.atoms.size (), legacy.atoms.size ());
    }

    QCOMPARE (Jtty::nativeMacroTemplate (2), QString {"%H %E"});
    QCOMPARE (Jtty::nativeMacroTemplate (6), QString {"TU NOW %Q %E"});
    QCOMPARE (Jtty::nativeMacroTemplate (8), QString {"%E"});
  }

  void savedDefaultMigrationIsExactAndNarrow ()
  {
    for (int functionKey : {2, 6, 8}) {
      QCOMPARE (Jtty::migratedNativeMacroTemplate (
                  functionKey, Jtty::legacyNativeMacroTemplate (functionKey)),
                Jtty::nativeMacroTemplate (functionKey));
    }

    QCOMPARE (Jtty::migratedNativeMacroTemplate (1, QString {"CQ %M CQ"}),
              QString {"CQ %M CQ"});
    QCOMPARE (Jtty::migratedNativeMacroTemplate (2, QString {"%h 599 %n"}),
              QString {"%h 599 %n"});
    QCOMPARE (Jtty::migratedNativeMacroTemplate (8, QString {" 599 %N "}),
              QString {" 599 %N "});
    QCOMPARE (Jtty::migratedNativeMacroTemplate (8, QString {"TEST 599 %N"}),
              QString {"TEST 599 %N"});
  }

  void noneProfileUsesLiveSerial ()
  {
    Jtty::NativeMacroContext const context {
      QString {"K1ABC"}, QString {"W9XYZ"}, 7, Jtty::NativeExchangeProfile::None};
    auto const compiled = Jtty::compileNativeMacro (QString {"%H %E"}, context);

    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (compiled.text, QString {"W9XYZ 599 007"});
    QCOMPARE (compiled.atoms.size (), 2);
    QCOMPARE (int {compiled.atoms.at (1).kind},
              static_cast<int> (Jtty::NativeAtomKind::ExchangeNumber));
    QCOMPARE (int {compiled.atoms.at (1).subtype},
              static_cast<int> (Jtty::NumberKind::Serial));
    QCOMPARE (int {compiled.atoms.at (1).role},
              static_cast<int> (Jtty::ExchangeRole::Full));
    QCOMPARE (compiled.atoms.at (1).value, qint32 {7});
  }

  void fieldDayProfileParsesClassAndSection_data ()
  {
    QTest::addColumn<QString> ("exchange");
    QTest::addColumn<QString> ("text");
    QTest::addColumn<int> ("count");
    QTest::addColumn<int> ("classIndex");
    QTest::addColumn<QString> ("section");

    QTest::newRow ("minimum") << QString {"1A AB"} << QString {"1A AB"}
                               << 1 << 0 << QString {"AB"};
    QTest::newRow ("maximum") << QString {"32F EMA"} << QString {"32F EMA"}
                               << 32 << 5 << QString {"EMA"};
    QTest::newRow ("normalizes-case-and-spacing")
      << QString {" 2d   wma "} << QString {"2D WMA"} << 2 << 3 << QString {"WMA"};
  }

  void compactFieldDayConfigurationIsNormalized ()
  {
    QCOMPARE (Jtty::normalizedFieldDayExchange (QStringLiteral ("3AOR")),
              QStringLiteral ("3A OR"));
    QCOMPARE (Jtty::normalizedFieldDayExchange (QStringLiteral (" 32fema ")),
              QStringLiteral ("32F EMA"));
    QCOMPARE (Jtty::normalizedFieldDayExchange (QStringLiteral ("3A OR")),
              QStringLiteral ("3A OR"));

    Jtty::NativeMacroContext context;
    context.exchangeProfile = Jtty::NativeExchangeProfile::FieldDay;
    context.configuredExchange = Jtty::normalizedFieldDayExchange (
      QStringLiteral ("3AOR"));
    auto const compiled = Jtty::compileNativeMacro (QStringLiteral ("%E"), context);
    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (compiled.text, QStringLiteral ("3A OR"));
  }

  void fieldDayProfileParsesClassAndSection ()
  {
    QFETCH (QString, exchange);
    QFETCH (QString, text);
    QFETCH (int, count);
    QFETCH (int, classIndex);
    QFETCH (QString, section);
    Jtty::NativeMacroContext const context {
      QString {}, QString {}, 1, Jtty::NativeExchangeProfile::FieldDay, exchange};
    auto const compiled = Jtty::compileNativeMacro (QString {"%E"}, context);

    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (compiled.text, text);
    QCOMPARE (compiled.atoms.size (), 1);
    auto const& atom = compiled.atoms.constFirst ();
    QCOMPARE (int {atom.kind}, static_cast<int> (Jtty::NativeAtomKind::ExchangePair));
    QCOMPARE (int {atom.subtype}, static_cast<int> (Jtty::PairSchema::ClassSection));
    QCOMPARE (int {atom.role}, classIndex);
    QCOMPARE (atom.value, qint32 {count});
    QCOMPARE (QString::fromLatin1 (atom.text), section);
  }

  void profileExchangeComposesWithCallActions ()
  {
    Jtty::NativeMacroContext const context {
      QString {"K1ABC"}, QString {"W9XYZ"}, 1,
      Jtty::NativeExchangeProfile::FieldDay, QString {"2D WMA"}};

    auto const reply = Jtty::compileNativeMacro (QString {"%H %E"}, context);
    QCOMPARE (reply.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (reply.text, QString {"W9XYZ 2D WMA"});
    QCOMPARE (reply.atoms.size (), 2);
    QCOMPARE (int {reply.atoms.at (0).kind}, static_cast<int> (Jtty::NativeAtomKind::Call));
    QCOMPARE (int {reply.atoms.at (1).kind},
              static_cast<int> (Jtty::NativeAtomKind::ExchangePair));

    auto const queued = Jtty::compileNativeMacro (QString {"TU NOW %Q %E"}, context);
    QCOMPARE (queued.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (queued.text, QString {"TU NOW W9XYZ 2D WMA"});
    QCOMPARE (queued.atoms.size (), 2);
    QCOMPARE (int {queued.atoms.at (0).subtype},
              static_cast<int> (Jtty::CallAction::TuNowCall));
    QCOMPARE (int {queued.atoms.at (1).kind},
              static_cast<int> (Jtty::NativeAtomKind::ExchangePair));
  }

  void invalidFieldDayExchangeIsRejected_data ()
  {
    QTest::addColumn<QString> ("exchange");
    QTest::newRow ("missing") << QString {};
    QTest::newRow ("zero-count") << QString {"0A EMA"};
    QTest::newRow ("count-overflow") << QString {"33A EMA"};
    QTest::newRow ("bad-class") << QString {"1G EMA"};
    QTest::newRow ("short-section") << QString {"1A E"};
    QTest::newRow ("long-section") << QString {"1A WMAA"};
    QTest::newRow ("non-base36-section") << QString {"1A E-A"};
    QTest::newRow ("noncanonical-three-character-section") << QString {"1A 0AB"};
  }

  void invalidFieldDayExchangeIsRejected ()
  {
    QFETCH (QString, exchange);
    Jtty::NativeMacroContext const context {
      QString {}, QString {}, 1, Jtty::NativeExchangeProfile::FieldDay, exchange};
    auto const compiled = Jtty::compileNativeMacro (QString {"%E"}, context);
    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::InvalidRuntime);
    QVERIFY (!compiled.error.isEmpty ());
    QVERIFY (compiled.atoms.isEmpty ());
  }

  void rttyProfileParsesConfiguredExchange_data ()
  {
    QTest::addColumn<QString> ("exchange");
    QTest::addColumn<int> ("serial");
    QTest::addColumn<int> ("kind");
    QTest::addColumn<int> ("value");
    QTest::addColumn<QString> ("atomText");
    QTest::addColumn<QString> ("rendered");

    QTest::newRow ("dx-live-serial") << QString {"DX"} << 42
      << static_cast<int> (Jtty::NativeAtomKind::ExchangeNumber) << 42 << QString {}
      << QString {"599 042"};
    QTest::newRow ("hash-live-serial") << QString {"#"} << 1234
      << static_cast<int> (Jtty::NativeAtomKind::ExchangeNumber) << 1234 << QString {}
      << QString {"599 1234"};
    QTest::newRow ("decimal-serial") << QString {"0013"} << 99
      << static_cast<int> (Jtty::NativeAtomKind::ExchangeNumber) << 13 << QString {}
      << QString {"599 013"};
    QTest::newRow ("state") << QString {" ma "} << 99
      << static_cast<int> (Jtty::NativeAtomKind::ExchangeLocation) << 0 << QString {"MA"}
      << QString {"599 MA"};
    QTest::newRow ("province") << QString {"nwt"} << 99
      << static_cast<int> (Jtty::NativeAtomKind::ExchangeLocation) << 0 << QString {"NWT"}
      << QString {"599 NWT"};
  }

  void rttyProfileParsesConfiguredExchange ()
  {
    QFETCH (QString, exchange);
    QFETCH (int, serial);
    QFETCH (int, kind);
    QFETCH (int, value);
    QFETCH (QString, atomText);
    QFETCH (QString, rendered);
    Jtty::NativeMacroContext const context {
      QString {}, QString {}, serial, Jtty::NativeExchangeProfile::RttyRoundup, exchange};
    auto const compiled = Jtty::compileNativeMacro (QString {"%E"}, context);

    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (compiled.text, rendered);
    auto const& atom = compiled.atoms.constFirst ();
    QCOMPARE (int {atom.kind}, kind);
    QCOMPARE (atom.value, qint32 {value});
    QCOMPARE (QString::fromLatin1 (atom.text), atomText);
    if (kind == static_cast<int> (Jtty::NativeAtomKind::ExchangeLocation)) {
      QCOMPARE (int {atom.subtype}, static_cast<int> (Jtty::LocationKind::StateProvince));
      QCOMPARE (int {atom.role}, static_cast<int> (Jtty::ExchangeRole::Full));
    }
  }

  void invalidRttyExchangeIsRejected_data ()
  {
    QTest::addColumn<QString> ("exchange");
    QTest::newRow ("empty") << QString {};
    QTest::newRow ("one-character") << QString {"M"};
    QTest::newRow ("too-long") << QString {"NWTX"};
    QTest::newRow ("punctuation") << QString {"M-A"};
    QTest::newRow ("noncanonical-three-character-token") << QString {"0MA"};
    QTest::newRow ("decimal-overflow") << QString {"131072"};
  }

  void invalidRttyExchangeIsRejected ()
  {
    QFETCH (QString, exchange);
    Jtty::NativeMacroContext const context {
      QString {}, QString {}, 7, Jtty::NativeExchangeProfile::RttyRoundup, exchange};
    auto const compiled = Jtty::compileNativeMacro (QString {"%E"}, context);
    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::InvalidRuntime);
    QVERIFY (!compiled.error.isEmpty ());
  }

  void gridMacroUsesFieldOnlyAndFullRoles_data ()
  {
    QTest::addColumn<QString> ("macro");
    QTest::addColumn<QString> ("grid");
    QTest::addColumn<int> ("role");
    QTest::addColumn<QString> ("text");
    QTest::newRow ("field-only-lower-bound") << QString {"%G"} << QString {"aa00"}
      << static_cast<int> (Jtty::ExchangeRole::FieldOnly) << QString {"AA00"};
    QTest::newRow ("field-only-upper-bound") << QString {"%G"} << QString {"RR99"}
      << static_cast<int> (Jtty::ExchangeRole::FieldOnly) << QString {"RR99"};
    QTest::newRow ("full") << QString {"599 %G"} << QString {"FN42"}
      << static_cast<int> (Jtty::ExchangeRole::Full) << QString {"599 FN42"};
  }

  void gridMacroUsesFieldOnlyAndFullRoles ()
  {
    QFETCH (QString, macro);
    QFETCH (QString, grid);
    QFETCH (int, role);
    QFETCH (QString, text);
    Jtty::NativeMacroContext const context {
      QString {}, QString {}, 1, Jtty::NativeExchangeProfile::None, QString {}, grid};
    auto const compiled = Jtty::compileNativeMacro (macro, context);
    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (compiled.text, text);
    QCOMPARE (compiled.atoms.size (), 1);
    QCOMPARE (int {compiled.atoms.constFirst ().kind},
              static_cast<int> (Jtty::NativeAtomKind::Grid4));
    QCOMPARE (int {compiled.atoms.constFirst ().role}, role);
    QCOMPARE (QString::fromLatin1 (compiled.atoms.constFirst ().text), grid.toUpper ());
  }

  void callAndQueuedGridTemplatesKeepGridFieldOnly ()
  {
    Jtty::NativeMacroContext const context {
      QString {"K1ABC"}, QString {"W9XYZ"}, 1,
      Jtty::NativeExchangeProfile::None, QString {}, QString {"FN42"}};

    auto const call = Jtty::compileNativeMacro (QString {"%H %G"}, context);
    QCOMPARE (call.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (call.text, QString {"W9XYZ FN42"});
    QCOMPARE (call.atoms.size (), 2);
    QCOMPARE (int {call.atoms.at (1).kind}, static_cast<int> (Jtty::NativeAtomKind::Grid4));
    QCOMPARE (int {call.atoms.at (1).role},
              static_cast<int> (Jtty::ExchangeRole::FieldOnly));

    auto const queued = Jtty::compileNativeMacro (QString {"TU NOW %Q %G"}, context);
    QCOMPARE (queued.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (queued.text, QString {"TU NOW W9XYZ FN42"});
    QCOMPARE (queued.atoms.size (), 2);
    QCOMPARE (int {queued.atoms.at (1).role},
              static_cast<int> (Jtty::ExchangeRole::FieldOnly));
  }

  void configuredSubsquareProducesGrid4WithoutChangingLiteralExpansion ()
  {
    Jtty::NativeMacroContext const context {
      QString {"K1ABC"}, QString {"W9XYZ"}, 1,
      Jtty::NativeExchangeProfile::None, QString {}, QString {"fn42ab"}};
    auto const native = Jtty::compileNativeMacro (QString {"%H %G"}, context);
    QCOMPARE (native.status, Jtty::NativeMacroStatus::Native);
    QCOMPARE (native.text, QString {"W9XYZ FN42"});
    QCOMPARE (QString::fromLatin1 (native.atoms.at (1).text), QString {"FN42"});

    auto const literal = Jtty::compileNativeMacro (QString {"GRID %G"}, context);
    QCOMPARE (literal.status, Jtty::NativeMacroStatus::LiteralFallback);
    QCOMPARE (literal.text, QString {"GRID fn42ab"});
  }

  void invalidGridIsRejected_data ()
  {
    QTest::addColumn<QString> ("grid");
    QTest::newRow ("empty") << QString {};
    QTest::newRow ("short") << QString {"AA0"};
    QTest::newRow ("field-overflow") << QString {"AS00"};
    QTest::newRow ("square-letter") << QString {"AA0A"};
    QTest::newRow ("invalid-subsquare") << QString {"FN42AZ"};
  }

  void invalidGridIsRejected ()
  {
    QFETCH (QString, grid);
    Jtty::NativeMacroContext const context {
      QString {}, QString {}, 1, Jtty::NativeExchangeProfile::None, QString {}, grid};
    auto const compiled = Jtty::compileNativeMacro (QString {"%G"}, context);
    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::InvalidRuntime);
    QVERIFY (!compiled.error.isEmpty ());
  }

  void registeredControlPhrasesCompileNatively ()
  {
    Jtty::NativeMacroContext const context;
    for (int phraseId = 0; phraseId < 18; ++phraseId) {
      auto const compiled = Jtty::compileNativeMacro (
        QString {"  %1  "}.arg (Jtty::controlPhrase (phraseId).toLower ()), context);
      QCOMPARE (compiled.status, Jtty::NativeMacroStatus::Native);
      QCOMPARE (compiled.text, Jtty::controlPhrase (phraseId));
      QCOMPARE (compiled.atoms.size (), 1);
      QCOMPARE (int {compiled.atoms.constFirst ().kind},
                static_cast<int> (Jtty::NativeAtomKind::Control));
      QCOMPARE (int {compiled.atoms.constFirst ().subtype}, phraseId);
    }
  }

  void customLiteralExpandsProfileFields ()
  {
    Jtty::NativeMacroContext const context {
      QString {"K1ABC"}, QString {"W9XYZ"}, 7,
      Jtty::NativeExchangeProfile::RttyRoundup, QString {" dx "}, QString {"fn42"}};
    auto const compiled = Jtty::compileNativeMacro (QString {"TEST %E %G"}, context);
    QCOMPARE (compiled.status, Jtty::NativeMacroStatus::LiteralFallback);
    QCOMPARE (compiled.text, QString {"TEST 007 fn42"});
    QVERIFY (compiled.error.isEmpty ());
  }

  void prepareTransmitText_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::addColumn<QString> ("expected");
    QTest::addColumn<bool> ("substituted");
    QTest::addColumn<bool> ("truncated");

    QTest::newRow ("empty") << QString {} << QString {}
                            << false << false;
    QTest::newRow ("supported") << QString {"CQ KA1ABC CQ"} << QString {"CQ KA1ABC CQ"}
                                << false << false;
    QTest::newRow ("lowercase-preserved") << QString {"cq ka1abc cq"} << QString {"cq ka1abc cq"}
                                          << false << false;
    QTest::newRow ("tab") << QString {"HELLO\tWORLD"} << QString {"HELLO#WORLD"}
                          << true << false;
    QTest::newRow ("cr-lf") << QString {"HELLO\r\nWORLD"} << QString {"HELLO##WORLD"}
                            << true << false;
    QTest::newRow ("nul") << (QString {"A"} + QChar::Null + QString {"B"}) << QString {"A B"}
                          << true << false;
    QTest::newRow ("display-space-marker") << QString {"A~B"} << QString {"A B"}
                                           << true << false;
    QTest::newRow ("exactly-80") << QString (80, QLatin1Char {'A'}) << QString (80, QLatin1Char {'A'})
                                 << false << false;
    QTest::newRow ("truncated") << QString (81, QLatin1Char {'A'}) << QString (80, QLatin1Char {'A'})
                                << false << true;
    QTest::newRow ("unsupported-past-limit")
        << (QString (80, QLatin1Char {'A'}) + QString {"\t"})
        << QString (80, QLatin1Char {'A'})
        << false << true;
    QTest::newRow ("substitution-and-truncation")
        << (QString (79, QLatin1Char {'A'}) + QString {"\tB"})
        << (QString (79, QLatin1Char {'A'}) + QString {"#"})
        << true << true;
  }

  void prepareTransmitText ()
  {
    QFETCH (QString, message);
    QFETCH (QString, expected);
    QFETCH (bool, substituted);
    QFETCH (bool, truncated);

    auto const prepared = Jtty::prepareTransmitText (message);

    QCOMPARE (prepared.text, expected);
    QCOMPARE (prepared.substituted, substituted);
    QCOMPARE (prepared.truncated, truncated);
    QCOMPARE (prepared.changed (), substituted || truncated);
  }

  void withChainedSpacing_data ()
  {
    QTest::addColumn<QString> ("message");
    QTest::addColumn<bool> ("isChained");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("not-chained-unchanged") << QString {"CQ KA1ABC CQ"} << false
                                            << QString {"CQ KA1ABC CQ"};
    QTest::newRow ("chained-gets-leading-space") << QString {"TU DE KA1ABC"} << true
                                                 << QString {" TU DE KA1ABC"};
    QTest::newRow ("not-chained-empty-stays-empty") << QString {} << false << QString {};
    QTest::newRow ("chained-at-max-length-drops-last-char")
        << QString (Jtty::maxTransmitLength, QLatin1Char {'A'}) << true
        << (QString {" "} + QString (Jtty::maxTransmitLength - 1, QLatin1Char {'A'}));
  }

  void withChainedSpacing ()
  {
    QFETCH (QString, message);
    QFETCH (bool, isChained);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::withChainedSpacing (message, isChained), expected);
  }

  void formatSerialNumber_data ()
  {
    QTest::addColumn<int> ("serialNumber");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("minimum") << 1 << QString {"001"};
    QTest::newRow ("single-digit") << 5 << QString {"005"};
    QTest::newRow ("two-digit") << 57 << QString {"057"};
    QTest::newRow ("three-digit") << 101 << QString {"101"};
    QTest::newRow ("four-digit") << 1234 << QString {"1234"};
    QTest::newRow ("spinbox-maximum") << 5000 << QString {"5000"};
  }

  void formatSerialNumber ()
  {
    QFETCH (int, serialNumber);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::formatSerialNumber (serialNumber), expected);
  }

  void jttyLineTimeLabel_data ()
  {
    QTest::addColumn<QDateTime> ("diskDateTime");
    QTest::addColumn<qint32> ("utcDiskRaw");
    QTest::addColumn<float> ("tsyncSeconds");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("real-date-anchors-tsync")
        << QDateTime {QDate {2026, 8, 28}, QTime {19, 45, 7}, Qt::UTC}
        << qint32 {194507} << 3.0f << QString {"194510"};
    QTest::newRow ("real-date-wraps-past-midnight")
        << QDateTime {QDate {2026, 8, 28}, QTime {23, 59, 58}, Qt::UTC}
        << qint32 {235958} << 5.0f << QString {"000003"};
    QTest::newRow ("subsecond-offset-preserves-current-second")
        << QDateTime {QDate {2026, 8, 28}, QTime {19, 45, 7}, Qt::UTC}
        << qint32 {194507} << 0.6f << QString {"194507"};
    QTest::newRow ("dummy-date-falls-back-to-raw-digits")
        << QDateTime {} << qint32 {2} << 0.0f << QString {"000002"};
    QTest::newRow ("dummy-date-fallback-still-anchors-tsync")
        << QDateTime {} << qint32 {2} << 5.0f << QString {"000007"};
    QTest::newRow ("neither-anchor-usable")
        << QDateTime {} << qint32 {999999} << 0.0f << QString {};
  }

  void jttyLineTimeLabel ()
  {
    QFETCH (QDateTime, diskDateTime);
    QFETCH (qint32, utcDiskRaw);
    QFETCH (float, tsyncSeconds);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::jttyLineTimeLabel (diskDateTime, utcDiskRaw, tsyncSeconds), expected);
  }

  void jttyLineStartTimeUtc ()
  {
    QDateTime const diskDateTime {QDate {2026, 8, 28}, QTime {23, 59, 58}, Qt::UTC};
    QDateTime const expectedMidnight {QDate {2026, 8, 29}, QTime {0, 0, 3}, Qt::UTC};
    QDateTime const expectedFallback {QDate {2000, 1, 1}, QTime {0, 0, 7}, Qt::UTC};
    QCOMPARE (Jtty::jttyLineStartTimeUtc (diskDateTime, 235958, 5.0f), expectedMidnight);
    QCOMPARE (Jtty::jttyLineStartTimeUtc (QDateTime {}, 2, 5.0f), expectedFallback);
    QVERIFY (!Jtty::jttyLineStartTimeUtc (QDateTime {}, 999999, 0.0f).isValid ());
  }

  void jttyLineTimeLabelForStoredTimestamp ()
  {
    QCOMPARE (Jtty::jttyLineTimeLabel (
                QDateTime {QDate {2026, 8, 28}, QTime {19, 45, 7}, Qt::UTC}),
              QString {"194507"});
    QCOMPARE (Jtty::jttyLineTimeLabel (QDateTime {}), QString {});
  }

  void parseDecodeLine_data ()
  {
    QTest::addColumn<QString> ("line");
    QTest::addColumn<int> ("frequency");
    QTest::addColumn<QString> ("message");
    QTest::addColumn<bool> ("valid");

    QTest::newRow ("four-digit-frequency")
        << QString {"1500  CQ K1ABC"} << 1500 << QString {"CQ K1ABC"} << true;
    QTest::newRow ("three-digit-frequency-with-padding")
        << QString {" 500  599 K1ABC "} << 500 << QString {"599 K1ABC"} << true;
    QTest::newRow ("numeric-leading-message-is-preserved")
        << QString {"1500  599 K1ABC"} << 1500 << QString {"599 K1ABC"} << true;
    QTest::newRow ("frequency-only")
        << QString {"1500"} << 1500 << QString {} << true;
    QTest::newRow ("malformed-frequency")
        << QString {"CQ K1ABC"} << 0 << QString {"CQ K1ABC"} << false;
    QTest::newRow ("empty")
        << QString {} << 0 << QString {} << false;
  }

  void parseDecodeLine ()
  {
    QFETCH (QString, line);
    QFETCH (int, frequency);
    QFETCH (QString, message);
    QFETCH (bool, valid);

    auto const decoded = Jtty::parseDecodeLine (line);

    QCOMPARE (decoded.frequency, frequency);
    QCOMPARE (decoded.message, message);
    QCOMPARE (decoded.valid, valid);
  }

  void wrapMessage_data ()
  {
    QTest::addColumn<QString> ("text");
    QTest::addColumn<QString> ("expected");

    QTest::newRow ("short-unchanged") << QString {"HELLO"} << QString {"HELLO"};
    QTest::newRow ("exactly-40-unchanged")
        << QString (40, QLatin1Char {'A'}) << QString (40, QLatin1Char {'A'});
    QTest::newRow ("joes-example")
        << QString {"MAYBE CLAUDE COULD HELP US MAKE THE 3 MIN TRANSITION SEAMLESS"}
        << QString {"MAYBE CLAUDE COULD HELP US MAKE THE 3\n  MIN TRANSITION SEAMLESS"};
    QTest::newRow ("break-at-boundary")
        << (QString (39, QLatin1Char {'A'}) + QString {" B"})
        << (QString (39, QLatin1Char {'A'}) + QString {"\n  B"});
    QTest::newRow ("hard-break-no-blank")
        << QString (50, QLatin1Char {'A'})
        << (QString (40, QLatin1Char {'A'}) + QString {"\n  "}
            + QString (10, QLatin1Char {'A'}));
    QTest::newRow ("leading-space-does-not-create-empty-line")
        << (QString {" "} + QString (79, QLatin1Char {'A'}))
        << (QString {" "} + QString (39, QLatin1Char {'A'}) + QString {"\n  "}
            + QString (40, QLatin1Char {'A'}));
    QTest::newRow ("multiple-wrap-points")
        << QString {"AAAAAAAAAA BBBBBBBBBB CCCCCCCCCC DDDDDDDDDD EEEEEEEEEE FFFFFFFFFF GGGGGGGGGG"}
        << QString {"AAAAAAAAAA BBBBBBBBBB CCCCCCCCCC\n  DDDDDDDDDD EEEEEEEEEE FFFFFFFFFF\n  GGGGGGGGGG"};
  }

  void wrapMessage ()
  {
    QFETCH (QString, text);
    QFETCH (QString, expected);

    QCOMPARE (Jtty::wrapMessage (text), expected);
  }

  void compareMessagesTracksMessageSemantics ()
  {
    auto const unchanged = Jtty::compareMessages (
        QStringLiteral ("CQ K1ABC"), QStringLiteral ("CQ K1ABC"));
    QVERIFY (!unchanged.messageChanged);
    QVERIFY (!unchanged.extendsMessage);
    QVERIFY (!unchanged.startsMessage);
    QVERIFY (unchanged.appendedText.isEmpty ());

    auto const growth = Jtty::compareMessages (
        QStringLiteral ("CQ K1"), QStringLiteral ("CQ K1ABC"));
    QVERIFY (growth.messageChanged);
    QVERIFY (growth.extendsMessage);
    QVERIFY (!growth.startsMessage);
    QCOMPARE (growth.appendedText, QStringLiteral ("ABC"));

    auto const replacement = Jtty::compareMessages (
        QStringLiteral ("FIRST"), QStringLiteral ("SECOND"));
    QVERIFY (replacement.messageChanged);
    QVERIFY (!replacement.extendsMessage);
    QVERIFY (!replacement.startsMessage);
    QVERIFY (replacement.appendedText.isEmpty ());

    auto const textAfterEmptyFrame = Jtty::compareMessages (
        QString {}, QStringLiteral ("ABC"));
    QVERIFY (textAfterEmptyFrame.messageChanged);
    QVERIFY (textAfterEmptyFrame.extendsMessage);
    QVERIFY (textAfterEmptyFrame.startsMessage);
    QCOMPARE (textAfterEmptyFrame.appendedText, QStringLiteral ("ABC"));
  }

  void messageUpdatesDoNotRemoveHistory ()
  {
    QVector<Jtty::MessageUpdate> history {
      {1, 1000.f, QStringLiteral ("FIRST"), 1.f, false},
      {2, 1100.f, QStringLiteral ("SECOND"), 2.f, false},
    };
    QVector<Jtty::MessageUpdate> updates {
      {2, 1100.f, QStringLiteral ("SECOND"), 2.f, false},
    };

    QVERIFY (!Jtty::mergeMessageUpdates (history, updates));
    QCOMPARE (history.size (), 2);
    QCOMPARE (history.at (0).messageId, qint64 {1});
    QCOMPARE (history.at (1).messageId, qint64 {2});
  }

  void messageUpdatesReplaceSameId ()
  {
    QVector<Jtty::MessageUpdate> history {
      {8, 1400.f, QStringLiteral ("CQ K1"), 3.f, false},
    };
    QVector<Jtty::MessageUpdate> updates {
      {8, 1401.f, QStringLiteral ("CQ K1ABC"), 3.f, true},
    };

    QVERIFY (Jtty::mergeMessageUpdates (history, updates));
    QCOMPARE (history.size (), 1);
    QCOMPARE (history.at (0).frequency, 1401.f);
    QCOMPARE (history.at (0).text, QStringLiteral ("CQ K1ABC"));
    QVERIFY (history.at (0).complete);
  }

  void messageUpdatesPreserveCompletionAndInitialStart ()
  {
    QVector<Jtty::MessageUpdate> history {
      {12, 1600.f, QStringLiteral ("TEST"), 4.f, true},
    };
    QVector<Jtty::MessageUpdate> updates {
      {12, 1600.f, QStringLiteral ("TEST"), 9.f, false},
    };

    QVERIFY (!Jtty::mergeMessageUpdates (history, updates));
    QVERIFY (history.at (0).complete);
    QCOMPARE (history.at (0).sequenceStart, 4.f);
  }

  void messageUpdatesDistinguishIdenticalTextById ()
  {
    QVector<Jtty::MessageUpdate> history {
      {qint64 {1} << 35, 1600.f, QStringLiteral ("TEST"), 1.f, false},
    };
    QVector<Jtty::MessageUpdate> updates {
      {(qint64 {1} << 35) + 1, 1600.f, QStringLiteral ("TEST"), 2.f, false},
    };

    QVERIFY (Jtty::mergeMessageUpdates (history, updates));
    QCOMPARE (history.size (), 2);
    QVERIFY (history.at (0).messageId != history.at (1).messageId);
    QCOMPARE (history.at (0).text, history.at (1).text);
  }

  void messageUpdatesOrderByStartTimeThenId ()
  {
    QVector<Jtty::MessageUpdate> history;
    QVector<Jtty::MessageUpdate> updates {
      {9, 1000.f, QStringLiteral ("FOURTH"), 5.f, false},
      {4, 2000.f, QStringLiteral ("FIRST"), 1.f, false},
      {7, 900.f, QStringLiteral ("THIRD"), 3.f, false},
      {2, 1800.f, QStringLiteral ("SECOND"), 3.f, false},
    };

    QVERIFY (Jtty::mergeMessageUpdates (history, updates));
    QCOMPARE (history.size (), 4);
    QCOMPARE (history.at (0).messageId, qint64 {4});
    QCOMPARE (history.at (1).messageId, qint64 {2});
    QCOMPARE (history.at (2).messageId, qint64 {7});
    QCOMPARE (history.at (3).messageId, qint64 {9});
  }

  void qsoHistoryRetainsAdmittedMessageAcrossDrift ()
  {
    QVERIFY (Jtty::shouldApplyToQsoHistory (false, 1504.f, 1500.f, 5.f));
    QVERIFY (!Jtty::shouldApplyToQsoHistory (false, 1505.f, 1500.f, 5.f));
    QVERIFY (Jtty::shouldApplyToQsoHistory (true, 1510.f, 1500.f, 5.f));
  }
};

QTEST_MAIN (TestJttyMessages)
#include "test_jtty_messages.moc"
