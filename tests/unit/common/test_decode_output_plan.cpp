#include <QtTest>

#include "DecodeOutputPlan.hpp"
#include "Decoder/decodedtext.h"

class TestDecodeOutputPlan final
  : public QObject
{
  Q_OBJECT

  using ActionKind = DecodeOutputPlan::ActionKind;
  using SpecOp = SpecialOperatingActivity;

  enum FilterOption
  {
    InitiallyFiltered = 1 << 0,
    AlwaysPass = 1 << 1,
    Blacklist = 1 << 2,
    Whitelist = 1 << 3,
    Bypass = 1 << 4,
    WaitAndPounceOnly = 1 << 5,
    ScoringPounce = 1 << 6,
    NonScoringPounce = 1 << 7,
    HideIgnored = 1 << 8,
    HideWorkedToday = 1 << 9
  };

  enum FilterOutcome
  {
    KeywordFiltered = 1 << 0,
    FinallyFiltered = 1 << 1,
    ContinueBatch = 1 << 2,
    AlwaysPassed = 1 << 3,
    ResetPounceScores = 1 << 4,
    DisplayLeft = 1 << 5
  };

  static QString actionName(ActionKind kind)
  {
    switch (kind) {
    case ActionKind::ShowQsyMessage: return "qsy";
    case ActionKind::AccumulateFoxActivity: return "fox-activity";
    case ActionKind::IncrementDecodeCount: return "count";
    case ActionKind::WriteAll: return "all.txt";
    case ActionKind::UploadWsprSpot: return "wspr";
    case ActionKind::UpdateArrlActivity: return "arrl";
    case ActionKind::FlushFoxActivity: return "flush-fox";
    }
    return "unknown";
  }

  static QStringList actionNames(QVector<DecodeOutputPlan::Action> const& actions)
  {
    QStringList names;
    for (auto const& action : actions) names.append(actionName(action.kind));
    return names;
  }

  static DecodeOutputPlan::PreparationContext preparationContext()
  {
    DecodeOutputPlan::PreparationContext context;
    context.mode = "FT8";
    context.myCall = "K1ABC";
    context.qsyEnabled = true;
    return context;
  }

  static DecodeOutputPlan::RoutingContext routingContext()
  {
    DecodeOutputPlan::RoutingContext context;
    context.mode = "FT8";
    context.myCall = "K1ABC";
    context.baseCall = "K1ABC";
    context.hisCall = "W1AW";
    context.rxFrequency = 1500;
    context.wideGraphRxFrequency = 1500;
    return context;
  }

private slots:
  void wireFormatRegressionCorpus_data()
  {
    QTest::addColumn<QByteArray>("rawLine");
    QTest::addColumn<QByteArray>("expectedNormalizedLine");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("specOp");
    QTest::addColumn<bool>("activeStationsAvailable");
    QTest::addColumn<int>("expectedDisposition");
    QTest::addColumn<bool>("expectedAveraged");
    QTest::addColumn<int>("expectedAverageCount");
    QTest::addColumn<bool>("expectedFrequencySpread");
    QTest::addColumn<float>("expectedSpread");
    QTest::addColumn<bool>("expectedDecoded");
    QTest::addColumn<int>("expectedSingleDecodes");
    QTest::addColumn<int>("expectedAveragedDecodes");
    QTest::addColumn<QStringList>("expectedActions");

    auto legacyNormalized = [] (QByteArray line) {
      line.insert(qMin(44, line.size()), QByteArray(14, ' '));
      return line;
    };
    auto addRow = [] (char const * name, QByteArray const& rawLine,
                      QByteArray const& normalizedLine, QString const& mode,
                      SpecOp specOp, bool activeStationsAvailable,
                      DecodeOutputPlan::LineDisposition disposition,
                      bool averaged, int averageCount, bool haveSpread, float spread,
                      bool decoded, int singleDecodes, int averagedDecodes,
                      QStringList const& actions) {
      QTest::newRow(name) << rawLine << normalizedLine << mode << static_cast<int>(specOp)
                          << activeStationsAvailable << static_cast<int>(disposition)
                          << averaged << averageCount << haveSpread << spread << decoded
                          << singleDecodes << averagedDecodes << actions;
    };

    using Disposition = DecodeOutputPlan::LineDisposition;
    QStringList const decodeActions {"qsy", "count", "all.txt"};
    QStringList const finishActions {"qsy"};

    QByteArray const ft8 {
      "133430  -2 -0.8 1197 ~  CQ F5RXL IN94                           \r\n"};
    addRow("real FT8", ft8, ft8.left(ft8.size() - 2), "FT8", SpecOp::NONE, false,
           Disposition::Decode, false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const ft8Ap = QByteArray {"060500 -10  0.3 1500 ~  "}
      + QByteArray {"CQ W1AW FN31"}.leftJustified(36, ' ') + "? a7\n";
    addRow("FT8 AP low confidence", ft8Ap, ft8Ap.left(ft8Ap.size() - 1),
           "FT8", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const ft4 {
      "000002 -10 -0.2  296 +  N1TRK N4FKH 569 VA                      \n"};
    addRow("real FT4 contest", ft4, ft4.left(ft4.size() - 1),
           "FT4", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const jt9 {"1742  -5  1.2 1505 @  JA1KAU PD0JAC -23     \n"};
    auto jt9Normalized = jt9.left(jt9.size() - 1);
    addRow("real JT9", jt9, legacyNormalized(jt9Normalized),
           "JT9", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const jt65 = QByteArray {"2343 -11  0.8 1259 #  "}
      + QByteArray {"YV6BFE F6GUU R-08"}.leftJustified(22, ' ') + "    \n";
    auto jt65Normalized = jt65.left(jt65.size() - 1);
    addRow("ordinary JT65", jt65, legacyNormalized(jt65Normalized),
           "JT65", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const jt65Average = QByteArray {"2343 -18  0.3 1500 #  "}
      + QByteArray {"K1ABC W1AW R-10"}.leftJustified(22, ' ') + " d22\n";
    auto jt65AverageNormalized = jt65Average.left(jt65Average.size() - 1);
    addRow("averaged deep JT65", jt65Average, legacyNormalized(jt65AverageNormalized),
           "JT65", SpecOp::NONE, false, Disposition::Decode,
           true, 2, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const jt65NumericOnly {"2343 -20  0.3 1500\n"};
    auto jt65NumericOnlyNormalized = jt65NumericOnly.left(jt65NumericOnly.size() - 1);
    addRow("numeric-only JT65", jt65NumericOnly,
           legacyNormalized(jt65NumericOnlyNormalized),
           "JT65", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const q65 {
      "1621 -24  2.8  697 :  W7GJ N8JX EN73                        q0 \n"};
    addRow("real long-period Q65", q65, q65.left(q65.size() - 1),
           "Q65", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const q65Average = QByteArray {"060500 -20  0.3 1500 :  "}
      + QByteArray {"K1ABC W1AW R-10"}.leftJustified(37, ' ') + " q23\n";
    addRow("short-period averaged Q65", q65Average,
           q65Average.left(q65Average.size() - 1),
           "Q65", SpecOp::NONE, false, Disposition::Decode,
           true, 3, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const fst4Short = QByteArray {"060500 -18  0.3 1500 `  "}
      .append(QByteArray {"CQ W1AW FN31"}.leftJustified(37, ' '))
      .append("   ").leftJustified(70, ' ');
    addRow("short-period FST4", fst4Short + "\n", fst4Short,
           "FST4", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray fst4Spread = QByteArray {"0058  16  0.4 1331 `  "}
      .append(QByteArray {"CQ K9KFR EN71"}.leftJustified(37, ' '))
      .append("   ").leftJustified(70, ' ');
    fst4Spread.replace(64, 6, " 1.250");
    addRow("long-period FST4 spread", fst4Spread + "\n", fst4Spread.left(64),
           "FST4", SpecOp::NONE, false, Disposition::Decode,
           false, 0, true, 1.25f, false, 0, 0, decodeActions);

    QByteArray const fst4w = QByteArray {"0058 -20  0.4 1500 `  "}
      .append(QByteArray {"CQ W1AW FN31"}.leftJustified(37, ' '))
      .append("   ").leftJustified(70, ' ');
    addRow("accepted FST4W", fst4w + "\n", fst4w,
           "FST4W", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0,
           {"qsy", "count", "all.txt", "wspr"});

    QByteArray const msk144 {
      "120500   8  8.7 1488 &  K1JT WA4CQG EM72                                        \n"};
    auto msk144Normalized = msk144.left(msk144.size() - 1);
    addRow("real MSK144", msk144, legacyNormalized(msk144Normalized),
           "MSK144", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const fox = QByteArray {"060500 -10  0.3 1500 ~  "}
      + QByteArray {"K1ABC RR73; W9XYZ <KH1DX> -12"}.leftJustified(37, ' ') + "   \n";
    addRow("FOX dual reply", fox, fox.left(fox.size() - 1),
           "FT8", SpecOp::FOX, true, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0,
           {"qsy", "fox-activity", "count", "all.txt"});

    QByteArray const hound {"060500 -10  0.3 1500 ~  $VERIFY$ KH1DX 920749\n"};
    addRow("variable-length SuperFox Hound", hound, hound.left(hound.size() - 1),
           "FT8", SpecOp::HOUND, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const completion {"<DecodeFinished>   0  14        0\n"};
    addRow("ordinary completion", completion, completion.left(completion.size() - 1),
           "FT8", SpecOp::NONE, false, Disposition::Finish,
           false, 0, false, 0.0f, true, 0, 0, finishActions);

    QByteArray const q65Completion {"<DecodeFinished>   1   6     2003\n"};
    addRow("packed Q65 completion", q65Completion,
           q65Completion.left(q65Completion.size() - 1),
           "Q65", SpecOp::NONE, false, Disposition::Finish,
           false, 0, false, 0.0f, true, 2, 3, finishActions);

    QByteArray const truncatedRecord {"060500 -20  0.3 1500\n"};
    addRow("truncated decode record", truncatedRecord,
           truncatedRecord.left(truncatedRecord.size() - 1),
           "FT8", SpecOp::NONE, false, Disposition::Decode,
           false, 0, false, 0.0f, false, 0, 0, decodeActions);

    QByteArray const truncatedCompletion {"<DecodeFinished>\n"};
    addRow("truncated completion", truncatedCompletion,
           truncatedCompletion.left(truncatedCompletion.size() - 1),
           "FT8", SpecOp::NONE, false, Disposition::Finish,
           false, 0, false, 0.0f, false, 0, 0, finishActions);
  }

  void wireFormatRegressionCorpus()
  {
    QFETCH(QByteArray, rawLine);
    QFETCH(QByteArray, expectedNormalizedLine);
    QFETCH(QString, mode);
    QFETCH(int, specOp);
    QFETCH(bool, activeStationsAvailable);
    QFETCH(int, expectedDisposition);
    QFETCH(bool, expectedAveraged);
    QFETCH(int, expectedAverageCount);
    QFETCH(bool, expectedFrequencySpread);
    QFETCH(float, expectedSpread);
    QFETCH(bool, expectedDecoded);
    QFETCH(int, expectedSingleDecodes);
    QFETCH(int, expectedAveragedDecodes);
    QFETCH(QStringList, expectedActions);

    auto context = preparationContext();
    context.mode = mode;
    context.specOp = static_cast<SpecOp>(specOp);
    context.activeStationsAvailable = activeStationsAvailable;

    auto const plan = DecodeOutputPlan::prepareLine(rawLine, context);

    QCOMPARE(plan.rawLine, rawLine);
    QCOMPARE(plan.normalizedLine, expectedNormalizedLine);
    QCOMPARE(static_cast<int>(plan.disposition), expectedDisposition);
    QVERIFY(plan.reason.isEmpty());
    if (expectedDisposition == static_cast<int>(DecodeOutputPlan::LineDisposition::Decode)) {
      QCOMPARE(plan.logicMessage.string(), plan.decodedForLogic);
    } else {
      QVERIFY(plan.logicMessage.string().isEmpty());
    }
    QCOMPARE(plan.averaged, expectedAveraged);
    QCOMPARE(plan.averageCount, expectedAverageCount);
    QCOMPARE(plan.haveFrequencySpread, expectedFrequencySpread);
    QCOMPARE(plan.frequencySpread, expectedSpread);
    QCOMPARE(plan.batchHasDecodes, expectedDecoded);
    QCOMPARE(plan.q65SingleDecodes, expectedSingleDecodes);
    QCOMPARE(plan.q65AveragedDecodes, expectedAveragedDecodes);
    QCOMPARE(actionNames(plan.actions), expectedActions);
  }

  void validityRejectionMatrix_data()
  {
    QTest::addColumn<QByteArray>("rawLine");
    QTest::addColumn<bool>("reduceFalseDecodes");
    QTest::addColumn<int>("specOp");
    QTest::addColumn<bool>("diskData");
    QTest::addColumn<bool>("noA7Decodes");
    QTest::addColumn<int>("expectedDisposition");
    QTest::addColumn<QString>("expectedReason");
    QTest::addColumn<QStringList>("expectedActions");

    auto const decode = static_cast<int>(DecodeOutputPlan::LineDisposition::Decode);
    auto const ignore = static_cast<int>(DecodeOutputPlan::LineDisposition::Ignore);
    auto const none = static_cast<int>(SpecOp::NONE);
    auto const hound = static_cast<int>(SpecOp::HOUND);
    auto const fox = static_cast<int>(SpecOp::FOX);
    QStringList const acceptedActions {"qsy", "count", "all.txt"};
    QStringList const rejectedActions {"qsy"};

    auto addRow = [&] (char const * name, QByteArray const& line, bool reduce,
                       int operation, bool disk, bool suppressA7, int disposition,
                       QString const& reason, QStringList const& actions) {
      QTest::newRow(name) << line << reduce << operation << disk << suppressA7
                          << disposition << reason << actions;
    };

    QString const suspiciousReason {"suspicious low-SNR decode rejected"};
    addRow("two hashes below -12", "060500 -13  0.3 1500 ~  <W1AW> <K1ABC> R-10",
           false, none, false, false, ignore, suspiciousReason, rejectedActions);
    addRow("two portable calls below -12", "060500 -13  0.3 1500 ~  W1AW/P K1ABC/P R-10",
           false, none, false, false, ignore, suspiciousReason, rejectedActions);
    addRow("two rover calls below -12", "060500 -13  0.3 1500 ~  W1AW/R K1ABC/R R-10",
           false, none, false, false, ignore, suspiciousReason, rejectedActions);
    addRow("rover question below -12", "060500 -13  0.3 1500 ~  W1AW/R K1ABC ?",
           false, none, false, false, ignore, suspiciousReason, rejectedActions);
    addRow("semicolon report below -12", "060500 -13  0.3 1500 ~  W1AW; K1ABC R -10",
           false, none, false, false, ignore, suspiciousReason, rejectedActions);
    addRow("suspicious shape at -12", "060500 -12  0.3 1500 ~  <W1AW> <K1ABC> R-10",
           false, none, false, false, decode, {}, acceptedActions);

    QString const reduceReason {"reduce-false-decodes rule rejected line"};
    addRow("suspicious shape at -16 enabled", "060500 -16  0.3 1500 ~  ABCDEFGH K1ABC -10",
           true, none, false, false, ignore, reduceReason, rejectedActions);
    addRow("suspicious shape at -16 disabled", "060500 -16  0.3 1500 ~  ABCDEFGH K1ABC -10",
           false, none, false, false, decode, {}, acceptedActions);
    addRow("suspicious shape at -15", "060500 -15  0.3 1500 ~  ABCDEFGH K1ABC -10",
           true, none, false, false, decode, {}, acceptedActions);
    addRow("rover report at -18", "060500 -18  0.3 1500 ~  W1AW/R K1ABC -10",
           true, none, false, false, decode, {}, acceptedActions);
    addRow("rover report below -18", "060500 -19  0.3 1500 ~  W1AW/R K1ABC -10",
           true, none, false, false, ignore, reduceReason, rejectedActions);
    addRow("3-dot timing shape at -15", "060500 -15  3.1 1500 ~  CQ W1AW FN31",
           true, none, false, false, decode, {}, acceptedActions);
    addRow("2-dot timing shape below -15", "060500 -16  2.1 1500 ~  CQ W1AW FN31",
           true, none, false, false, ignore, reduceReason, rejectedActions);
    addRow("3-dot timing shape at -20", "060500 -20  3.1 1500 ~  CQ W1AW FN31",
           true, none, false, false, ignore, reduceReason, rejectedActions);
    addRow("2-dot timing shape below -20", "060500 -21  2.1 1500 ~  CQ W1AW FN31",
           true, none, false, false, ignore, reduceReason, rejectedActions);

    addRow("ordinary low-SNR CQ", "060500 -24  0.3 1500 ~  CQ W1AW FN31",
           true, none, false, false, decode, {}, acceptedActions);
    addRow("ordinary low-SNR direct report", "060500 -24  0.3 1500 ~  K1ABC W1AW -10",
           true, none, false, false, decode, {}, acceptedActions);
    addRow("hound bypasses suspicious low-SNR", "060500 -24  0.3 1500 ~  <W1AW> <K1ABC> R-10",
           false, hound, false, false, decode, {}, acceptedActions);
    addRow("hound bypasses reduce false decodes", "060500 -24  0.3 1500 ~  ABCDEFGH K1ABC -10",
           true, hound, false, false, decode, {}, acceptedActions);

    QString const a7Reason {"a7 decode rejected"};
    addRow("a7 suppression", "060500 -10  0.3 1500 ~  W1AW K1ABC -10 a7",
           false, none, false, true, ignore, a7Reason, rejectedActions);
    addRow("disk data bypasses a7 suppression", "060500 -10  0.3 1500 ~  W1AW K1ABC -10 a7",
           false, none, true, true, decode, {}, acceptedActions);
    addRow("special operation CQ a7 exemption", "060500 -10  0.3 1500 ~  CQ W1AW FN31 a7",
           false, fox, false, false, decode, {}, acceptedActions);
    addRow("special operation R a7 exemption", "060500 -10  0.3 1500 ~  W1AW K1ABC R -10 a7",
           false, fox, false, false, decode, {}, acceptedActions);
    addRow("special operation RR73 a7 exemption", "060500 -10  0.3 1500 ~  W1AW K1ABC RR73 a7",
           false, fox, false, false, decode, {}, acceptedActions);
  }

  void validityRejectionMatrix()
  {
    QFETCH(QByteArray, rawLine);
    QFETCH(bool, reduceFalseDecodes);
    QFETCH(int, specOp);
    QFETCH(bool, diskData);
    QFETCH(bool, noA7Decodes);
    QFETCH(int, expectedDisposition);
    QFETCH(QString, expectedReason);
    QFETCH(QStringList, expectedActions);

    auto context = preparationContext();
    context.reduceFalseDecodes = reduceFalseDecodes;
    context.specOp = static_cast<SpecOp>(specOp);
    context.diskData = diskData;
    context.noA7Decodes = noA7Decodes;

    auto const plan = DecodeOutputPlan::prepareLine(rawLine, context);

    QCOMPARE(static_cast<int>(plan.disposition), expectedDisposition);
    QCOMPARE(plan.reason, expectedReason);
    QCOMPARE(actionNames(plan.actions), expectedActions);
  }

  void preparationActionInvariants_data()
  {
    QTest::addColumn<QByteArray>("rawLine");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("specOp");
    QTest::addColumn<bool>("activeStationsAvailable");
    QTest::addColumn<bool>("noA7Decodes");
    QTest::addColumn<bool>("noOwnCall");
    QTest::addColumn<bool>("displayPoints");
    QTest::addColumn<int>("expectedDisposition");
    QTest::addColumn<QStringList>("expectedEarlyActions");
    QTest::addColumn<bool>("expectedUpdate");

    auto addRow = [] (char const * name, QByteArray const& rawLine, QString const& mode,
                      SpecOp specOp, bool activeStationsAvailable, bool noA7Decodes,
                      bool noOwnCall, bool displayPoints,
                      DecodeOutputPlan::LineDisposition disposition,
                      QStringList const& earlyActions, bool update = false) {
      QTest::newRow(name) << rawLine << mode << static_cast<int>(specOp)
                          << activeStationsAvailable << noA7Decodes << noOwnCall
                          << displayPoints << static_cast<int>(disposition)
                          << earlyActions << update;
    };

    using Disposition = DecodeOutputPlan::LineDisposition;
    addRow("ignored false decode", "060500 -13  0.3 1500 ~  <W1AW> <K1ABC> R-10\n",
           "FT8", SpecOp::NONE, false, false, false, false,
           Disposition::Ignore, {"qsy"});
    addRow("rejected FOX a7 retains early actions",
           "060500 -10  0.3 1500 ~  W1AW K1ABC -10 a7\n",
           "FT8", SpecOp::FOX, true, true, false, false,
           Disposition::Ignore, {"qsy", "fox-activity"});
    addRow("decode completion", "<DecodeFinished>   1   6     2003\n",
           "FT8", SpecOp::NONE, false, false, false, false,
           Disposition::Finish, {"qsy"});
    addRow("FST4W completion", "<DecodeFinished>   0   0        0\n",
           "FST4W", SpecOp::NONE, false, false, false, false,
           Disposition::Finish, {"qsy"});
    addRow("ignored FST4W own call", "060500 -10  0.3 1500 `  CQ K1ABC FN31\n",
           "FST4W", SpecOp::NONE, false, false, true, false,
           Disposition::Ignore, {"qsy"});
    addRow("accepted FST4W ordinary decode", "060500 -10  0.3 1500 `  CQ W1AW FN31\n",
           "FST4W", SpecOp::NONE, false, false, false, false,
           Disposition::Decode, {"qsy"});
    addRow("accepted FT8 display-points update", "060500 -10  0.3 1500 ~  CQ W1AW FN31\n",
           "FT8", SpecOp::NONE, false, false, false, true,
           Disposition::Decode, {"qsy"}, true);
  }

  void preparationActionInvariants()
  {
    QFETCH(QByteArray, rawLine);
    QFETCH(QString, mode);
    QFETCH(int, specOp);
    QFETCH(bool, activeStationsAvailable);
    QFETCH(bool, noA7Decodes);
    QFETCH(bool, noOwnCall);
    QFETCH(bool, displayPoints);
    QFETCH(int, expectedDisposition);
    QFETCH(QStringList, expectedEarlyActions);
    QFETCH(bool, expectedUpdate);

    auto context = preparationContext();
    context.mode = mode;
    context.specOp = static_cast<SpecOp>(specOp);
    context.activeStationsAvailable = activeStationsAvailable;
    context.noA7Decodes = noA7Decodes;
    context.noOwnCall = noOwnCall;
    context.displayPoints = displayPoints;

    auto const plan = DecodeOutputPlan::prepareLine(rawLine, context);
    auto const names = actionNames(plan.actions);
    QStringList earlyActions;
    for (auto const& action : plan.actions) {
      if (action.kind == ActionKind::ShowQsyMessage
          || action.kind == ActionKind::AccumulateFoxActivity) {
        earlyActions.append(actionName(action.kind));
        QCOMPARE(action.text, QString {rawLine});
      }
    }

    bool const accepted = expectedDisposition
      == static_cast<int>(DecodeOutputPlan::LineDisposition::Decode);
    QCOMPARE(static_cast<int>(plan.disposition), expectedDisposition);
    QCOMPARE(earlyActions, expectedEarlyActions);
    QCOMPARE(names.count("count"), accepted ? 1 : 0);
    QCOMPARE(names.count("all.txt"), accepted ? 1 : 0);
    QCOMPARE(names.count("wspr"), accepted && mode == "FST4W" ? 1 : 0);
    QCOMPARE(names.count("arrl"), expectedUpdate ? 1 : 0);
    QVERIFY(!names.contains("flush-fox"));
  }

  void retainsRawFoxActivityBeforeRejectedA7Decode()
  {
    auto context = preparationContext();
    context.specOp = SpecOp::FOX;
    context.activeStationsAvailable = true;
    context.noA7Decodes = true;
    QByteArray const rawLine {"060500 -20  0.3 1500 ~  CQ W1AW FN31 a7\n"};

    auto const plan = DecodeOutputPlan::prepareLine(rawLine, context);

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Ignore);
    QCOMPARE(plan.reason, QString {"a7 decode rejected"});
    QCOMPARE(actionNames(plan.actions), QStringList({"qsy", "fox-activity"}));
  }

  void displayPointsRemovesA7BeforeFalseDecodeClassification()
  {
    auto context = preparationContext();
    context.displayPoints = true;
    context.noA7Decodes = true;
    QByteArray const line {"060500 -20  0.3 1500 ~  CQ W1AW FN31 a7"};

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Decode);
    QVERIFY(!plan.normalizedLine.contains("a7"));
  }

  void wantedOnlyFoxActivityRetainsRawNewline()
  {
    auto context = preparationContext();
    context.specOp = SpecOp::FOX;
    context.activeStationsAvailable = true;
    context.activeStationsWantedOnly = true;
    QByteArray const line {"060500 -10  0.3 1500 ~  W1AW K1ABC -10\n"};

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QCOMPARE(actionNames(plan.actions).first(), QString {"qsy"});
    QCOMPARE(actionNames(plan.actions).at(1), QString {"fox-activity"});
    QCOMPARE(plan.actions.at(1).text, QString {line});
  }

  void ignoresFst4wOwnCallAfterQsyInspection()
  {
    auto context = preparationContext();
    context.mode = "FST4W";
    context.noOwnCall = true;
    QByteArray const rawLine {"060500 -10  0.3 1500 `  CQ K1ABC FN31\n"};

    auto const plan = DecodeOutputPlan::prepareLine(rawLine, context);

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Ignore);
    QCOMPARE(actionNames(plan.actions), QStringList({"qsy"}));
  }

  void legacyPaddingDoesNotChangeDecodedRepresentations()
  {
    auto context = preparationContext();
    context.mode = "JT65";
    QByteArray const line {
      "0605 -10  0.3 1500 #  TU; K1ABC W1AW R-10                    "};

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QCOMPARE(plan.normalizedLine.size(), line.size() + 14);
    QCOMPARE(plan.decodedOriginal, QString {line});
    QVERIFY(plan.decodedOriginal.contains("TU; "));
    QVERIFY(!plan.decodedForLogic.contains("TU; "));
    QCOMPARE(plan.logicMessage.string(), plan.decodedForLogic);
  }

  void rejectsEarlyDuplicateBeforeLogging()
  {
    auto context = preparationContext();
    context.nominalFrequency = 50313000;
    QByteArray const line {"060500 -20  0.3 1500 ~  CQ W1AW FN31"};
    context.earlyDecodes = QString::fromUtf8(line.mid(23, 19));

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Ignore);
    QCOMPARE(plan.reason, QString {"early duplicate rejected"});
    QCOMPARE(actionNames(plan.actions), QStringList({"qsy"}));
  }

  void shortLineIsNotAnEarlyDuplicate()
  {
    auto context = preparationContext();
    context.nominalFrequency = 50313000;
    context.earlyDecodes = "060500 -20  0.3 1500 ~  CQ W1AW FN31";
    QByteArray const line {"060500 -20  0.3 1500"};

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Decode);
  }

  void decodeFinishedIsDetectedBeforeDuplicateRejection()
  {
    auto context = preparationContext();
    context.nominalFrequency = 50313000;
    QByteArray const line {"<DecodeFinished>   1   6     2003"};
    context.earlyDecodes = QString::fromUtf8(line);

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Finish);
    QVERIFY(plan.batchHasDecodes);
  }

  void parsesAveragingMarkers_data()
  {
    QTest::addColumn<QByteArray>("line");
    QTest::addColumn<int>("count");
    QTest::newRow("f2") << QByteArray {"0605 -10  0.3 1500 #  K1ABC W1AW R-10 f2"} << 2;
    QTest::newRow("f-star") << QByteArray {"0605 -10  0.3 1500 #  K1ABC W1AW R-10 f*"} << 10;
    QTest::newRow("d2") << QByteArray {"0605 -10  0.3 1500 #  K1ABC W1AW R-10 d 2"} << 2;
    QTest::newRow("a2") << QByteArray {"0605 -10  0.3 1500 #  K1ABC W1AW R-10 a 2"} << 2;
    QTest::newRow("q2") << QByteArray {"0605 -10  0.3 1500 #  K1ABC W1AW R-10 q 2"} << 2;
  }

  void parsesAveragingMarkers()
  {
    QFETCH(QByteArray, line);
    QFETCH(int, count);
    auto context = preparationContext();
    context.mode = "JT65";

    auto const plan = DecodeOutputPlan::prepareLine(line, context);

    QVERIFY(plan.averaged);
    QCOMPARE(plan.averageCount, count);
  }

  void decodeFinishedWireFormats_data()
  {
    QTest::addColumn<QByteArray>("line");
    QTest::addColumn<bool>("decoded");
    QTest::addColumn<int>("singleDecodes");
    QTest::addColumn<int>("averagedDecodes");

    QTest::newRow("ft8 fixture")
      << QByteArray {"<DecodeFinished>   0  14        0"} << true << 0 << 0;
    QTest::newRow("jt9 fixture")
      << QByteArray {"<DecodeFinished>   0   6        0"} << true << 0 << 0;
    QTest::newRow("msk144 no messages")
      << QByteArray {"<DecodeFinished>   0   0        0"} << false << 0 << 0;
    QTest::newRow("q65 packed counts")
      << QByteArray {"<DecodeFinished>   1   6     2003"} << true << 2 << 3;
  }

  void decodeFinishedWireFormats()
  {
    QFETCH(QByteArray, line);
    QFETCH(bool, decoded);
    QFETCH(int, singleDecodes);
    QFETCH(int, averagedDecodes);

    auto const plan = DecodeOutputPlan::prepareLine(line, preparationContext());

    QCOMPARE(plan.disposition, DecodeOutputPlan::LineDisposition::Finish);
    QCOMPARE(plan.batchHasDecodes, decoded);
    QCOMPARE(plan.q65SingleDecodes, singleDecodes);
    QCOMPARE(plan.q65AveragedDecodes, averagedDecodes);
    QCOMPARE(actionNames(plan.actions), QStringList({"qsy"}));
  }

  void blacklistCanAbortOrContinueWithScoreReset()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    DecodeOutputPlan::KeywordFilterContext context;
    context.mode = "FT8";
    context.blacklistEnabled = true;
    context.blacklistKeywords = QStringList {"W1AW"};

    auto decision = DecodeOutputPlan::decideKeywordFilter(message, context);
    QVERIFY(decision.filtered);
    QVERIFY(!decision.continueBatch);
    QVERIFY(!decision.resetPounceScores);

    context.waitAndPounceOnly = true;
    context.pounce = true;
    context.respondSelection = "CQ: Max Dist";
    decision = DecodeOutputPlan::decideKeywordFilter(message, context);
    QVERIFY(decision.filtered);
    QVERIFY(decision.continueBatch);
    QVERIFY(decision.resetPounceScores);
  }

  void filterPrecedenceMatrix_data()
  {
    QTest::addColumn<int>("options");
    QTest::addColumn<int>("expectedOutcomes");
    QTest::addColumn<QString>("expectedKeywordReason");
    QTest::addColumn<QString>("expectedVisibilityReason");

    auto addRow = [] (char const * name, int options, int outcomes,
                      QString const& keywordReason = {},
                      QString const& visibilityReason = {}) {
      QTest::newRow(name) << options << outcomes << keywordReason << visibilityReason;
    };

    addRow("baseline", 0, ContinueBatch | DisplayLeft);
    addRow("blacklist stops batch", Blacklist, KeywordFiltered | FinallyFiltered,
           "blacklist matched");
    addRow("whitelist miss stops batch", Whitelist, KeywordFiltered | FinallyFiltered,
           "whitelist did not match");
    addRow("blacklist precedes whitelist", Blacklist | Whitelist,
           KeywordFiltered | FinallyFiltered, "blacklist matched");
    addRow("always-pass precedes keyword lists", AlwaysPass | Blacklist | Whitelist,
           ContinueBatch | AlwaysPassed | DisplayLeft);
    addRow("always-pass preserves earlier filter", InitiallyFiltered | AlwaysPass | Blacklist,
           KeywordFiltered | FinallyFiltered | ContinueBatch | AlwaysPassed);
    addRow("always-pass does not bypass ignore list", AlwaysPass | HideIgnored,
           FinallyFiltered | ContinueBatch | AlwaysPassed, {}, "ignore-list filter");
    addRow("always-pass does not bypass worked-today", AlwaysPass | HideWorkedToday,
           FinallyFiltered | ContinueBatch | AlwaysPassed, {}, "recently-worked filter");
    addRow("bypass suppresses all filters but resets scoring pounce",
           Blacklist | Whitelist | Bypass | ScoringPounce | HideIgnored | HideWorkedToday,
           ContinueBatch | ResetPounceScores | DisplayLeft, "blacklist matched");
    addRow("wait-and-pounce filter continues and displays",
           Blacklist | WaitAndPounceOnly | ScoringPounce,
           KeywordFiltered | FinallyFiltered | ContinueBatch | ResetPounceScores | DisplayLeft,
           "blacklist matched");
    addRow("non-scoring pounce does not reset", Blacklist | WaitAndPounceOnly | NonScoringPounce,
           KeywordFiltered | FinallyFiltered | ContinueBatch | DisplayLeft,
           "blacklist matched");
    addRow("stopped batch does not reset scoring pounce", Blacklist | ScoringPounce,
           KeywordFiltered | FinallyFiltered, "blacklist matched");
  }

  void filterPrecedenceMatrix()
  {
    QFETCH(int, options);
    QFETCH(int, expectedOutcomes);
    QFETCH(QString, expectedKeywordReason);
    QFETCH(QString, expectedVisibilityReason);

    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    DecodeOutputPlan::KeywordFilterContext keywordContext;
    keywordContext.mode = "FT8";
    keywordContext.alreadyFiltered = options & InitiallyFiltered;
    keywordContext.alwaysPass = options & AlwaysPass;
    keywordContext.passKeywords = QStringList {"W1AW"};
    keywordContext.blacklistEnabled = options & Blacklist;
    keywordContext.blacklistKeywords = QStringList {"W1AW"};
    keywordContext.whitelistEnabled = options & Whitelist;
    keywordContext.whitelistKeywords = QStringList {"K9XYZ"};
    keywordContext.bypass = options & Bypass;
    keywordContext.waitAndPounceOnly = options & WaitAndPounceOnly;
    keywordContext.pounce = options & (ScoringPounce | NonScoringPounce);
    keywordContext.respondSelection = options & ScoringPounce ? "CQ: Max Dist" : "CQ: First";

    auto const keywordDecision = DecodeOutputPlan::decideKeywordFilter(message, keywordContext);
    bool finalFiltered = keywordDecision.filtered;
    QString visibilityReason;
    if (keywordDecision.continueBatch) {
      DecodeOutputPlan::VisibilityFilterContext visibilityContext;
      visibilityContext.alreadyFiltered = finalFiltered;
      visibilityContext.alwaysPassed = keywordDecision.alwaysPassed;
      visibilityContext.bypass = keywordContext.bypass;
      visibilityContext.hideIgnored = options & HideIgnored;
      visibilityContext.ignoreList = "W1AW,";
      visibilityContext.hideWorkedToday = options & HideWorkedToday;
      visibilityContext.today = "2026-07-11";
      visibilityContext.txLog = "2026-07-11,12:34:56,W1AW,FN31";
      auto const visibilityDecision = DecodeOutputPlan::decideVisibilityFilter(message,
                                                                                visibilityContext);
      finalFiltered = visibilityDecision.filtered;
      visibilityReason = visibilityDecision.reason;
    }
    bool const displayLeft = keywordDecision.continueBatch
      && DecodeOutputPlan::shouldDisplayLeft(false, "FT8", SpecOp::NONE, finalFiltered,
                                             keywordContext.waitAndPounceOnly);

    QCOMPARE(keywordDecision.filtered, bool(expectedOutcomes & KeywordFiltered));
    QCOMPARE(finalFiltered, bool(expectedOutcomes & FinallyFiltered));
    QCOMPARE(keywordDecision.continueBatch, bool(expectedOutcomes & ContinueBatch));
    QCOMPARE(keywordDecision.alwaysPassed, bool(expectedOutcomes & AlwaysPassed));
    QCOMPARE(keywordDecision.resetPounceScores, bool(expectedOutcomes & ResetPounceScores));
    QCOMPARE(keywordDecision.reason, expectedKeywordReason);
    QCOMPARE(visibilityReason, expectedVisibilityReason);
    QCOMPARE(displayLeft, bool(expectedOutcomes & DisplayLeft));
  }

  void wordFilterUsesConfiguredSecondPayloadWord()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ DX W1AW FN31"};
    DecodeOutputPlan::KeywordFilterContext context;
    context.mode = "FT8";
    context.filterBySecondWord = true;
    context.blacklistEnabled = true;
    context.blacklistKeywords = QStringList {"W1AW"};

    auto const decision = DecodeOutputPlan::decideKeywordFilter(message, context);

    QCOMPARE(decision.selectedWord, QString {"W1AW"});
    QVERIFY(decision.filtered);
    QVERIFY(!decision.continueBatch);
  }

  void alwaysPassSkipsGeographyLookupButNotIgnoreList()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    DecodeOutputPlan::VisibilityFilterContext context;
    context.alwaysPassed = true;
    context.hideEurope = true;
    int continentLookups = 0;
    context.continent = [&continentLookups] (QString const&) {
      ++continentLookups;
      return QString {"EU"};
    };

    auto decision = DecodeOutputPlan::decideVisibilityFilter(message, context);
    QVERIFY(!decision.filtered);
    QCOMPARE(continentLookups, 0);

    context.hideIgnored = true;
    context.ignoreList = "W1AW,";
    decision = DecodeOutputPlan::decideVisibilityFilter(message, context);
    QVERIFY(decision.filtered);
    QCOMPARE(decision.reason, QString {"ignore-list filter"});
    QCOMPARE(continentLookups, 0);
  }

  void geographyAndRecentLogFiltersAreCharacterized()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    DecodeOutputPlan::VisibilityFilterContext context;
    context.hideEurope = true;
    context.continent = [] (QString const&) { return QString {"EU"}; };

    auto decision = DecodeOutputPlan::decideVisibilityFilter(message, context);
    QVERIFY(decision.filtered);
    QCOMPARE(decision.reason, QString {"geography or worked-before filter"});

    context = DecodeOutputPlan::VisibilityFilterContext {};
    context.hideWorkedToday = true;
    context.today = "2026-07-11";
    context.yesterday = "2026-07-10";
    context.txLog = "2026-07-11,12:34:56,W1AW,FN31";
    decision = DecodeOutputPlan::decideVisibilityFilter(message, context);
    QVERIFY(decision.filtered);
    QCOMPARE(decision.reason, QString {"recently-worked filter"});
  }

  void workedBeforeLookupRunsOnlyWhenEnabled()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    DecodeOutputPlan::VisibilityFilterContext context;
    int lookups = 0;
    context.workedBeforeOnBand = [&lookups] (QString const&, QString const&) {
      ++lookups;
      return true;
    };

    auto decision = DecodeOutputPlan::decideVisibilityFilter(message, context);
    QVERIFY(!decision.filtered);
    QCOMPARE(lookups, 0);

    context.hideWorkedBefore = true;
    decision = DecodeOutputPlan::decideVisibilityFilter(message, context);
    QVERIFY(decision.filtered);
    QCOMPARE(lookups, 1);
  }

  void experimentalFilterMarksUnknownLowSnrPayload()
  {
    DecodedText const message {"060500 -24  0.3 1500 ~  BADWORD OTHER TEXT"};
    DecodeOutputPlan::ExperimentalFilterContext context;
    context.mode = "FT8";
    context.reduceFalseDecodes = true;
    context.allCallsigns = "W1AW K1ABC";

    auto const decision = DecodeOutputPlan::decideExperimentalFilter(message, context);

    QVERIFY(decision.filtered);
    QVERIFY(!decision.reason.isEmpty());
  }

  void routesNearDecodeToRightWindow()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    auto context = routingContext();

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QVERIFY(decision.displayRight);
  }

  void farDecodeDoesNotRouteRight()
  {
    DecodedText const message {"060500 -10  0.3 1900 ~  CQ W1AW FN31"};
    auto context = routingContext();

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QVERIFY(!decision.displayRight);
  }

  void routesAveragedQ65ToRight()
  {
    DecodedText const message {"060500 -10  0.3 1500 :  K1ABC W1AW R-10"};
    auto context = routingContext();
    context.mode = "Q65";

    auto const decision = DecodeOutputPlan::decideRouting(message, message, true, context);

    QVERIFY(decision.displayRight);
  }

  void blockedRightDisplayOverridesRouting()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    auto context = routingContext();
    context.blockRightDisplay = true;

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QVERIFY(!decision.displayRight);
  }

  void houndNotForUsDoesNotRouteRight()
  {
    DecodedText const message {"060500 -10  0.3 1500 ~  CQ W1AW FN31"};
    auto context = routingContext();
    context.specOp = SpecOp::HOUND;
    context.wideGraphRxFrequency = 2000;

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QVERIFY(!decision.displayRight);
  }

  void routingOperatorIntentMatrix_data()
  {
    QTest::addColumn<QString>("line");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<int>("specOp");
    QTest::addColumn<QString>("myCall");
    QTest::addColumn<QString>("baseCall");
    QTest::addColumn<QString>("hisCall");
    QTest::addColumn<int>("rxFrequency");
    QTest::addColumn<int>("wideGraphRxFrequency");
    QTest::addColumn<bool>("averaged");
    QTest::addColumn<bool>("enableVhfFeatures");
    QTest::addColumn<bool>("expectedForUs");
    QTest::addColumn<bool>("expectedDisplayRight");

    auto addRow = [] (char const * name, QString const& line, QString const& mode,
                      SpecOp specOp, QString const& myCall, QString const& baseCall,
                      QString const& hisCall, int rxFrequency, int wideGraphRxFrequency,
                      bool averaged, bool enableVhfFeatures, bool forUs, bool displayRight) {
      QTest::newRow(name) << line << mode << static_cast<int>(specOp) << myCall << baseCall
                          << hisCall << rxFrequency << wideGraphRxFrequency << averaged
                          << enableVhfFeatures << forUs << displayRight;
    };

    addRow("compound local call does not claim base-call match",
           "060500 -10  0.3 1900 ~  K1ABC/R W1AW R-10", "FT8", SpecOp::NONE,
           "K1ABC/P", "K1ABC", "W1AW", 1500, 1500, false, false, false, false);
    addRow("FT8 base-call and DX-call pair routes",
           "060500 -10  0.3 1900 ~  CQ K1ABC W1AW", "FT8", SpecOp::NONE,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, false, true);
    addRow("FT4 base-call and DX-call pair routes",
           "060500 -10  0.3 1900 +  CQ K1ABC W1AW", "FT4", SpecOp::NONE,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, false, true);
    addRow("unaveraged Q65 not for operator stays out",
           "060500 -10  0.3 1500 :  W9XYZ W1AW R-10", "Q65", SpecOp::NONE,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, false, false);
    addRow("near JT65 routes without VHF features",
           "060500 -10  0.3 1505 #  CQ W1AW FN31", "JT65", SpecOp::NONE,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, false, true);
    addRow("near JT65 stays out with VHF features",
           "060500 -10  0.3 1505 #  CQ W1AW FN31", "JT65", SpecOp::NONE,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, true, false, false);
    addRow("FOX treats DE report as for operator",
           "060500 -10  0.3 1900 ~  DE W1AW R-10", "FT8", SpecOp::FOX,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, true, true);
    addRow("Hound suppresses near decode not for operator",
           "060500 -10  0.3 1500 ~  W9XYZ W1AW R-10", "FT8", SpecOp::HOUND,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, false, false);
    addRow("Hound retains decode for operator",
           "060500 -10  0.3 1900 ~  K1ABC W1AW R-10", "FT8", SpecOp::HOUND,
           "K1ABC", "K1ABC", "W1AW", 1500, 1500, false, false, true, true);
  }

  void routingOperatorIntentMatrix()
  {
    QFETCH(QString, line);
    QFETCH(QString, mode);
    QFETCH(int, specOp);
    QFETCH(QString, myCall);
    QFETCH(QString, baseCall);
    QFETCH(QString, hisCall);
    QFETCH(int, rxFrequency);
    QFETCH(int, wideGraphRxFrequency);
    QFETCH(bool, averaged);
    QFETCH(bool, enableVhfFeatures);
    QFETCH(bool, expectedForUs);
    QFETCH(bool, expectedDisplayRight);

    DecodedText const message {line};
    auto context = routingContext();
    context.mode = mode;
    context.specOp = static_cast<SpecOp>(specOp);
    context.myCall = myCall;
    context.baseCall = baseCall;
    context.hisCall = hisCall;
    context.rxFrequency = rxFrequency;
    context.wideGraphRxFrequency = wideGraphRxFrequency;
    context.enableVhfFeatures = enableVhfFeatures;

    auto const decision = DecodeOutputPlan::decideRouting(message, message, averaged, context);

    QCOMPARE(decision.forUs, expectedForUs);
    QCOMPARE(decision.displayRight, expectedDisplayRight);
  }

  void foxReportRequiresStandaloneReportToken_data()
  {
    QTest::addColumn<QString>("line");
    QTest::addColumn<bool>("displayRight");
    QTest::newRow("report")
      << QString {"060500 -10  0.3 1900 ~  K1ABC W1AW R-10"} << true;
    QTest::newRow("embedded-report-shape")
      << QString {"060500 -10  0.3 1900 ~  K1ABC W1AW XR-10"} << false;
  }

  void foxReportRequiresStandaloneReportToken()
  {
    QFETCH(QString, line);
    QFETCH(bool, displayRight);
    DecodedText const message {line};
    auto context = routingContext();
    context.specOp = SpecOp::FOX;

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QCOMPARE(decision.displayRight, displayRight);
  }

  void competingFoxAlertRequiresFullyFormedDecode_data()
  {
    QTest::addColumn<QString>("line");
    QTest::addColumn<bool>("competing");
    QTest::newRow("short-free-text-report")
      << QString {"060500 -10  0.3  950 ~  R-10"} << false;
    QTest::newRow("other-hound-report")
      << QString {"060500 -10  0.3  950 ~  K1XYZ W9AAA R-10"} << true;
    QTest::newRow("for-us-report")
      << QString {"060500 -10  0.3  950 ~  K1ABC W9AAA R-10"} << false;
    QTest::newRow("above-fox-band")
      << QString {"060500 -10  0.3 1500 ~  K1XYZ W9AAA R-10"} << false;
  }

  void competingFoxAlertRequiresFullyFormedDecode()
  {
    QFETCH(QString, line);
    QFETCH(bool, competing);
    DecodedText const message {line};
    auto context = routingContext();
    context.specOp = SpecOp::FOX;

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QCOMPARE(decision.competingFoxReport, competing);
  }

  void safelyMalformedDecodeDoesNotRouteRight()
  {
    DecodedText const message {"malformed"};
    auto context = routingContext();

    auto const decision = DecodeOutputPlan::decideRouting(message, message, false, context);

    QVERIFY(!decision.displayRight);
    QVERIFY(!decision.forUs);
  }

  void displayLeftSkipsAveragedFoxAndFilteredLines()
  {
    using DecodeOutputPlan::shouldDisplayLeft;
    QVERIFY(shouldDisplayLeft(false, "FT8", SpecOp::NONE, false, false));
    QVERIFY(!shouldDisplayLeft(true, "FT8", SpecOp::NONE, false, false));
    QVERIFY(!shouldDisplayLeft(false, "FT8", SpecOp::FOX, false, false));
    QVERIFY(!shouldDisplayLeft(false, "FT8", SpecOp::NONE, true, false));
    QVERIFY(shouldDisplayLeft(false, "FT8", SpecOp::NONE, true, true));
  }

  void pskPostingRequiresStandardMessageOutsideHoundMode()
  {
    using DecodeOutputPlan::shouldPostPsk;
    QVERIFY(shouldPostPsk("FT8", SpecOp::NONE, false, true, true));
    QVERIFY(!shouldPostPsk("FT8", SpecOp::NONE, false, true, false));
    QVERIFY(!shouldPostPsk("FT8", SpecOp::NONE, false, false, true));
    QVERIFY(!shouldPostPsk("FT8", SpecOp::HOUND, false, true, true));
    QVERIFY(shouldPostPsk("FT8", SpecOp::HOUND, true, true, true));
    QVERIFY(shouldPostPsk("FST4W", SpecOp::NONE, false, false, true));
  }

  void flushesFoxActivityOnlyForFoxBatch()
  {
    auto const actions = DecodeOutputPlan::finishBatchActions("FT8", SpecOp::FOX,
                                                              true, "one\ntwo\n");
    QCOMPARE(actionNames(actions), QStringList({"flush-fox"}));
    QCOMPARE(actions[0].text, QString {"one\ntwo\n"});
    QVERIFY(DecodeOutputPlan::finishBatchActions("FT8", SpecOp::NONE,
                                                 true, "one\n").isEmpty());
  }
};

QTEST_MAIN(TestDecodeOutputPlan)
#include "test_decode_output_plan.moc"
