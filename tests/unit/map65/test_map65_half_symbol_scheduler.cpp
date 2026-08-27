#include "half_symbol_scheduler.h"

#include <QtTest/QtTest>

class TestMap65HalfSymbolScheduler final : public QObject
{
  Q_OBJECT

private slots:
  void retries_busy_half_symbols ();
  void catches_up_one_half_symbol_at_a_time ();
  void resets_for_the_next_period ();
};

void TestMap65HalfSymbolScheduler::retries_busy_half_symbols ()
{
  Map65HalfSymbolScheduler scheduler;
  std::atomic_bool busy {false};

  QVERIFY (scheduler.claim_next (1, busy));
  QCOMPARE (scheduler.scheduled_half_symbol (), 1);
  QVERIFY (!scheduler.claim_next (2, busy));
  QCOMPARE (scheduler.scheduled_half_symbol (), 1);

  busy.store (false, std::memory_order_release);
  QVERIFY (scheduler.claim_next (2, busy));
  QCOMPARE (scheduler.scheduled_half_symbol (), 2);
}

void TestMap65HalfSymbolScheduler::catches_up_one_half_symbol_at_a_time ()
{
  Map65HalfSymbolScheduler scheduler;
  std::atomic_bool busy {false};

  for (int expected = 1; expected <= 5; ++expected)
    {
      QVERIFY (scheduler.claim_next (5, busy));
      QCOMPARE (scheduler.scheduled_half_symbol (), expected);
      busy.store (false, std::memory_order_release);
    }

  QVERIFY (!scheduler.claim_next (5, busy));
  QVERIFY (!busy.load (std::memory_order_acquire));
}

void TestMap65HalfSymbolScheduler::resets_for_the_next_period ()
{
  Map65HalfSymbolScheduler scheduler;
  std::atomic_bool busy {false};

  QVERIFY (scheduler.claim_next (1, busy));
  busy.store (false, std::memory_order_release);
  scheduler.reset ();

  QCOMPARE (scheduler.scheduled_half_symbol (), 0);
  QVERIFY (scheduler.claim_next (1, busy));
}

QTEST_GUILESS_MAIN (TestMap65HalfSymbolScheduler)

#include "test_map65_half_symbol_scheduler.moc"
