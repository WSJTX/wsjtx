#include <QtTest>

#include "widgets/BandHopping.hpp"

class TestBandHopping
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void empty_selection_returns_no_hop ()
  {
    QCOMPARE (next_band_hop_index ({false, false, false}, 0), -1);
  }

  Q_SLOT void single_selection_returns_same_entry ()
  {
    std::vector<bool> const selected {false, true, false};

    QCOMPARE (next_band_hop_index (selected, 0), 1);
    QCOMPARE (next_band_hop_index (selected, 1), 1);
    QCOMPARE (next_band_hop_index (selected, 2), 1);
  }

  Q_SLOT void multiple_selections_advance_from_start ()
  {
    std::vector<bool> const selected {false, true, false, true};

    QCOMPARE (next_band_hop_index (selected, 0), 1);
    QCOMPARE (next_band_hop_index (selected, 2), 3);
  }

  Q_SLOT void selection_wraps_after_end ()
  {
    std::vector<bool> const selected {true, false, false, true};

    QCOMPARE (next_band_hop_index (selected, 4), 0);
    QCOMPARE (next_band_hop_index (selected, 5), 3);
  }

  Q_SLOT void start_index_normalizes_safely ()
  {
    std::vector<bool> const selected {false, false, true};

    QCOMPARE (next_band_hop_index (selected, -1), 2);
    QCOMPARE (next_band_hop_index (selected, 8), 2);
  }
};

QTEST_MAIN (TestBandHopping);

#include "test_band_hopping.moc"
