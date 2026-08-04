#include <QtTest>

#include "AutoRespondPeriod.hpp"
#include "Decoder/decodedtext.h"

namespace
{
  QDateTime utc(QString const& text)
  {
    auto value = QDateTime::fromString(text, Qt::ISODateWithMs);
    value.setTimeSpec(Qt::UTC);
    return value;
  }

  AutoRespondPeriodState armedState(QDateTime const& period,
                                    AutoRespondPolicy policy = AutoRespondPolicy::First)
  {
    AutoRespondPeriodState state;
    state.setEnableTx(true);
    state.observePeriod(period, true, true, policy);
    return state;
  }
}

class TestAutoRespondPeriod final : public QObject
{
  Q_OBJECT

private slots:
  void enableTxAtReceiveBoundaryArmsPeriod()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    AutoRespondPeriodState state;
    state.setEnableTx(true);

    QVERIFY(state.observePeriod(period, true, true, AutoRespondPolicy::First));
    QVERIFY(state.accepts(period));
  }

  void armedPeriodAcceptsOnlyItsDecodeSequence()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    auto state = armedState(period);

    QVERIFY(state.accepts(period));
    QVERIFY(!state.accepts(period.addSecs(-15)));
    QVERIFY(!state.accepts(period.addSecs(15)));
  }

  void enableDuringReceivePeriodArmsItProspectively()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    AutoRespondPeriodState state;
    QVERIFY(!state.observePeriod(period, true, true, AutoRespondPolicy::First));
    QVERIFY(!state.claimFirst(period));

    state.setEnableTx(true);

    QVERIFY(state.armCurrentReceivePeriod(true, AutoRespondPolicy::First));
    QVERIFY(state.claimFirst(period));
  }

  void disableAndReenableWithinReceivePeriodCanRearmIt()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    auto state = armedState(period);

    state.setEnableTx(false);
    QVERIFY(!state.accepts(period));
    state.setEnableTx(true);

    QVERIFY(state.armCurrentReceivePeriod(true, AutoRespondPolicy::First));
    QVERIFY(state.accepts(period));
  }

  void enableDuringTransmitArmsNextReceivePeriodOnly()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    AutoRespondPeriodState state;
    state.observePeriod(period, true, true, AutoRespondPolicy::First);

    auto const transmitPeriod = period.addSecs(15);
    state.observePeriod(transmitPeriod, false, true, AutoRespondPolicy::First);
    state.setEnableTx(true);
    QVERIFY(!state.armCurrentReceivePeriod(true, AutoRespondPolicy::First));
    QVERIFY(!state.accepts(period));

    auto const nextReceivePeriod = period.addSecs(30);

    QVERIFY(state.observePeriod(nextReceivePeriod, true, true, AutoRespondPolicy::First));
    QVERIFY(state.accepts(nextReceivePeriod));
  }

  void transmitPeriodKeepsDecodeWindowOpenUntilActualTransmit()
  {
    auto const receivePeriod = utc("2026-08-03T12:00:15.000Z");
    auto state = armedState(receivePeriod);

    state.observePeriod(receivePeriod.addSecs(15), false, true, AutoRespondPolicy::First);
    QVERIFY(state.accepts(receivePeriod));

    state.close();
    QVERIFY(!state.accepts(receivePeriod));
  }

  void firstPolicyClaimsOnlyOneCallerPerPeriod()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    auto state = armedState(period);

    QVERIFY(state.claimFirst(period));
    QVERIFY(!state.claimFirst(period));

    auto const nextPeriod = period.addSecs(30);
    state.observePeriod(period.addSecs(15), false, true, AutoRespondPolicy::First);
    state.observePeriod(nextPeriod, true, true, AutoRespondPolicy::First);
    QVERIFY(state.claimFirst(nextPeriod));
  }

  void repeatedEnableDoesNotResetFirstCallerClaim()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    auto state = armedState(period);

    QVERIFY(state.claimFirst(period));
    QVERIFY(!state.setEnableTx(true));
    QVERIFY(!state.armCurrentReceivePeriod(true, AutoRespondPolicy::First));
    QVERIFY(!state.claimFirst(period));
  }

  void responsePolicyIsSnapshottedForThePeriod()
  {
    auto const period = utc("2026-08-03T12:00:07.500Z");
    auto state = armedState(period, AutoRespondPolicy::MaxSignal);

    QCOMPARE(state.policy(), AutoRespondPolicy::MaxSignal);
    state.observePeriod(period, true, true, AutoRespondPolicy::MinSignal);
    QCOMPARE(state.policy(), AutoRespondPolicy::MaxSignal);
  }

  void cqIntentAndActivePolicyAreRequired()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    AutoRespondPeriodState state;
    state.setEnableTx(true);

    QVERIFY(!state.observePeriod(period, true, false, AutoRespondPolicy::First));
    state.observePeriod(period.addSecs(15), false, true, AutoRespondPolicy::First);
    QVERIFY(!state.observePeriod(period.addSecs(30), true, true, AutoRespondPolicy::None));
  }

  void currentPeriodArmingRequiresCqIntentAndActivePolicy()
  {
    auto const period = utc("2026-08-03T12:00:15.000Z");
    AutoRespondPeriodState state;
    state.observePeriod(period, true, true, AutoRespondPolicy::First);
    state.setEnableTx(true);

    QVERIFY(!state.armCurrentReceivePeriod(false, AutoRespondPolicy::First));
    QVERIFY(!state.armCurrentReceivePeriod(true, AutoRespondPolicy::None));
    QVERIFY(state.armCurrentReceivePeriod(true, AutoRespondPolicy::MaxSignal));
    QCOMPARE(state.policy(), AutoRespondPolicy::MaxSignal);
  }

  void directOpeningMessagesAreEligible_data()
  {
    QTest::addColumn<QString>("payload");

    QTest::newRow("grid") << "K1ABC W1AW FN31";
    QTest::newRow("report") << "K1ABC W1AW -10";
    QTest::newRow("positive-report") << "K1ABC W1AW +03";
  }

  void directOpeningMessagesAreEligible()
  {
    QFETCH(QString, payload);
    DecodedText message {"0605 -10  0.3 1500 ~  " + payload};

    QVERIFY(message.isStandardMessage());
    QVERIFY(isDirectAutoRespondCandidate(message, "K1ABC"));
  }

  void compoundConfiguredCallRequiresExactRecipient()
  {
    DecodedText exactCall {"0605 -10  0.3 1500 ~  K1ABC/P W1AW FN31"};
    DecodedText sharedBase {"0605 -10  0.3 1500 ~  K1ABC W1AW FN31"};

    QVERIFY(exactCall.isStandardMessage());
    QVERIFY(sharedBase.isStandardMessage());
    QVERIFY(isDirectAutoRespondCandidate(exactCall, "K1ABC/P"));
    QVERIFY(!isDirectAutoRespondCandidate(sharedBase, "K1ABC/P"));
  }

  void nonOpeningMessagesAreRejected_data()
  {
    QTest::addColumn<QString>("payload");

    QTest::newRow("generic-cq") << "CQ W1AW FN31";
    QTest::newRow("another-recipient") << "K9XYZ W1AW FN31";
    QTest::newRow("self") << "K1ABC K1ABC FN31";
    QTest::newRow("roger-report") << "K1ABC W1AW R-10";
    QTest::newRow("rrr") << "K1ABC W1AW RRR";
    QTest::newRow("rr73") << "K1ABC W1AW RR73";
    QTest::newRow("73") << "K1ABC W1AW 73";
  }

  void nonOpeningMessagesAreRejected()
  {
    QFETCH(QString, payload);
    DecodedText message {"0605 -10  0.3 1500 ~  " + payload};

    QVERIFY(!isDirectAutoRespondCandidate(message, "K1ABC"));
  }
};

QTEST_MAIN(TestAutoRespondPeriod)
#include "test_auto_respond_period.moc"
