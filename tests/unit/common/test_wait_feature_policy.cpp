#include <QtTest/QtTest>

#include "WaitFeaturePolicy.hpp"

class TestWaitFeaturePolicy final
  : public QObject
{
  Q_OBJECT

private slots:
  void enableTxWarning_data ();
  void enableTxWarning ();
  void waitAndCallEligibility_data ();
  void waitAndCallEligibility ();
};

void TestWaitFeaturePolicy::enableTxWarning_data ()
{
  QTest::addColumn<QString> ("mode");
  QTest::addColumn<int> ("specialOperation");
  QTest::addColumn<bool> ("waitFeaturesEnabled");
  QTest::addColumn<bool> ("autoSequenceEnabled");
  QTest::addColumn<bool> ("hasDxCall");
  QTest::addColumn<bool> ("ncccSprint");
  QTest::addColumn<int> ("expected");

  using Op = SpecialOperatingActivity;
  auto addRow = [] (char const * name, QString const& mode, Op operation,
                    bool waitEnabled, bool autoSeq, bool hasCall, bool nccc,
                    EnableTxWarning expected) {
    QTest::newRow (name) << mode << static_cast<int> (operation) << waitEnabled
                         << autoSeq << hasCall << nccc << static_cast<int> (expected);
  };

  using Warning = EnableTxWarning;
  addRow ("arrl-digi-auto-seq", "FT8", Op::ARRL_DIGI, true, true, true, false,
          Warning::WAIT_AND_REPLY);
  addRow ("arrl-digi-no-auto-seq", "FT8", Op::ARRL_DIGI, true, false, true, false,
          Warning::WAIT_AND_REPLY);
  addRow ("field-day", "FT4", Op::FIELD_DAY, true, true, true, false,
          Warning::WAIT_AND_REPLY);
  addRow ("ww-digi-no-auto-seq", "FT4", Op::WW_DIGI, true, false, true, false,
          Warning::WAIT_AND_REPLY);
  addRow ("ordinary-no-auto-seq", "Q65", Op::NONE, true, false, true, false,
          Warning::WAIT_AND_REPLY);

  for (auto const& mode : {"FT8", "FT4", "Q65", "FST4", "MSK144", "JT65", "JT9", "JT4"})
    {
      addRow (qPrintable (QString {"supported-%1"}.arg (mode)), mode, Op::NONE,
              true, true, true, false, Warning::WAIT_AND_REPLY);
    }
  for (auto const& mode : {"WSPR", "FST4W", "Echo", "JTTY", "Unknown"})
    {
      addRow (qPrintable (QString {"unsupported-%1"}.arg (mode)), mode, Op::NONE,
              true, true, true, false, Warning::NONE);
    }

  addRow ("missing-call", "FT8", Op::NONE, true, true, false, false, Warning::NONE);
  addRow ("wait-features-disabled", "FT8", Op::NONE, false, true, true, false,
          Warning::NONE);
  addRow ("fox", "FT8", Op::FOX, true, true, true, false, Warning::NONE);
  addRow ("nccc-sprint-override", "FT4", Op::NA_VHF, false, false, true, true,
          Warning::NCCC_SPRINT);
  addRow ("nccc-wrong-mode", "FT8", Op::NA_VHF, false, false, true, true, Warning::NONE);
  addRow ("hound-wait-enabled", "FT8", Op::HOUND, true, false, true, false,
          Warning::HOUND_AUTO_REPLY);
  addRow ("hound-wait-disabled", "FT8", Op::HOUND, false, false, true, false,
          Warning::HOUND_AUTO_REPLY);
  addRow ("hound-without-call", "FT8", Op::HOUND, true, false, false, false,
          Warning::NONE);
  addRow ("non-ft8-hound", "FT4", Op::HOUND, true, true, true, false,
          Warning::NONE);
}

