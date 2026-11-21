// Tests for HeardByModel (rows, filters, expiry).
#include <QSignalSpy>
#include <QtTest>

#include "Network/HeardByReport.hpp"
#include "models/HeardByModel.hpp"

namespace
{
  HeardByReport report (QString const& rx, qint64 when, quint64 f, int snr,
                                 QString const& mode = "FT8", QString const& grid = "IN94")
  {
    HeardByReport r;
    r.receiver = rx; r.locator = grid; r.mode = mode; r.frequency = f; r.snr = snr; r.when = when;
    r.id = HeardByReport::id_of (rx, when, f);
    return r;
  }

  QString band_of (quint64 f)
  {
    return f > 10000000000ULL ? "3cm" : f > 2000000000ULL ? "13cm" : f > 14000000 && f < 14350000 ? "20m" : QString {};
  }
}

class TestHeardBy : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void model_groups_counts_and_ignores_repeats ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    m.set_distance_to ([] (QString const& g) {return g == "IN94" ? 1000.0 : -1.0;});
    auto const now = qint64 (1795011000);
    QVERIFY (m.add_report (report ("F6BLP", now - 600, 10489541745ULL, 9)));
    QVERIFY (m.add_report (report ("F6BLP", now - 300, 10489541745ULL, 11)));
    QVERIFY (!m.add_report (report ("F6BLP", now - 300, 10489541745ULL, 11)));   // same report again
    QVERIFY (m.add_report (report ("DK3RW", now - 200, 2400041745ULL, 4, "FT8", "JO40")));
    QVERIFY (m.add_report (report ("F6BLP", now - 900, 10489541745ULL, 15)));   // older: counts only
    m.refresh (now);
    QCOMPARE (m.rowCount (), 2);
    QCOMPARE (m.total (), 4);
    QCOMPARE (m.visible (0).receiver, QString {"DK3RW"});                        // newest first
    auto const f6 = m.visible (1);
    QCOMPARE (f6.count, 3);
    QCOMPARE (f6.snr, 11);                                                       // latest, not the older 15
    QCOMPARE (f6.band, QString {"3cm"});
    QCOMPARE (m.index (1, HeardByModel::Distance).data ().toInt (), 1000);
    QCOMPARE (m.index (0, HeardByModel::Distance).data ().toString (), QString {});  // unknown grid
    QCOMPARE (m.index (1, HeardByModel::Ago).data ().toString (), QString {"5m"});
  }

  void refresh_with_the_same_rows_does_not_reset_the_view ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    auto const now = qint64 (1795011000);
    m.add_report (report ("A1AAA", now - 10, 14074000, 1));
    QSignalSpy resets {&m, &QAbstractItemModel::modelReset};
    QSignalSpy changes {&m, &QAbstractItemModel::dataChanged};
    m.refresh (now);
    QCOMPARE (resets.count (), 1);                    // a row appeared
    m.add_report (report ("A1AAA", now - 5, 14074000, 3));
    m.refresh (now);
    QCOMPARE (resets.count (), 1);                    // same rows: updated in place
    QCOMPARE (changes.count (), 1);
    QCOMPARE (m.visible (0).snr, 3);
  }

  void model_filters_by_mode_and_band ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    auto const now = qint64 (1795011000);
    m.add_report (report ("A1AAA", now - 10, 14074000, 1, "FT8"));
    m.add_report (report ("B1BBB", now - 20, 14076000, 2, "Q65"));
    m.add_report (report ("C1CCC", now - 30, 10489541745ULL, 3, "FT8"));
    m.refresh (now);
    QCOMPARE (m.rowCount (), 3);
    m.set_filter ("FT8", "");
    QCOMPARE (m.rowCount (), 2);
    m.set_filter ("Q65A", "");                       // sub-mode letter follows the mode
    QCOMPARE (m.rowCount (), 1);
    QCOMPARE (m.visible (0).receiver, QString {"B1BBB"});
    m.set_filter ("", "3cm");
    QCOMPARE (m.rowCount (), 1);
    QCOMPARE (m.visible (0).receiver, QString {"C1CCC"});
    m.set_filter ("", "");
    QCOMPARE (m.rowCount (), 3);
    QCOMPARE (m.all_rows (), 3);
  }

  void model_drops_old_rows_and_clears ()
  {
    HeardByModel m;
    m.set_band_of (band_of);
    auto const now = qint64 (1795011000);
    m.add_report (report ("OLD1", now - HeardByModel::keep_seconds - 60, 14074000, 1));
    m.add_report (report ("NEW1", now - 60, 14074000, 1));
    m.refresh (now);
    QCOMPARE (m.rowCount (), 1);
    QCOMPARE (m.visible (0).receiver, QString {"NEW1"});
    m.clear ();
    QCOMPARE (m.rowCount (), 0);
    QCOMPARE (m.total (), 0);
    QVERIFY (m.add_report (report ("NEW1", now - 60, 14074000, 1)));             // seen-set cleared too
  }

  void model_limits_rows_and_ignores_empty_receiver ()
  {
    HeardByModel m;
    QVERIFY (!m.add_report (report ("", 1795010000, 14074000, 1)));
    auto const now = qint64 (1795011000);
    for (int i = 0; i < HeardByModel::max_rows + 20; ++i)
      m.add_report (report (QString {"S%1"}.arg (i), now - 1000 + i % 900, 14074000, 0));
    m.refresh (now);
    QCOMPARE (m.rowCount (), int (HeardByModel::max_rows));
  }
};

QTEST_MAIN (TestHeardBy)
#include "test_heard_by.moc"
