#include <QtTest>

#include "RadioModePolicy.hpp"

class TestRadioModePolicy final : public QObject
{
  Q_OBJECT

  using Mode = Transceiver::MODE;
  using Kind = Transceiver::ModeStatusKind;
  using State = RadioModePolicy::State;

private slots:
  void initialModeDoesNotOverrideStartupPreparation ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::DIG_U);
    policy.observe ({Transceiver::CW, Kind::Observed}, 1);
    QVERIFY (policy.ready ());
    QVERIFY (!policy.yielded ());

    policy.prepare (2);
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 2);
    QVERIFY (policy.ready ());
    policy.observe ({Transceiver::CW, Kind::Observed}, 2);
    QVERIFY (policy.yielded ());
  }

  void preparationRequiresCompletion ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::DIG_U);
    policy.prepare (10);
    QVERIFY (!policy.ready ());
    QVERIFY (!policy.yielded ());
    QCOMPARE (policy.state (), State::Preparing);

    policy.observe ({Transceiver::UNK, Kind::Observed}, 10);
    QVERIFY (!policy.ready ());
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 10);
    QVERIFY (policy.ready ());
    QVERIFY (!policy.preparation_failed ());
  }

  void knownIncompatibleModesYield_data ()
  {
    QTest::addColumn<Mode> ("configured");
    QTest::addColumn<Mode> ("observed");
    QTest::newRow ("data-to-cw") << Transceiver::DIG_U << Transceiver::CW;
    QTest::newRow ("data-to-voice") << Transceiver::DIG_U << Transceiver::USB;
    QTest::newRow ("usb-to-lsb") << Transceiver::USB << Transceiver::LSB;
    QTest::newRow ("usb-to-data") << Transceiver::USB << Transceiver::DIG_U;
  }

  void knownIncompatibleModesYield ()
  {
    QFETCH (Mode, configured);
    QFETCH (Mode, observed);
    RadioModePolicy policy;
    policy.configure (configured);
    policy.prepare (1);
    policy.observe ({configured, Kind::Observed}, 1);
    QVERIFY (policy.ready ());
    policy.observe ({observed, Kind::Observed}, 1);
    QVERIFY (policy.yielded ());
    QVERIFY (!policy.ready ());
    QVERIFY (!policy.preparation_failed ());
  }

  void yieldRequiresExplicitPreparation ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::DIG_U);
    policy.prepare (10);
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 10);
    policy.observe ({Transceiver::CW, Kind::Observed}, 10);
    QVERIFY (policy.yielded ());

    policy.observe ({Transceiver::DIG_U, Kind::Preparing}, 11);
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 11);
    policy.observe ({Transceiver::DIG_U, Kind::Applied}, 12);
    QVERIFY (policy.yielded ());
    QVERIFY (!policy.ready ());

    policy.prepare (13);
    QVERIFY (!policy.yielded ());
    QVERIFY (!policy.ready ());
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 13);
    QVERIFY (policy.ready ());
  }

  void olderReportsCannotCancelNewPreparation ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::DIG_U);
    policy.prepare (20);
    policy.observe ({Transceiver::CW, Kind::Observed}, 19);
    policy.observe ({Transceiver::UNK, Kind::Unavailable}, 19);
    policy.observe ({Transceiver::DIG_U, Kind::Applied}, 19);
    QCOMPARE (policy.state (), State::Preparing);
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 20);
    QVERIFY (policy.ready ());
  }

  void unavailableObservationOnlyFailsPendingPreparation ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::DIG_U);
    policy.observe ({Transceiver::UNK, Kind::Unavailable}, 1);
    QVERIFY (policy.ready ());
    QVERIFY (!policy.preparation_failed ());

    policy.prepare (2);
    policy.observe ({Transceiver::UNK, Kind::Unavailable}, 2);
    QVERIFY (policy.yielded ());
    QVERIFY (policy.preparation_failed ());
    policy.prepare (3);
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 3);
    QVERIFY (policy.ready ());
    QVERIFY (!policy.preparation_failed ());
  }

  void commandCompletionSupportsBackendsWithoutReadback ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::USB);
    policy.prepare (1);
    policy.observe ({Transceiver::USB, Kind::Applied}, 1);
    QVERIFY (policy.ready ());
  }

  void modeNoneOptsOut ()
  {
    RadioModePolicy policy;
    policy.configure (Transceiver::DIG_U);
    policy.prepare (1);
    policy.observe ({Transceiver::DIG_U, Kind::Observed}, 1);
    policy.observe ({Transceiver::CW, Kind::Observed}, 1);
    QVERIFY (policy.yielded ());

    policy.configure (Transceiver::UNK);
    policy.prepare (2);
    policy.observe ({Transceiver::CW, Kind::Observed}, 2);
    policy.observe ({Transceiver::UNK, Kind::Unavailable}, 2);
    QVERIFY (policy.ready ());
    QVERIFY (!policy.yielded ());
    QVERIFY (!policy.preparation_failed ());
  }
};

QTEST_GUILESS_MAIN (TestRadioModePolicy)

#include "test_radio_mode_policy.moc"