void TestWaitFeaturePolicy::enableTxWarning ()
{
  QFETCH (QString, mode);
  QFETCH (int, specialOperation);
  QFETCH (bool, waitFeaturesEnabled);
  QFETCH (bool, autoSequenceEnabled);
  QFETCH (bool, hasDxCall);
  QFETCH (bool, ncccSprint);
  QFETCH (int, expected);

  WaitFeatureContext const context {
    mode,
    static_cast<SpecialOperatingActivity> (specialOperation),
    waitFeaturesEnabled,
    autoSequenceEnabled,
    hasDxCall,
    ncccSprint
  };
  QCOMPARE (static_cast<int> (enable_tx_warning (context)), expected);
}

void TestWaitFeaturePolicy::waitAndCallEligibility_data ()
{
  QTest::addColumn<QString> ("mode");
  QTest::addColumn<int> ("specialOperation");
  QTest::addColumn<bool> ("waitFeaturesEnabled");
  QTest::addColumn<bool> ("autoSequenceEnabled");
  QTest::addColumn<bool> ("hasDxCall");
  QTest::addColumn<bool> ("expectedArming");
  QTest::addColumn<bool> ("expectedWarning");

  using Op = SpecialOperatingActivity;
  auto addRow = [] (char const * name, QString const& mode, Op operation,
                    bool waitEnabled, bool autoSeq, bool hasCall,
                    bool expectedArming, bool expectedWarning) {
    QTest::newRow (name) << mode << static_cast<int> (operation) << waitEnabled
                         << autoSeq << hasCall << expectedArming << expectedWarning;
  };

  for (auto const& mode : {"FT8", "FT4", "Q65", "FST4", "MSK144"})
    {
      addRow (qPrintable (QString {"ordinary-%1"}.arg (mode)), mode, Op::NONE,
              true, true, true, true, true);
    }
  for (auto const& mode : {"JT65", "JT9", "JT4", "WSPR"})
    {
      addRow (qPrintable (QString {"unsupported-%1"}.arg (mode)), mode, Op::NONE,
              true, true, true, false, false);
    }

  addRow ("missing-call", "FT8", Op::NONE, true, true, false, false, false);
  addRow ("ft8-hound-with-call", "FT8", Op::HOUND, true, true, true, true, true);
  addRow ("ft8-hound-no-call", "FT8", Op::HOUND, true, true, false, true, false);
  addRow ("non-ft8-hound-with-call", "FT4", Op::HOUND, true, true, true, false, false);
  addRow ("ft4-hound-no-call", "FT4", Op::HOUND, true, true, false, false, false);
  addRow ("auto-seq-disabled", "FT8", Op::NONE, true, false, true, false, false);
  addRow ("wait-features-disabled", "FT8", Op::NONE, false, true, true, false, false);
  addRow ("arrl-digi", "FT8", Op::ARRL_DIGI, true, true, true, false, false);
  addRow ("field-day", "FT4", Op::FIELD_DAY, true, true, true, false, false);
  addRow ("nccc-sprint", "FT4", Op::NA_VHF, true, true, true, false, false);
  addRow ("fox", "FT8", Op::FOX, true, true, true, false, false);
}

void TestWaitFeaturePolicy::waitAndCallEligibility ()
{
  QFETCH (QString, mode);
  QFETCH (int, specialOperation);
  QFETCH (bool, waitFeaturesEnabled);
  QFETCH (bool, autoSequenceEnabled);
  QFETCH (bool, hasDxCall);
  QFETCH (bool, expectedArming);
  QFETCH (bool, expectedWarning);

  WaitFeatureContext const context {
    mode,
    static_cast<SpecialOperatingActivity> (specialOperation),
    waitFeaturesEnabled,
    autoSequenceEnabled,
    hasDxCall,
    false
  };
  QCOMPARE (wait_and_call_arming_eligible (context), expectedArming);
  QCOMPARE (wait_and_call_warning_eligible (context), expectedWarning);
}

QTEST_MAIN (TestWaitFeaturePolicy)

#include "test_wait_feature_policy.moc"
