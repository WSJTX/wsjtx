#include <QtTest/QtTest>

#include "TxStartPolicy.hpp"

class TestTxStartPolicy final
  : public QObject
{
  Q_OBJECT

private slots:
  void standardMessageModesRequireText ();
  void generatedPayloadModesDoNotRequireText ();
  void unknownModesDefaultToStandardMessages ();
  void missingPayloadStopsOnlyStandardMessageModes ();
  void canStartTransmitRequiresPayloadAndOpenWindow ();
  void generatedPayloadModesRespectStartWindow ();
  void tuningCanStartWithoutPayload ();
};

void TestTxStartPolicy::standardMessageModesRequireText ()
{
  for (auto const& mode : {"FT8", "FT4", "MSK144", "Q65", "JT65", "JT4", "FST4"})
    {
      QVERIFY (requires_standard_tx_message (mode));
      QVERIFY (!tx_payload_ready (mode, 0));
      QVERIFY (tx_payload_ready (mode, 1));
    }
}

void TestTxStartPolicy::generatedPayloadModesDoNotRequireText ()
{
  for (auto const& mode : {"Echo", "WSPR", "FST4W", "JTTY"})
    {
      QVERIFY (!requires_standard_tx_message (mode));
      QVERIFY (tx_payload_ready (mode, 0));
    }
}

void TestTxStartPolicy::unknownModesDefaultToStandardMessages ()
{
  for (auto const& mode : {"", "Unknown"})
    {
      QVERIFY (requires_standard_tx_message (mode));
      QVERIFY (!tx_payload_ready (mode, 0));
      QVERIFY (tx_payload_ready (mode, 1));
    }
}

void TestTxStartPolicy::missingPayloadStopsOnlyStandardMessageModes ()
{
  QVERIFY (should_stop_for_missing_tx_payload ("FT8", 0, false));
  QVERIFY (!should_stop_for_missing_tx_payload ("FT8", 1, false));
  QVERIFY (!should_stop_for_missing_tx_payload ("Echo", 0, false));
  QVERIFY (!should_stop_for_missing_tx_payload ("WSPR", 0, false));
  QVERIFY (!should_stop_for_missing_tx_payload ("FST4W", 0, false));
  QVERIFY (!should_stop_for_missing_tx_payload ("JTTY", 0, false));
  QVERIFY (!should_stop_for_missing_tx_payload ("FT8", 0, true));
}

void TestTxStartPolicy::canStartTransmitRequiresPayloadAndOpenWindow ()
{
  QVERIFY (!can_start_transmit ("FT8", true, 0.5, 0, false));
  QVERIFY (can_start_transmit ("FT8", true, 0.5, 1, false));
  QVERIFY (!can_start_transmit ("FT8", true, 0.75, 1, false));
  QVERIFY (!can_start_transmit ("FT8", false, 0.5, 1, false));

  QVERIFY (can_start_transmit ("Echo", true, 0.5, 0, false));
  QVERIFY (!can_start_transmit ("Echo", false, 0.5, 0, false));
  QVERIFY (can_start_transmit ("WSPR", true, 0.5, 0, false));
  QVERIFY (can_start_transmit ("FST4W", true, 0.5, 0, false));

  QVERIFY (can_start_transmit ("JTTY", false, 0.95, 0, false));
}

void TestTxStartPolicy::generatedPayloadModesRespectStartWindow ()
{
  QVERIFY (can_start_transmit ("Echo", true, 0.5, 0, false));
  QVERIFY (!can_start_transmit ("Echo", true, TxStartLatestPeriodFraction, 0, false));
  QVERIFY (!can_start_transmit ("Echo", true, 0.8, 0, false));
  QVERIFY (!can_start_transmit ("WSPR", true, TxStartLatestPeriodFraction, 0, false));
  QVERIFY (!can_start_transmit ("FST4W", true, 0.8, 0, false));
  QVERIFY (can_start_transmit ("JTTY", false, 0.95, 0, false));
}

void TestTxStartPolicy::tuningCanStartWithoutPayload ()
{
  QVERIFY (can_start_transmit ("FT8", false, 0.95, 0, true));
}

QTEST_MAIN (TestTxStartPolicy)

#include "test_tx_start_policy.moc"
