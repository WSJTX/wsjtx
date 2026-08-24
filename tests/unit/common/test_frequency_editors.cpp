#include <QtTest>

#include <cmath>
#include <limits>

#include <QDateTime>
#include <QModelIndex>
#include <QString>

#include "Radio.hpp"
#include "models/Bands.hpp"
#include "models/FrequencyList.hpp"
#include "models/IARURegions.hpp"
#include "models/Modes.hpp"
#include "models/StationList.hpp"
#include "widgets/FrequencyDeltaLineEdit.hpp"
#include "widgets/FrequencyLineEdit.hpp"

class TestFrequencyEditors
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void frequency_line_edit_requires_positive_frequency ();
  void frequency_line_edit_accepts_frequency_above_signed_range ();
  void frequency_conversion_rejects_unsigned_limit ();
  void frequency_delta_line_edit_accepts_signed_offsets ();
  void station_list_rejects_invalid_offset_variants ();
  void frequency_list_rejects_invalid_frequency_variants ();
};

void TestFrequencyEditors::frequency_line_edit_requires_positive_frequency ()
{
  FrequencyLineEdit editor;

  bool ok {true};
  editor.setText ("0");
  QCOMPARE (editor.frequency (&ok), Radio::Frequency {0});
  QVERIFY (!ok);

  editor.setText ("0.000001");
  QCOMPARE (editor.frequency (&ok), Radio::Frequency {1});
  QVERIFY (ok);
}

void TestFrequencyEditors::frequency_line_edit_accepts_frequency_above_signed_range ()
{
  FrequencyLineEdit editor;
  editor.setText ("9223372036864.000000");

  QVERIFY (editor.hasAcceptableInput ());
  bool ok {false};
  QCOMPARE (editor.frequency (&ok), Radio::Frequency {9223372036864000000ULL});
  QVERIFY (ok);
}

void TestFrequencyEditors::frequency_conversion_rejects_unsigned_limit ()
{
  auto const limit = std::ldexp (1., std::numeric_limits<Radio::Frequency>::digits);
  bool ok {true};

  QCOMPARE (Radio::frequency (limit, 0, &ok), Radio::Frequency {0});
  QVERIFY (!ok);
}

void TestFrequencyEditors::frequency_delta_line_edit_accepts_signed_offsets ()
{
  FrequencyDeltaLineEdit editor;

  bool ok {false};
  editor.setText ("0");
  QCOMPARE (editor.frequency_delta (&ok), Radio::FrequencyDelta {0});
  QVERIFY (ok);

  editor.setText ("-0.001");
  QCOMPARE (editor.frequency_delta (&ok), Radio::FrequencyDelta {-1000});
  QVERIFY (ok);
}

void TestFrequencyEditors::station_list_rejects_invalid_offset_variants ()
{
  Bands bands;
  StationList stations {&bands, {{"20m", 1000, "Yagi"}}};
  auto const offset_index = stations.index (0, StationList::offset_column);

  QVERIFY (!stations.setData (offset_index, QString {"abc"}, Qt::EditRole));
  QCOMPARE (stations.data (offset_index, Qt::EditRole).value<Radio::FrequencyDelta> (),
            Radio::FrequencyDelta {1000});

  QVERIFY (stations.setData (offset_index, Radio::FrequencyDelta {-2000}, Qt::EditRole));
  QCOMPARE (stations.data (offset_index, Qt::EditRole).value<Radio::FrequencyDelta> (),
            Radio::FrequencyDelta {-2000});
}

void TestFrequencyEditors::frequency_list_rejects_invalid_frequency_variants ()
{
  Bands bands;
  FrequencyList_v2_101 frequencies {&bands};
  auto const added_index = frequencies.add ({14074000, Modes::FT8, IARURegions::ALL,
        QString {}, QString {}, QDateTime {}, QDateTime {}, false});
  auto const frequency_index = frequencies.index (added_index.row (), FrequencyList_v2_101::frequency_column);

  QVERIFY (!frequencies.setData (QModelIndex {}, Radio::Frequency {14075000}, Qt::EditRole));
  QVERIFY (!frequencies.setData (frequency_index, QString {"abc"}, Qt::EditRole));
  QCOMPARE (frequencies.data (frequency_index, Qt::EditRole).value<Radio::Frequency> (),
            Radio::Frequency {14074000});

  QVERIFY (!frequencies.setData (frequency_index, Radio::Frequency {0}, Qt::EditRole));
  QCOMPARE (frequencies.data (frequency_index, Qt::EditRole).value<Radio::Frequency> (),
            Radio::Frequency {14074000});

  QVERIFY (frequencies.setData (frequency_index, Radio::Frequency {14075000}, Qt::EditRole));
  QCOMPARE (frequencies.data (frequency_index, Qt::EditRole).value<Radio::Frequency> (),
            Radio::Frequency {14075000});
}

QTEST_MAIN (TestFrequencyEditors)

#include "test_frequency_editors.moc"
