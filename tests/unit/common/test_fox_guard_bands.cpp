#include <QtTest>

#include "FoxGuardBands.hpp"

class TestFoxGuardBands
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void standard_ft8_first_and_last_guards_block ()
  {
    auto const first = FoxGuardBands::check (1840000);
    QVERIFY (first.blocked);
    QCOMPARE (static_cast<int> (first.kind), static_cast<int> (FoxGuardBands::GuardKind::StandardFT8));
    QCOMPARE (first.guard_frequency, Radio::Frequency {1840000});

    auto const last = FoxGuardBands::check (70154000);
    QVERIFY (last.blocked);
    QCOMPARE (static_cast<int> (last.kind), static_cast<int> (FoxGuardBands::GuardKind::StandardFT8));
    QCOMPARE (last.guard_frequency, Radio::Frequency {70154000});
  }

  Q_SLOT void wspr_first_and_last_guards_block ()
  {
    auto const first = FoxGuardBands::check (1836600);
    QVERIFY (first.blocked);
    QCOMPARE (static_cast<int> (first.kind), static_cast<int> (FoxGuardBands::GuardKind::WSPR));
    QCOMPARE (first.guard_frequency, Radio::Frequency {1836600});

    auto const last = FoxGuardBands::check (28124600);
    QVERIFY (last.blocked);
    QCOMPARE (static_cast<int> (last.kind), static_cast<int> (FoxGuardBands::GuardKind::WSPR));
    QCOMPARE (last.guard_frequency, Radio::Frequency {28124600});
  }

  Q_SLOT void standard_ft8_boundaries_are_exclusive_at_3000_hz ()
  {
    auto const below = FoxGuardBands::check (14074000 - 2999);
    QVERIFY (below.blocked);
    QCOMPARE (static_cast<int> (below.kind), static_cast<int> (FoxGuardBands::GuardKind::StandardFT8));
    QCOMPARE (below.guard_frequency, Radio::Frequency {14074000});

    auto const above = FoxGuardBands::check (14074000 + 2999);
    QVERIFY (above.blocked);
    QCOMPARE (static_cast<int> (above.kind), static_cast<int> (FoxGuardBands::GuardKind::StandardFT8));
    QCOMPARE (above.guard_frequency, Radio::Frequency {14074000});

    QVERIFY (!FoxGuardBands::check (14074000 - 3000).blocked);
    QVERIFY (!FoxGuardBands::check (14074000 + 3000).blocked);
  }

  Q_SLOT void wspr_boundaries_match_existing_thresholds ()
  {
    auto const guard_frequency = Radio::Frequency {28124600};

    auto const lower = FoxGuardBands::check (guard_frequency - 3499);
    QVERIFY (lower.blocked);
    QCOMPARE (static_cast<int> (lower.kind), static_cast<int> (FoxGuardBands::GuardKind::WSPR));
    QCOMPARE (lower.guard_frequency, guard_frequency);

    auto const upper = FoxGuardBands::check (guard_frequency + 299);
    QVERIFY (upper.blocked);
    QCOMPARE (static_cast<int> (upper.kind), static_cast<int> (FoxGuardBands::GuardKind::WSPR));
    QCOMPARE (upper.guard_frequency, guard_frequency);

    QVERIFY (!FoxGuardBands::check (guard_frequency - 3500).blocked);
    QVERIFY (!FoxGuardBands::check (guard_frequency + 300).blocked);
  }

  Q_SLOT void standard_ft8_match_wins_over_wspr_match ()
  {
    auto const match = FoxGuardBands::check (1000000, {1000000}, {1000000});

    QVERIFY (match.blocked);
    QCOMPARE (static_cast<int> (match.kind), static_cast<int> (FoxGuardBands::GuardKind::StandardFT8));
    QCOMPARE (match.guard_frequency, Radio::Frequency {1000000});
  }

  Q_SLOT void unrelated_frequency_is_unblocked ()
  {
    auto const match = FoxGuardBands::check (123456789);

    QVERIFY (!match.blocked);
    QCOMPARE (static_cast<int> (match.kind), static_cast<int> (FoxGuardBands::GuardKind::None));
    QCOMPARE (match.guard_frequency, Radio::Frequency {0});
  }
};

QTEST_MAIN (TestFoxGuardBands);

#include "test_fox_guard_bands.moc"
