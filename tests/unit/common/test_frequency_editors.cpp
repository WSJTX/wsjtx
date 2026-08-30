#include <QtTest>

#include <cmath>
#include <limits>
#include <memory>

#include <QComboBox>
#include <QDateTime>
#include <QFocusEvent>
#include <QLineEdit>
#include <QModelIndex>
#include <QString>

#include "Radio.hpp"
#include "models/Bands.hpp"
#include "models/FrequencyList.hpp"
#include "models/IARURegions.hpp"
#include "models/Modes.hpp"
#include "models/StationList.hpp"
#include "validators/LiveFrequencyValidator.hpp"
#include "widgets/FrequencyDeltaLineEdit.hpp"
#include "widgets/FrequencyLineEdit.hpp"

namespace
{
  constexpr Radio::Frequency preferred_3cm_frequency {10368200000ULL};
  constexpr Radio::Frequency entered_3cm_frequency {10368799000ULL};
  constexpr Radio::Frequency preferred_20m_frequency {14074000ULL};

  class LiveFrequencyEditorFixture
  {
  public:
    LiveFrequencyEditorFixture ()
      : frequencies {&bands}
    {
      frequencies.add ({preferred_3cm_frequency, Modes::Q65, IARURegions::ALL,
                        {}, {}, {}, {}, true});
      frequencies.add ({preferred_20m_frequency, Modes::FT8, IARURegions::ALL,
                        {}, {}, {}, {}, true});
      band_editor.setEditable (true);
      band_editor.setModel (&frequencies);
      band_editor.setModelColumn (FrequencyList_v2_101::frequency_mhz_column);
      validator = std::make_unique<LiveFrequencyValidator> (
        &band_editor, &bands, &frequencies, &nominal_frequency, false);
      band_editor.setValidator (validator.get ());
      band_editor.setCurrentText (bands.find (nominal_frequency));

      QObject::connect (validator.get (), &LiveFrequencyValidator::valid,
                        [&] (Radio::Frequency frequency) {
                          requests.append (frequency);
                          nominal_frequency = frequency;
                        });
    }

    void enter (QString const& text, QString const& completed_text = {})
    {
      band_editor.setFocus ();
      band_editor.lineEdit ()->selectAll ();
      QTest::keyClicks (band_editor.lineEdit (), text);
      if (!completed_text.isEmpty ()) band_editor.lineEdit ()->setText (completed_text);
      QTest::keyClick (band_editor.lineEdit (), Qt::Key_Return);
      QCoreApplication::processEvents ();
    }

    void send_focus_out ()
    {
      QFocusEvent event {QEvent::FocusOut, Qt::MouseFocusReason};
      QCoreApplication::sendEvent (band_editor.lineEdit (), &event);
      QCoreApplication::processEvents ();
    }

    Bands bands;
    FrequencyList_v2_101 frequencies;
    Radio::Frequency nominal_frequency {preferred_3cm_frequency};
    QComboBox band_editor;
    QVector<Radio::Frequency> requests;
    std::unique_ptr<LiveFrequencyValidator> validator;
  };
}

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
  void live_frequency_entry_requests_one_qsy_data ();
  void live_frequency_entry_requests_one_qsy ();
  void live_frequency_band_entry_requests_working_frequency ();
  void live_frequency_focus_loss_without_edit_is_harmless ();
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

void TestFrequencyEditors::live_frequency_entry_requests_one_qsy_data ()
{
  QTest::addColumn<QString> ("entry");
  QTest::addColumn<QString> ("completed_text");
  QTest::addColumn<Radio::Frequency> ("expected_frequency");

  QTest::newRow ("full-frequency")
    << QString {"10368.799000"} << QString {} << entered_3cm_frequency;
  QTest::newRow ("relative-khz")
    << QString {"799k"} << QString {} << entered_3cm_frequency;
  QTest::newRow ("completed-working-frequency")
    << QString {"14.074"} << QString {"14.074000"} << preferred_20m_frequency;
}

void TestFrequencyEditors::live_frequency_entry_requests_one_qsy ()
{
  QFETCH (QString, entry);
  QFETCH (QString, completed_text);
  QFETCH (Radio::Frequency, expected_frequency);
  LiveFrequencyEditorFixture fixture;

  fixture.enter (entry, completed_text);

  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {expected_frequency});
  QCOMPARE (fixture.band_editor.currentText (), fixture.bands.find (expected_frequency));

  fixture.send_focus_out ();
  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {expected_frequency});

  fixture.send_focus_out ();
  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {expected_frequency});
}

void TestFrequencyEditors::live_frequency_band_entry_requests_working_frequency ()
{
  LiveFrequencyEditorFixture fixture;
  fixture.nominal_frequency = 14074000;
  fixture.band_editor.setCurrentText (fixture.bands.find (fixture.nominal_frequency));

  fixture.enter ("3cm");

  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {preferred_3cm_frequency});
  QCOMPARE (fixture.band_editor.currentText (), QString {"3cm"});
}

void TestFrequencyEditors::live_frequency_focus_loss_without_edit_is_harmless ()
{
  LiveFrequencyEditorFixture fixture;
  fixture.band_editor.lineEdit ()->setText ("10368.200000");
  fixture.band_editor.setFocus ();
  QCoreApplication::processEvents ();

  fixture.send_focus_out ();
  QCOMPARE (fixture.band_editor.currentText (), QString {"3cm"});
  QVERIFY (fixture.requests.isEmpty ());
}

QTEST_MAIN (TestFrequencyEditors)

#include "test_frequency_editors.moc"
