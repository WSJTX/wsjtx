#include <QtTest>

#include "widgets/TxOutputFaultPolicy.hpp"

namespace
{
  using TxEvidence::TxBackendError;
  using TxEvidence::TxOutputError;
  using TxEvidence::TxPlaybackContext;
  using TxEvidence::TxSessionId;
  using TxEvidence::TxGeneration;
  using TxOutputFaultPolicy::Action;
  using TxOutputFaultPolicy::decide;

  TxPlaybackContext context (qint64 session = 10, qint64 generation = 20)
  {
    return {TxSessionId {session}, TxGeneration {generation}};
  }
}

class TestTxOutputFaultPolicy final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void currentFaultPolicy_data ()
  {
    QTest::addColumn<int> ("error");
    QTest::addColumn<bool> ("jtty");
    QTest::addColumn<int> ("expected");
    for (bool jtty : {false, true})
      {
        auto const mode = jtty ? QByteArray {"jtty-"} : QByteArray {"timed-or-tune-"};
        for (auto error : {TxBackendError::None, TxBackendError::Open,
                           TxBackendError::Io, TxBackendError::Underrun,
                           TxBackendError::Fatal, TxBackendError::Unknown})
          {
            auto expected = Action::Stop;
            if (error == TxBackendError::None) expected = Action::Ignore;
            else if (error == TxBackendError::Unknown
                     || (error == TxBackendError::Underrun && !jtty))
              expected = Action::Report;
            auto const name = mode + QByteArray::number (static_cast<int> (error));
            QTest::newRow (name.constData ())
              << static_cast<int> (error) << jtty << static_cast<int> (expected);
          }
      }
  }

  void currentFaultPolicy ()
  {
    QFETCH (int, error);
    QFETCH (bool, jtty);
    QFETCH (int, expected);
    TxOutputError const fault {context (), static_cast<TxBackendError> (error),
                               QStringLiteral ("Output failure")};
    QCOMPARE (static_cast<int> (decide (fault, context (), jtty)), expected);
  }

  void invalidAndStaleContextsAreIgnored ()
  {
    for (bool jtty : {false, true})
      for (auto error : {TxBackendError::Open, TxBackendError::Io,
                         TxBackendError::Fatal, TxBackendError::Underrun,
                         TxBackendError::Unknown})
        for (auto stale : {context (0, 20), context (10, 0), context (9, 20),
                           context (10, 19), context (11, 20), context (10, 21)})
          {
            TxOutputError const fault {stale, error, QStringLiteral ("Stale failure")};
            QCOMPARE (decide (fault, context (), jtty), Action::Ignore);
          }
  }

  void stopInvalidationRejectsDuplicateErrors ()
  {
    for (bool jtty : {false, true})
      {
        auto active = context ();
        TxOutputError const fault {active, TxBackendError::Fatal,
                                   QStringLiteral ("Fatal output failure")};
        QCOMPARE (decide (fault, active, jtty), Action::Stop);
        active = {};
        QCOMPARE (decide (fault, active, jtty), Action::Ignore);
        QCOMPARE (decide (fault, context (11, 21), jtty), Action::Ignore);
      }
  }

  void partiallyInvalidActiveContextCannotStopTransmission ()
  {
    TxOutputError const fault {context (), TxBackendError::Fatal, {}};
    QCOMPARE (decide (fault, context (0, 20), true), Action::Ignore);
    QCOMPARE (decide (fault, context (10, 0), true), Action::Ignore);
  }
};

QTEST_GUILESS_MAIN (TestTxOutputFaultPolicy)
#include "test_tx_output_fault_policy.moc"
